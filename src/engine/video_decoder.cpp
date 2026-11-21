#include "video_decoder.h"

#include "reaper.h"
#include "vthost/core/log.h"

#include <algorithm>
#include <chrono>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace vthost {
namespace {

constexpr std::size_t kMaxQueuedFrames = 3;
constexpr int kMaxConsecutiveErrors = 64;
constexpr int kMaxDimension = 16384;

int interruptCallback(void* opaque) {
    return static_cast<std::atomic<bool>*>(opaque)->load() ? 1 : 0;
}

std::string avErr(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof(buf));
    return buf;
}

// RAII para as estruturas do FFmpeg: libertadas em qualquer caminho de saída.
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
struct Sws {
    SwsContext* p = nullptr;
    ~Sws() { sws_freeContext(p); }
};

} // namespace

VideoDecoder::VideoDecoder(std::string utf8Path, DecodeRange range, double startTime)
    : path_(std::move(utf8Path)), range_(range), startTime_(std::isfinite(startTime) ? std::max(0.0, startTime) : 0.0) {
    thread_ = std::thread([this] { run(); });
}

VideoDecoder::~VideoDecoder() {
    stop_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

bool VideoDecoder::drained() const {
    if (state_.load() != State::Ended) return false;
    std::lock_guard lock(mutex_);
    return queue_.empty();
}

std::string VideoDecoder::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}

void VideoDecoder::fail(const std::string& message) {
    {
        std::lock_guard lock(mutex_);
        error_ = message;
    }
    state_ = State::Error;
    log::error("decoder [" + path_ + "]: " + message);
}

std::shared_ptr<VideoFrame> VideoDecoder::takeFrameFor(double mediaTime) {
    std::shared_ptr<VideoFrame> chosen;
    {
        std::lock_guard lock(mutex_);
        while (!queue_.empty() && queue_.front()->time <= mediaTime) {
            if (chosen) pool_.push_back(std::move(chosen));
            chosen = std::move(queue_.front());
            queue_.pop_front();
        }
    }
    if (chosen) cv_.notify_all(); // há espaço na fila
    return chosen;
}

std::shared_ptr<VideoFrame> VideoDecoder::takeOldest() {
    std::shared_ptr<VideoFrame> f;
    {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) return nullptr;
        f = std::move(queue_.front());
        queue_.pop_front();
    }
    cv_.notify_all();
    return f;
}

void VideoDecoder::retireAsync(std::unique_ptr<VideoDecoder> decoder) {
    if (!decoder) return;
    decoder->stop_ = true; // começa já a parar
    decoder->cv_.notify_all();
    retireLater(std::shared_ptr<VideoDecoder>(std::move(decoder)));
}

void VideoDecoder::recycle(std::shared_ptr<VideoFrame> frame) {
    if (!frame) return;
    std::lock_guard lock(mutex_);
    if (pool_.size() < kMaxQueuedFrames + 1) pool_.push_back(std::move(frame));
}

std::shared_ptr<VideoFrame> VideoDecoder::allocFrame() {
    std::lock_guard lock(mutex_);
    if (!pool_.empty()) {
        auto f = std::move(pool_.back());
        pool_.pop_back();
        return f;
    }
    return std::make_shared<VideoFrame>();
}

