#include "media_cache.h"

#include <QElapsedTimer>

#include <algorithm>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>

#ifdef VTHOST_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include "vthost/core/loudness.h"
#endif

namespace {

constexpr int kThumbW = 192;
constexpr int kThumbH = 108;

// Imagens que o FFmpeg não abre (ex. alguns PNG/JPG): tenta pelo Qt.
void qtImageFallback(const QString& path, MediaInfo& info) {
    QImage img(path);
    if (img.isNull()) return;
    info.ok = true;
    info.still = true;
    info.duration = 0.0;
    info.width = img.width();
    info.height = img.height();
    info.thumb = img.scaled(kThumbW, kThumbH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    info.error.clear();
}

#ifdef VTHOST_HAVE_FFMPEG
struct Deadline {
    QElapsedTimer timer;
    qint64 limitMs = 8000;
};

QString prettyCodec(AVCodecID id) {
    switch (id) {
    case AV_CODEC_ID_H264: return QStringLiteral("H.264");
    case AV_CODEC_ID_HEVC: return QStringLiteral("HEVC");
    case AV_CODEC_ID_PRORES: return QStringLiteral("ProRes");
    case AV_CODEC_ID_HAP: return QStringLiteral("HAP");
    case AV_CODEC_ID_AAC: return QStringLiteral("AAC");
    case AV_CODEC_ID_MP3: return QStringLiteral("MP3");
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S24LE:
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_F32LE: return QStringLiteral("PCM");
    default: break;
    }
    const AVCodecDescriptor* d = avcodec_descriptor_get(id);
    return d ? QString::fromLatin1(d->name).toUpper() : QString();
}

int interruptCb(void* opaque) {
    auto* d = static_cast<Deadline*>(opaque);
    return d->timer.elapsed() > d->limitMs ? 1 : 0;
}

MediaInfo probe(const QString& path) {
    MediaInfo info;
    info.ready = true;
    Deadline deadline;
    deadline.timer.start();

    AVFormatContext* fmt = avformat_alloc_context();
    if (!fmt) return info;
    fmt->interrupt_callback.callback = interruptCb;
    fmt->interrupt_callback.opaque = &deadline;
    const QByteArray p = path.toUtf8();
    if (avformat_open_input(&fmt, p.constData(), nullptr, nullptr) < 0) {
        info.error = QStringLiteral("open");
        qtImageFallback(path, info);
        return info; // avformat_open_input liberta fmt em caso de erro
    }
    AVCodecContext* cc = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    SwsContext* sws = nullptr;

    do {
        if (avformat_find_stream_info(fmt, nullptr) < 0) break;
        const AVCodec* codec = nullptr;
        const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (si < 0 || !codec || (fmt->streams[si]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            // Só áudio (mp3, wav...): sem miniatura, mas é uma media válida.
            if (const int ai = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0); ai >= 0) {
                const AVCodecParameters* ap = fmt->streams[ai]->codecpar;
                info.ok = true;
                info.hasAudio = true;
                info.audioChannels = ap->ch_layout.nb_channels;
                info.audioCodec = prettyCodec(ap->codec_id);
                if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
                    info.duration = static_cast<double>(fmt->duration) / AV_TIME_BASE;
            }
            break;
        }
        AVStream* st = fmt->streams[si];
        cc = avcodec_alloc_context3(codec);
        if (!cc || avcodec_parameters_to_context(cc, st->codecpar) < 0) break;
        cc->thread_count = 1; // um frame só: sem atraso de frame-threading
        if (avcodec_open2(cc, codec, nullptr) < 0) break;

        info.width = cc->width;
        info.height = cc->height;
        info.videoCodec = prettyCodec(st->codecpar->codec_id);
        {
            const AVRational fr = av_guess_frame_rate(fmt, st, nullptr);
            if (fr.num > 0 && fr.den > 0) info.fps = av_q2d(fr);
            if (const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(st->codecpar->format)))
                info.bitDepth = d->comp[0].depth;
        }
        if (const int ai = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0); ai >= 0) {
            const AVCodecParameters* ap = fmt->streams[ai]->codecpar;
            info.hasAudio = true;
            info.audioChannels = ap->ch_layout.nb_channels;
            info.audioCodec = prettyCodec(ap->codec_id);
        }
        if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
            info.duration = static_cast<double>(fmt->duration) / AV_TIME_BASE;
        info.still = info.duration <= 0.05 || st->nb_frames == 1;
        if (info.still) info.duration = 0.0;

        // Descodifica o primeiro frame a partir da posição actual (envia/recebe como
        // manda a API do FFmpeg: um pacote pode não dar frame e um frame pode exigir vários).
        auto decodeOne = [&]() -> bool {
            for (int guard = 0; guard < 3000; ++guard) {
                const int rr = av_read_frame(fmt, pkt);
                if (rr < 0) break; // fim do ficheiro: drena abaixo
                if (pkt->stream_index == si) {
                    const int sr = avcodec_send_packet(cc, pkt);
                    av_packet_unref(pkt);
                    if (avcodec_receive_frame(cc, frame) >= 0) return true;
                    if (sr < 0 && sr != AVERROR(EAGAIN)) continue; // pacote estragado: tenta o seguinte
                } else {
                    av_packet_unref(pkt);
                }
            }
            avcodec_send_packet(cc, nullptr);
            return avcodec_receive_frame(cc, frame) >= 0;
        };

        // Miniatura a ~10% (evita o preto do primeiro frame), no máximo a 2 s.
        bool got = false;
        if (!info.still && info.duration > 1.0) {
            const double at = std::min(2.0, info.duration * 0.1);
            const int64_t ts = (st->start_time != AV_NOPTS_VALUE ? st->start_time : 0) +
                               static_cast<int64_t>(at / av_q2d(st->time_base));
            if (av_seek_frame(fmt, si, ts, AVSEEK_FLAG_BACKWARD) >= 0) {
                avcodec_flush_buffers(cc);
                got = decodeOne();
            }
            if (!got) { // o salto falhou: volta ao início
                av_seek_frame(fmt, si, st->start_time != AV_NOPTS_VALUE ? st->start_time : 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(cc);
            }
        }
        if (!got) got = decodeOne();
        if (!got || frame->width <= 0 || frame->height <= 0) break;

        const double s = std::min(static_cast<double>(kThumbW) / frame->width, static_cast<double>(kThumbH) / frame->height);
        const int tw = std::max(2, static_cast<int>(frame->width * s));
        const int th = std::max(2, static_cast<int>(frame->height * s));
        sws = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), tw, th,
                             AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) break;
        QImage img(tw, th, QImage::Format_RGB32);
        uint8_t* dst[4] = {img.bits(), nullptr, nullptr, nullptr};
        int dstStride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
        info.thumb = img;
        info.ok = true;
    } while (false);

    sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&cc);
    avformat_close_input(&fmt);
    if (!info.ok) qtImageFallback(path, info);
    if (!info.ok && info.error.isEmpty()) info.error = QStringLiteral("decode");
    return info;
}
AudioAnalysis analyzeAudio(const QString& path) {
    AudioAnalysis a;
    a.ready = true;
    Deadline deadline;
    deadline.limitMs = 10 * 60 * 1000; // ficheiros muito longos: no máximo 10 min de análise
    deadline.timer.start();
    AVFormatContext* fmt = avformat_alloc_context();
    if (!fmt) return a;
    fmt->interrupt_callback.callback = interruptCb;
    fmt->interrupt_callback.opaque = &deadline;
    const QByteArray p = path.toUtf8();
    if (avformat_open_input(&fmt, p.constData(), nullptr, nullptr) < 0) return a;
    AVCodecContext* cc = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    SwrContext* swr = nullptr;
    do {
        if (avformat_find_stream_info(fmt, nullptr) < 0 || !pkt || !frame) break;
        const AVCodec* codec = nullptr;
        const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (si < 0 || !codec) break;
        for (unsigned i = 0; i < fmt->nb_streams; ++i)
            if (static_cast<int>(i) != si) fmt->streams[i]->discard = AVDISCARD_ALL;
        cc = avcodec_alloc_context3(codec);
        if (!cc || avcodec_parameters_to_context(cc, fmt->streams[si]->codecpar) < 0) break;
        if (avcodec_open2(cc, codec, nullptr) < 0 || cc->sample_rate <= 0) break;
        a.hasAudio = true;
        AVChannelLayout outL;
        av_channel_layout_default(&outL, 2);
        AVChannelLayout inL;
        if (cc->ch_layout.nb_channels > 0) av_channel_layout_copy(&inL, &cc->ch_layout);
        else av_channel_layout_default(&inL, 2);
        const int rc = swr_alloc_set_opts2(&swr, &outL, AV_SAMPLE_FMT_FLT, 48000, &inL, cc->sample_fmt, cc->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&inL);
        av_channel_layout_uninit(&outL);
        if (rc < 0 || !swr || swr_init(swr) < 0) break;

        double duration = 0.0;
        if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) duration = static_cast<double>(fmt->duration) / AV_TIME_BASE;
        // Resolução da forma de onda: até 50 blocos/s, no máximo ~40000 blocos.
        a.blocksPerSecond = duration > 0.0 ? std::clamp(40000.0 / duration, 1.0, 50.0) : 20.0;
        const int blockFrames = std::max(1, static_cast<int>(48000.0 / a.blocksPerSecond));
        vthost::LoudnessMeter meter(48000, 2);
        std::vector<float> buf;
        float bl = 0.0f, br = 0.0f;
        int inBlock = 0;
        auto consume = [&](const float* d, int n) {
            meter.add(d, static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) {
                bl = std::max(bl, std::abs(d[2 * i]));
                br = std::max(br, std::abs(d[2 * i + 1]));
                if (++inBlock >= blockFrames) {
                    a.peaks << std::min(1.0f, bl) << std::min(1.0f, br);
                    bl = br = 0.0f;
                    inBlock = 0;
                }
            }
        };
        auto convert = [&](const AVFrame* f) {
            const int maxOut = swr_get_out_samples(swr, f ? f->nb_samples : 0) + 64;
            if (maxOut <= 0 || maxOut > 48000 * 8) return;
            buf.resize(static_cast<std::size_t>(maxOut) * 2);
            uint8_t* out[1] = {reinterpret_cast<uint8_t*>(buf.data())};
            const int got = f ? swr_convert(swr, out, maxOut, const_cast<const uint8_t**>(f->extended_data), f->nb_samples)
                              : swr_convert(swr, out, maxOut, nullptr, 0);
            if (got > 0) consume(buf.data(), got);
        };
        int errors = 0;
        while (av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == si && avcodec_send_packet(cc, pkt) < 0) ++errors;
            av_packet_unref(pkt);
            while (avcodec_receive_frame(cc, frame) >= 0) {
                convert(frame);
                av_frame_unref(frame);
            }
            if (errors > 1000) break;
        }
        avcodec_send_packet(cc, nullptr);
        while (avcodec_receive_frame(cc, frame) >= 0) {
            convert(frame);
            av_frame_unref(frame);
        }
        convert(nullptr);
        if (inBlock > 0) a.peaks << std::min(1.0f, bl) << std::min(1.0f, br);
        a.loudness = meter.integrated();
        a.peakDb = meter.peakDb();
    } while (false);
    swr_free(&swr);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&cc);
    avformat_close_input(&fmt);
    return a;
}
#else
MediaInfo probe(const QString& path) {
    MediaInfo info;
    info.ready = true;
    QImage img(path);
    if (!img.isNull()) {
        info.ok = true;
        info.still = true;
        info.width = img.width();
        info.height = img.height();
        info.thumb = img.scaled(kThumbW, kThumbH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return info;
}

AudioAnalysis analyzeAudio(const QString&) {
    AudioAnalysis a;
    a.ready = true;
    return a;
}
#endif

} // namespace

MediaCache::MediaCache(QObject* parent) : QObject(parent) {
    pool_.setMaxThreadCount(2);
    audioPool_.setMaxThreadCount(1);
    audioPool_.setThreadPriority(QThread::LowestPriority); // nunca rouba CPU ao show
}

MediaCache::~MediaCache() {
    // As análises pendentes não podem chamar um objeto destruído.
    pool_.clear();
    audioPool_.clear();
    pool_.waitForDone(5000);
    audioPool_.waitForDone(5000);
}

AudioAnalysis MediaCache::audio(const QString& path) {
    auto it = audio_.find(path);
    if (it != audio_.end()) return it.value();
    if (!audioInFlight_.contains(path) && QFileInfo::exists(path)) {
        audioInFlight_.insert(path);
        QPointer<MediaCache> self(this);
        audioPool_.start([self, path] {
            AudioAnalysis result;
            try {
                result = analyzeAudio(path);
            } catch (...) {
                result.ready = true;
            }
            if (self)
                QMetaObject::invokeMethod(self.data(), [self, path, result] {
                    if (!self) return;
                    self->audioInFlight_.remove(path);
                    self->audio_.insert(path, result);
                    emit self->audioAnalyzed(path);
                }, Qt::QueuedConnection);
        });
    }
    return {};
}

MediaInfo MediaCache::info(const QString& path) {
    auto it = cache_.find(path);
    if (it != cache_.end()) return it.value();
    if (!inFlight_.contains(path) && QFileInfo::exists(path)) {
        inFlight_.insert(path);
        QPointer<MediaCache> self(this);
        pool_.start([self, path] {
            MediaInfo result;
            try {
                result = probe(path);
            } catch (...) {
                result.ready = true;
            }
            if (self)
                QMetaObject::invokeMethod(self.data(), [self, path, result] {
                    if (self) self->finished(path, result);
                }, Qt::QueuedConnection);
        });
    }
    MediaInfo pending;
    if (!QFileInfo::exists(path)) {
        pending.ready = true;
        pending.error = QStringLiteral("missing");
    }
    return pending;
}

void MediaCache::finished(const QString& path, const MediaInfo& info) {
    inFlight_.remove(path);
    cache_.insert(path, info);
    emit updated(path);
}

void MediaCache::forget(const QString& path) {
    cache_.remove(path);
    audio_.remove(path);
}
