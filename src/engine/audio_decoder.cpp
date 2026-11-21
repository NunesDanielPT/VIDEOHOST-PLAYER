#include "audio_decoder.h"

#include "vthost/core/log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace vthost {
namespace {

constexpr std::size_t kMaxQueuedFrames = AudioDecoder::kRate; // ~1 s em fila
constexpr int kMaxConsecutiveErrors = 64;
constexpr double kAlignTolerance = 0.030; // desvio aceite antes de corrigir (s)

int interruptCallback(void* opaque) { return static_cast<std::atomic<bool>*>(opaque)->load() ? 1 : 0; }

std::string avErr(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof(buf));
    return buf;
}

struct FormatCtx {
    AVFormatContext* p = nullptr;
    ~FormatCtx() { if (p) avformat_close_input(&p); }
};
struct CodecCtx {
    AVCodecContext* p = nullptr;
    ~CodecCtx() { if (p) avcodec_free_context(&p); }
};
struct Packet {
    AVPacket* p = av_packet_alloc();
    ~Packet() { av_packet_free(&p); }
};
struct Frame {
    AVFrame* p = av_frame_alloc();
    ~Frame() { av_frame_free(&p); }
};
struct Swr {
    SwrContext* p = nullptr;
    ~Swr() { swr_free(&p); }
};

} // namespace

AudioDecoder::AudioDecoder(std::string utf8Path, DecodeRange range, double startTime, double speed)
    : path_(std::move(utf8Path)),
      range_(range),
      startTime_(std::isfinite(startTime) ? std::max(0.0, startTime) : 0.0),
      speed_(std::isfinite(speed) ? std::clamp(speed, 0.25, 4.0) : 1.0),
      step_(speed_ / kRate) {
    thread_ = std::thread([this] { run(); });
}

AudioDecoder::~AudioDecoder() {
    stop_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

std::string AudioDecoder::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

void AudioDecoder::fail(const std::string& message) {
    {
        std::lock_guard lock(mutex_);
        error_ = message;
    }
    state_ = State::Error;
    log::error("áudio [" + path_ + "]: " + message);
}

bool AudioDecoder::finished() const {
    const State s = state_.load();
    if (s != State::Ended && s != State::Error && s != State::NoAudio) return false;
    std::lock_guard lock(mutex_);
    return queue_.empty();
}

bool AudioDecoder::hasData() const {
    std::lock_guard lock(mutex_);
    return !queue_.empty();
}

bool AudioDecoder::push(Chunk&& c) {
    const std::size_t frames = c.data.size() / kChannels;
    if (frames == 0) return true;
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return stop_.load() || queuedFrames_ + frames <= kMaxQueuedFrames || queue_.empty(); });
    if (stop_) return false;
    queuedFrames_ += frames;
    queue_.push_back(std::move(c));
    return true;
}

std::size_t AudioDecoder::read(double mediaTime, float* out, std::size_t frames) {
    if (!out || frames == 0 || !std::isfinite(mediaTime)) return 0;
    std::size_t written = 0;
    std::size_t withSound = 0;
    double t = mediaTime;
    bool consumed = false;
    {
        std::lock_guard lock(mutex_);
        int guard = 0;
        while (written < frames && !queue_.empty() && ++guard < 100000) {
            Chunk& c = queue_.front();
            const std::size_t total = c.data.size() / kChannels;
            if (c.pos >= total) {
                queuedFrames_ -= std::min(queuedFrames_, total);
                queue_.pop_front();
                consumed = true;
                continue;
            }
            const std::size_t avail = total - c.pos;
            const double cursor = c.time + static_cast<double>(c.pos) * step_;
            const double diff = cursor - t;
            if (diff > kAlignTolerance) {
                // O som começa mais tarde (ex. início do vídeo sem áudio): silêncio até lá.
                const auto gap = static_cast<std::size_t>(std::ceil(diff / step_));
                const std::size_t n = std::min(frames - written, gap);
                written += n; // já está a zero
                t += static_cast<double>(n) * step_;
                continue;
            }
            if (diff < -kAlignTolerance) {
                // O som está atrasado em relação ao relógio: salta o que já passou.
                const auto late = static_cast<std::size_t>(-diff / step_);
                c.pos += std::max<std::size_t>(1, std::min(avail, late));
                consumed = true;
                continue;
            }
            const std::size_t n = std::min(avail, frames - written);
            std::memcpy(out + written * kChannels, c.data.data() + c.pos * kChannels, n * kChannels * sizeof(float));
            c.pos += n;
            written += n;
            withSound += n;
            t += static_cast<double>(n) * step_;
            consumed = true;
        }
    }
    if (consumed) cv_.notify_all();
    return withSound;
}