void VideoDecoder::run() {
    try {
        FormatCtx fmt;
        fmt.p = avformat_alloc_context();
        if (!fmt.p) return fail("sem memória");
        fmt.p->interrupt_callback.callback = interruptCallback;
        fmt.p->interrupt_callback.opaque = &stop_;

        int rc = avformat_open_input(&fmt.p, path_.c_str(), nullptr, nullptr);
        if (rc < 0) {
            opened_ = true;
            return fail("não foi possível abrir: " + avErr(rc));
        }
        rc = avformat_find_stream_info(fmt.p, nullptr);
        if (rc < 0) {
            opened_ = true;
            return fail("ficheiro ilegível: " + avErr(rc));
        }
        if (fmt.p->duration != AV_NOPTS_VALUE && fmt.p->duration > 0)
            duration_ = static_cast<double>(fmt.p->duration) / AV_TIME_BASE;

        const AVCodec* codec = nullptr;
        const int streamIndex = av_find_best_stream(fmt.p, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        // Capas de álbum (mp3 com imagem) contam como "sem vídeo".
        if (streamIndex < 0 || !codec || (fmt.p->streams[streamIndex]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            opened_ = true;
            if (av_find_best_stream(fmt.p, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0) {
                state_ = State::NoVideo;
                return;
            }
            return fail("sem stream de vídeo nem de áudio suportada");
        }
        AVStream* stream = fmt.p->streams[streamIndex];

        CodecCtx cc;
        cc.p = avcodec_alloc_context3(codec);
        if (!cc.p) return fail("sem memória");
        if ((rc = avcodec_parameters_to_context(cc.p, stream->codecpar)) < 0)
            return fail("parâmetros de codec inválidos: " + avErr(rc));
        cc.p->thread_count = 0; // automático
        if ((rc = avcodec_open2(cc.p, codec, nullptr)) < 0)
            return fail("codec não suportado: " + avErr(rc));

        const double tb = av_q2d(stream->time_base);
        opened_ = true;
        const AVRational fr = av_guess_frame_rate(fmt.p, stream, nullptr);
        const double frameDur = (fr.num > 0 && fr.den > 0) ? av_q2d(av_inv_q(fr)) : 1.0 / 30.0;

        // Zona In..Out e passagem inicial (recuperação / saltos a meio).
        const double fileDur = duration_.load();
        const double effOut = range_.effectiveOut(fileDur); // 0 = desconhecido (até ao EOF)
        const double passLen = range_.passLength(fileDur);
        int passIndex = 0;
        double seekTarget = range_.in;
        range_.split(startTime_, fileDur, passIndex, seekTarget);
        double passOffset = passLen > 0.0 ? passIndex * passLen : 0.0;
        const int64_t startPtsStream = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
        auto seekTo = [&](double target) -> bool {
            if (target <= 0.0) {
                if (av_seek_frame(fmt.p, streamIndex, startPtsStream, AVSEEK_FLAG_BACKWARD) < 0 &&
                    avformat_seek_file(fmt.p, -1, INT64_MIN, 0, INT64_MAX, 0) < 0)
                    return false;
            } else if (av_seek_frame(fmt.p, streamIndex, startPtsStream + static_cast<int64_t>(target / tb),
                                     AVSEEK_FLAG_BACKWARD) < 0) {
                return false;
            }
            avcodec_flush_buffers(cc.p);
            return true;
        };
        if (seekTarget > 0.0 && !seekTo(seekTarget)) seekTarget = 0.0;

        Packet pkt;
        Frame frame;
        Sws sws;
        if (!pkt.p || !frame.p) return fail("sem memória");

        double lastTime = -1.0;
        double lastLocal = 0.0;
        int64_t framesThisPass = 0;
        int64_t framesTotal = 0;
        int consecutiveErrors = 0;
        bool draining = false;
        bool skipUntilTarget = seekTarget > 0.0;
        bool passDone = false; // chegou ao ponto Out

        state_ = State::Playing;

        auto emitFrame = [&](AVFrame* f) -> bool {
            const int w = f->width, h = f->height;
            if (w <= 0 || h <= 0 || w > kMaxDimension || h > kMaxDimension) {
                ++consecutiveErrors;
                return true; // salta frame estranho
            }
            int64_t pts = f->best_effort_timestamp;
            double local = (pts != AV_NOPTS_VALUE) ? (pts - startPtsStream) * tb : lastLocal + frameDur;
            if (!std::isfinite(local) || local < 0.0) local = framesThisPass * frameDur;
            lastLocal = local;
            ++framesThisPass;
            ++framesTotal;

            if (skipUntilTarget) {
                if (local + frameDur * 0.5 < seekTarget) return true;
                skipUntilTarget = false;
            }
            if (effOut > 0.0 && local >= effOut - frameDur * 0.25) {
                passDone = true; // depois do ponto Out: esta passagem acabou
                return true;
            }

            double t = local + passOffset;
            if (t <= lastTime) t = lastTime + frameDur * 0.5; // tempos sempre crescentes
            lastTime = t;

            // Espera por espaço na fila (memória limitada).
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [&] { return stop_.load() || queue_.size() < kMaxQueuedFrames; });
                if (stop_) return false;
            }

            sws.p = sws_getCachedContext(sws.p, w, h, static_cast<AVPixelFormat>(f->format), w, h,
                                         AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws.p) {
                ++consecutiveErrors;
                return true;
            }
            auto out = allocFrame();
            out->width = w;
            out->height = h;
            out->time = t;
            out->bgra.resize(static_cast<std::size_t>(w) * h * 4);
            uint8_t* dst[4] = {out->bgra.data(), nullptr, nullptr, nullptr};
            int dstStride[4] = {w * 4, 0, 0, 0};
            sws_scale(sws.p, f->data, f->linesize, 0, h, dst, dstStride);

            {
                std::lock_guard lock(mutex_);
                queue_.push_back(std::move(out));
            }
            consecutiveErrors = 0;
            return true;
        };

        while (!stop_) {
            if (!draining) {
                rc = av_read_frame(fmt.p, pkt.p);
                if (rc == AVERROR_EOF) {
                    draining = true;
                    avcodec_send_packet(cc.p, nullptr); // flush
                } else if (rc < 0) {
                    if (stop_) break;
                    if (++consecutiveErrors > kMaxConsecutiveErrors)
                        return fail("erros de leitura repetidos: " + avErr(rc));
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                } else {
                    if (pkt.p->stream_index == streamIndex) {
                        rc = avcodec_send_packet(cc.p, pkt.p);
                        if (rc < 0 && rc != AVERROR(EAGAIN)) ++consecutiveErrors; // pacote corrompido: salta
                    }
                    av_packet_unref(pkt.p);
                }
            }

            // Recebe todos os frames disponíveis.
            while (!stop_) {
                rc = avcodec_receive_frame(cc.p, frame.p);
                if (rc == AVERROR(EAGAIN)) break;
                if (rc == AVERROR_EOF) break;
                if (rc < 0) {
                    ++consecutiveErrors;
                    break;
                }
                const bool keepGoing = emitFrame(frame.p);
                av_frame_unref(frame.p);
                if (!keepGoing) break;
            }
            if (consecutiveErrors > kMaxConsecutiveErrors) return fail("demasiados erros de descodificação");

            if (passDone || (draining && rc == AVERROR_EOF)) {
                // Fim de uma passagem (ponto Out ou fim do ficheiro).
                if (framesTotal <= 1 && !passDone) {
                    still_ = true; // imagem fixa: o frame único fica no ecrã
                    state_ = State::Ended;
                    break;
                }
                ++passIndex;
                if (range_.passes > 0 && passIndex >= range_.passes) {
                    state_ = State::Ended;
                    break;
                }
                // Próxima passagem: volta ao ponto In.
                passOffset = passLen > 0.0 ? passIndex * passLen : lastTime + frameDur - range_.in;
                framesThisPass = 0;
                lastLocal = range_.in;
                passDone = false;
                seekTarget = range_.in;
                skipUntilTarget = seekTarget > 0.0;
                if (!seekTo(seekTarget)) return fail("não foi possível voltar ao ponto In para repetir");
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
