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
#include <libswscale/swscale.h>
}
#endif

namespace {

constexpr int kThumbW = 192;
constexpr int kThumbH = 108;

#ifdef VTHOST_HAVE_FFMPEG
struct Deadline {
    QElapsedTimer timer;
    qint64 limitMs = 8000;
};

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
        if (si < 0 || !codec) break;
        AVStream* st = fmt->streams[si];
        cc = avcodec_alloc_context3(codec);
        if (!cc || avcodec_parameters_to_context(cc, st->codecpar) < 0) break;
        cc->thread_count = 2;
        if (avcodec_open2(cc, codec, nullptr) < 0) break;

        info.width = cc->width;
        info.height = cc->height;
        if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
            info.duration = static_cast<double>(fmt->duration) / AV_TIME_BASE;
        info.still = info.duration <= 0.05 || st->nb_frames == 1;
        if (info.still) info.duration = 0.0;

        // Miniatura a ~10% (evita o preto do primeiro frame), no máximo a 2 s.
        if (!info.still && info.duration > 1.0) {
            const double at = std::min(2.0, info.duration * 0.1);
            const int64_t ts = (st->start_time != AV_NOPTS_VALUE ? st->start_time : 0) +
                               static_cast<int64_t>(at / av_q2d(st->time_base));
            if (av_seek_frame(fmt, si, ts, AVSEEK_FLAG_BACKWARD) >= 0) avcodec_flush_buffers(cc);
        }

        bool got = false;
        int guard = 0;
        while (!got && guard++ < 500 && av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == si && avcodec_send_packet(cc, pkt) >= 0)
                got = avcodec_receive_frame(cc, frame) >= 0;
            av_packet_unref(pkt);
        }
        if (!got) {
            avcodec_send_packet(cc, nullptr);
            got = avcodec_receive_frame(cc, frame) >= 0;
        }
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
    if (!info.ok && info.error.isEmpty()) info.error = QStringLiteral("decode");
    return info;
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
#endif

} // namespace

MediaCache::MediaCache(QObject* parent) : QObject(parent) {}

MediaCache::~MediaCache() {
    // As análises pendentes não podem chamar um objeto destruído.
    QThreadPool::globalInstance()->clear();
    QThreadPool::globalInstance()->waitForDone(5000);
}

MediaInfo MediaCache::info(const QString& path) {
    auto it = cache_.find(path);
    if (it != cache_.end()) return it.value();
    if (!inFlight_.contains(path) && QFileInfo::exists(path)) {
        inFlight_.insert(path);
        QPointer<MediaCache> self(this);
        QThreadPool::globalInstance()->start([self, path] {
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

double MediaCache::suggestedClipDuration(const QString& path, double imageSeconds) {
    const MediaInfo i = info(path);
    if (i.ready && i.ok && !i.still && i.duration > 0.1) return i.duration;
    return imageSeconds > 0.0 ? imageSeconds : 10.0;
}