void AudioDecoder::run() {
    try {
        FormatCtx fmt;
        fmt.p = avformat_alloc_context();
        if (!fmt.p) return fail("sem memória");
        fmt.p->interrupt_callback.callback = interruptCallback;
        fmt.p->interrupt_callback.opaque = &stop_;

        int rc = avformat_open_input(&fmt.p, path_.c_str(), nullptr, nullptr);
        if (rc < 0) return fail("não foi possível abrir: " + avErr(rc));
        rc = avformat_find_stream_info(fmt.p, nullptr);
        if (rc < 0) return fail("ficheiro ilegível: " + avErr(rc));

        const AVCodec* codec = nullptr;
        const int si = av_find_best_stream(fmt.p, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (si < 0 || !codec) {
            state_ = State::NoAudio; // vídeo sem som: normal
            return;
        }
        // Só o stream de áudio interessa: o demuxer descarta o resto mais depressa.
        for (unsigned i = 0; i < fmt.p->nb_streams; ++i)
            if (static_cast<int>(i) != si) fmt.p->streams[i]->discard = AVDISCARD_ALL;
        AVStream* stream = fmt.p->streams[si];

        CodecCtx cc;
        cc.p = avcodec_alloc_context3(codec);
        if (!cc.p) return fail("sem memória");
        if ((rc = avcodec_parameters_to_context(cc.p, stream->codecpar)) < 0)
            return fail("parâmetros de áudio inválidos: " + avErr(rc));
        if ((rc = avcodec_open2(cc.p, codec, nullptr)) < 0) return fail("codec de áudio não suportado: " + avErr(rc));
        if (cc.p->sample_rate <= 0 || cc.p->sample_rate > 768000) return fail("frequência de áudio inválida");

        double fileDur = 0.0;
        if (fmt.p->duration != AV_NOPTS_VALUE && fmt.p->duration > 0)
            fileDur = static_cast<double>(fmt.p->duration) / AV_TIME_BASE;
        const double tb = av_q2d(stream->time_base);
        const int64_t startPts = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
        const double effOut = range_.effectiveOut(fileDur);
        const double passLen = range_.passLength(fileDur);
        int passIndex = 0;
        double seekTarget = range_.in;
        range_.split(startTime_, fileDur, passIndex, seekTarget);
        double passOffset = passLen > 0.0 ? passIndex * passLen : 0.0;

        auto seekTo = [&](double target) -> bool {
            const int64_t ts = startPts + static_cast<int64_t>(std::max(0.0, target) / tb);
            if (av_seek_frame(fmt.p, si, ts, AVSEEK_FLAG_BACKWARD) < 0 &&
                avformat_seek_file(fmt.p, -1, INT64_MIN, 0, INT64_MAX, 0) < 0)
                return false;
            avcodec_flush_buffers(cc.p);
            return true;
        };
        if (seekTarget > 0.0 && !seekTo(seekTarget)) seekTarget = 0.0;

        // Reamostragem para 48 kHz estéreo float; a velocidade muda a frequência de saída.
        const int outRate = std::max(8000, static_cast<int>(std::lround(kRate / speed_)));
        Swr swr;
        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, kChannels);
        AVChannelLayout inLayout;
        if (cc.p->ch_layout.nb_channels > 0) av_channel_layout_copy(&inLayout, &cc.p->ch_layout);
        else av_channel_layout_default(&inLayout, 2);
        rc = swr_alloc_set_opts2(&swr.p, &outLayout, AV_SAMPLE_FMT_FLT, outRate, &inLayout, cc.p->sample_fmt,
                                 cc.p->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&inLayout);
        av_channel_layout_uninit(&outLayout);
        if (rc < 0 || !swr.p || swr_init(swr.p) < 0) return fail("não foi possível converter o áudio");

        Packet pkt;
        Frame frame;
        if (!pkt.p || !frame.p) return fail("sem memória");

        state_ = State::Playing;
        int consecutiveErrors = 0;
        bool draining = false;
        bool passDone = false;
        bool skipUntilTarget = seekTarget > 0.0;
        double nextTime = -1.0; // tempo acumulado do próximo frame de saída (<0 = ressincronizar)
        std::vector<float> buf;

        auto emitFrame = [&](AVFrame* f) -> bool {
            if (f->nb_samples <= 0) return true;
            const double rate = f->sample_rate > 0 ? f->sample_rate : cc.p->sample_rate;
            const double frameDur = f->nb_samples / rate;
            const int64_t pts = f->best_effort_timestamp;
            double local = pts != AV_NOPTS_VALUE ? (pts - startPts) * tb : -1.0;
            if (!std::isfinite(local)) local = -1.0;
            if (skipUntilTarget && local >= 0.0) {
                if (local + frameDur < seekTarget) return true;
                skipUntilTarget = false;
            }
            int inSamples = f->nb_samples;
            if (effOut > 0.0 && local >= 0.0) {
                if (local >= effOut) {
                    passDone = true;
                    return true;
                }
                if (local + frameDur > effOut) { // corta no ponto Out
                    inSamples = std::clamp(static_cast<int>((effOut - local) * rate), 0, f->nb_samples);
                    passDone = true;
                }
            }
            if (inSamples <= 0) return true;
            const int maxOut = swr_get_out_samples(swr.p, inSamples) + 32;
            if (maxOut <= 0 || maxOut > 8 * kRate) {
                ++consecutiveErrors;
                return true;
            }
            buf.resize(static_cast<std::size_t>(maxOut) * kChannels);
            uint8_t* outPtr[1] = {reinterpret_cast<uint8_t*>(buf.data())};
            const int got = swr_convert(swr.p, outPtr, maxOut, const_cast<const uint8_t**>(f->extended_data), inSamples);
            if (got < 0) {
                ++consecutiveErrors;
                return true;
            }
            consecutiveErrors = 0;
            if (got == 0) return true;
            Chunk c;
            const double ptsTime = local >= 0.0 ? local + passOffset : -1.0;
            // Continuidade: acumula; ressincroniza se o pts divergir (ficheiros com buracos).
            if (nextTime < 0.0 || (ptsTime >= 0.0 && std::abs(ptsTime - nextTime) > 0.1)) nextTime = std::max(0.0, ptsTime);
            c.time = nextTime;
            c.data.assign(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(got) * kChannels);
            nextTime += got * step_;
            return push(std::move(c));
        };

        while (!stop_) {
            if (!draining) {
                rc = av_read_frame(fmt.p, pkt.p);
                if (rc == AVERROR_EOF) {
                    draining = true;
                    avcodec_send_packet(cc.p, nullptr);
                } else if (rc < 0) {
                    if (stop_) break;
                    if (++consecutiveErrors > kMaxConsecutiveErrors) return fail("erros de leitura repetidos: " + avErr(rc));
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                } else {
                    if (pkt.p->stream_index == si) {
                        rc = avcodec_send_packet(cc.p, pkt.p);
                        if (rc < 0 && rc != AVERROR(EAGAIN)) ++consecutiveErrors;
                    }
                    av_packet_unref(pkt.p);
                }
            }
            bool keepGoing = true;
            while (!stop_ && !passDone) {
                rc = avcodec_receive_frame(cc.p, frame.p);
                if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
                if (rc < 0) {
                    ++consecutiveErrors;
                    break;
                }
                keepGoing = emitFrame(frame.p);
                av_frame_unref(frame.p);
                if (!keepGoing) break;
            }
            if (!keepGoing || stop_) break;
            if (consecutiveErrors > kMaxConsecutiveErrors) return fail("demasiados erros de descodificação de áudio");

            if (passDone || (draining && rc == AVERROR_EOF)) {
                ++passIndex;
                if (range_.passes > 0 && passIndex >= range_.passes) {
                    state_ = State::Ended;
                    break;
                }
                // Próxima passagem: volta ao ponto In.
                passOffset = passLen > 0.0 ? passIndex * passLen : std::max(0.0, nextTime - range_.in);
                passDone = false;
                seekTarget = range_.in;
                skipUntilTarget = seekTarget > 0.0;
                if (!seekTo(seekTarget)) return fail("não foi possível voltar ao ponto In para repetir");
                swr_init(swr.p); // descarta o que ficou na reamostragem
                nextTime = -1.0;
                draining = false;
            }
        }
    } catch (const std::bad_alloc&) {
        fail("sem memória");
    } catch (const std::exception& e) {
        fail(std::string("excepção: ") + e.what());
    } catch (...) {
        fail("excepção desconhecida");
    }
}

} // namespace vthost
