#include "layer_player.h"

#include <algorithm>
#include <cmath>

namespace vthost {

namespace {
// Esvazia um slot sem bloquear o render loop.
template <typename SlotT>
void retire(SlotT& s) {
    VideoDecoder::retireAsync(std::move(s.decoder));
    s = SlotT{};
}
} // namespace

LayerPlayer::~LayerPlayer() {
    retire(current_);
    retire(previous_);
}

double LayerPlayer::Slot::mediaTime(bool paused) const {
    if (!clockStarted) return 0.0;
    if (paused) return frozenTime;
    return clockBase + std::chrono::duration<double>(Clock::now() - clockStart).count();
}

void LayerPlayer::setPaused(bool paused) {
    if (paused == paused_) return;
    for (Slot* s : {&current_, &previous_}) {
        if (!s->clockStarted) continue;
        if (paused) {
            s->frozenTime = s->mediaTime(false);
        } else {
            // Retoma a partir do tempo congelado.
            s->clockBase = s->frozenTime;
            s->clockStart = Clock::now();
        }
    }
    paused_ = paused;
}

void LayerPlayer::play(const std::string& mediaId, const std::string& utf8Path, bool loop,
                       double fadeSeconds, double startSeconds, const std::string& tag, FadeStyle style) {
    // A media actual passa a "anterior" e desaparece com o fade.
    // (Se já havia um fade a decorrer, a anterior antiga é descartada.)
    retire(previous_);
    previous_ = std::move(current_);
    current_ = Slot{};
    current_.mediaId = mediaId;
    current_.tag = tag;
    current_.path = utf8Path;
    current_.loop = loop;
    current_.decoder = std::make_unique<VideoDecoder>(utf8Path, loop, startSeconds);
    fadeSeconds_ = std::clamp(fadeSeconds, 0.0, 60.0);
    fadeStyle_ = style;
    fadeStart_ = Clock::now();
}

void LayerPlayer::seek(double seconds) {
    if (!current_.decoder || current_.path.empty() || current_.decoder->isStill()) return;
    if (!std::isfinite(seconds)) return;
    const double d = current_.decoder->duration();
    seconds = std::max(0.0, seconds);
    if (d > 0.0) seconds = std::min(seconds, std::max(0.0, d - 0.05));
    // A textura fica: o frame actual mantém-se até o descodificador novo ter o primeiro.
    VideoDecoder::retireAsync(std::move(current_.decoder));
    current_.decoder = std::make_unique<VideoDecoder>(current_.path, current_.loop, seconds);
    current_.clockStarted = false;
    current_.shownTime = seconds;
    current_.frozenTime = seconds;
}

double LayerPlayer::duration() const {
    if (!current_.decoder || current_.decoder->isStill()) return 0.0;
    const double d = current_.decoder->duration();
    return std::isfinite(d) ? std::max(0.0, d) : 0.0;
}

void LayerPlayer::stop(double fadeSeconds, FadeStyle style) {
    if (!current_.decoder && !current_.texture.valid()) return;
    retire(previous_);
    previous_ = std::move(current_);
    current_ = Slot{};
    fadeSeconds_ = std::clamp(fadeSeconds, 0.0, 60.0);
    fadeStyle_ = style;
    fadeStart_ = Clock::now();
}

float LayerPlayer::fadeProgress() const {
    if (fadeSeconds_ <= 0.0) return 1.0f;
    const double t = std::chrono::duration<double>(Clock::now() - fadeStart_).count() / fadeSeconds_;
    return static_cast<float>(std::clamp(t, 0.0, 1.0));
}

void LayerPlayer::updateSlot(Slot& s, Renderer& renderer) {
    if (!s.decoder) return;
    if (!s.clockStarted) {
        // O relógio só arranca quando o primeiro frame existe: abrir um ficheiro
        // lento não faz o vídeo começar "a meio".
        auto first = s.decoder->takeOldest();
        if (!first) return;
        s.clockStarted = true;
        s.clockBase = first->time;
        s.clockStart = Clock::now();
        s.frozenTime = first->time; // em pausa (ex. seek na timeline) mostra este frame
        if (renderer.upload(s.texture, *first)) {
            s.hasFrame = true;
            s.shownTime = first->time;
        }
        s.decoder->recycle(std::move(first));
        return;
    }
    auto frame = s.decoder->takeFrameFor(s.mediaTime(paused_));
    if (!frame) return; // frame hold: fica o último frame bom
    if (renderer.upload(s.texture, *frame)) {
        s.hasFrame = true;
        s.shownTime = frame->time;
    }
    s.decoder->recycle(std::move(frame));
}

void LayerPlayer::update(Renderer& renderer) {
    updateSlot(current_, renderer);
    // A media anterior só continua a ser descodificada enquanto o fade decorre.
    if (previous_.decoder || previous_.texture.valid()) {
        if (fadeProgress() >= 1.0f) retire(previous_);
        else updateSlot(previous_, renderer);
    }
}

void LayerPlayer::draw(Renderer& renderer, const LayerProps& props, float extraOpacity) {
    const float p = fadeProgress();
    float prevAlpha = 1.0f - p;
    float curAlpha = p;
    if (fadeStyle_ == FadeStyle::ThroughBlack) {
        prevAlpha = std::clamp(1.0f - 2.0f * p, 0.0f, 1.0f);
        curAlpha = std::clamp(2.0f * p - 1.0f, 0.0f, 1.0f);
    }
    if (previous_.hasFrame && prevAlpha > 0.0f) renderer.drawLayer(previous_.texture, props, prevAlpha * extraOpacity);
    if (current_.hasFrame && curAlpha > 0.0f) renderer.drawLayer(current_.texture, props, curAlpha * extraOpacity);
}

bool LayerPlayer::ended() const {
    return current_.decoder && current_.decoder->state() == VideoDecoder::State::Ended &&
           !current_.decoder->isStill();
}

double LayerPlayer::position() const {
    if (!current_.decoder) return 0.0;
    double t = current_.shownTime;
    const double d = current_.decoder->duration();
    if (d > 0.0) t = std::fmod(t, d);
    return std::isfinite(t) ? std::max(0.0, t) : 0.0;
}

std::string LayerPlayer::stateName() const {
    if (!current_.decoder) return "idle";
    switch (current_.decoder->state()) {
    case VideoDecoder::State::Opening: return "loading";
    case VideoDecoder::State::Playing: return current_.hasFrame ? "playing" : "loading";
    case VideoDecoder::State::Ended: return current_.decoder->isStill() ? "playing" : "ended";
    case VideoDecoder::State::Error: return "error";
    }
    return "idle";
}

std::string LayerPlayer::error() const {
    return current_.decoder ? current_.decoder->error() : std::string{};
}

} // namespace vthost
