#include "cue_player.h"

#include "vthost/core/log.h"

#include <algorithm>
#include <cmath>

namespace vthost {
namespace {

constexpr double kStartTimeout = 4.0; // s à espera do 1.º frame antes de arrancar o relógio na mesma

double clamp01(double v) { return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 1.0; }

double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }

} // namespace

CuePlayer::~CuePlayer() {
    retire(prev_);
    retire(cur_);
}

void CuePlayer::openDecoders(Slot& s, double startTime) {
    s.startTime = startTime;
    s.video = std::make_unique<VideoDecoder>(s.cue.path, s.range, startTime);
    if (s.audioHandle) audio_.removeSource(s.audioHandle);
    s.audio = std::make_shared<AudioDecoder>(s.cue.path, s.range, startTime, s.cue.speed);
    s.audioHandle = audio_.addSource(s.audio);
    s.clock = MediaClock{};
    s.clock.speed = s.cue.speed;
    s.clock.frozen = startTime;
}

void CuePlayer::retire(Slot& s) {
    if (s.video) VideoDecoder::retireAsync(std::move(s.video));
    if (s.audioHandle) audio_.removeSource(s.audioHandle);
    s = Slot{};
}

void CuePlayer::start(const Cue& cue, int index, const StartOptions& opts) {
    retire(prev_); // um crossfade de cada vez
    const auto now = Clock::now();
    const bool crossfade = !opts.cut && cue.crossfade > 0.0 && cur_.active && (cur_.hasFrame || cur_.clock.started);
    if (cur_.active) {
        if (crossfade) {
            prev_ = std::move(cur_);
            prev_.trans = Trans::Out;
            prev_.transStart = now;
            prev_.transSeconds = cue.crossfade;
        } else {
            retire(cur_);
        }
    }
    cur_ = Slot{};
    cur_.active = true;
    cur_.cue = cue;
    cur_.index = index;
    cur_.range.in = cue.inPoint;
    cur_.range.out = cue.outPoint;
    cur_.range.passes = cue.playCount;
    cur_.fadeInEnabled = !opts.cut;
    cur_.pendingPause = opts.paused;
    cur_.startedAt = now;
    if (crossfade) {
        cur_.trans = Trans::In;
        cur_.transStart = now;
        cur_.transSeconds = cue.crossfade;
    }
    const double startTime = opts.startTime >= 0.0 ? opts.startTime : cue.inPoint;
    openDecoders(cur_, std::max(cue.inPoint, startTime));
    log::info("cue " + std::to_string(index + 1) + " (" + cue.displayName() + ")" + (opts.paused ? " pronta" : " a tocar"));
}

void CuePlayer::stop(double fadeSeconds) {
    if (!cur_.active) return;
    retire(prev_);
    if (fadeSeconds > 0.0 && (cur_.hasFrame || cur_.clock.started)) {
        prev_ = std::move(cur_);
        prev_.trans = Trans::Out;
        prev_.transStart = Clock::now();
        prev_.transSeconds = fadeSeconds;
    } else {
        retire(cur_);
    }
    cur_ = Slot{};
}

void CuePlayer::setPaused(bool paused) {
    if (!cur_.active) return;
    if (!cur_.clock.started) {
        cur_.pendingPause = paused;
        return;
    }
    if (!paused) cur_.holding = false;
    cur_.clock.setPaused(paused);
}

void CuePlayer::holdAtEnd() {
    if (!cur_.active) return;
    cur_.holding = true;
    if (cur_.clock.started) cur_.clock.setPaused(true);
}

void CuePlayer::seekCueTime(double cueTime) {
    if (!cur_.active || !std::isfinite(cueTime)) return;
    const double end = endTime(cur_);
    double t = std::max(cur_.range.in, cueTime);
    if (end > 0.0) t = std::min(t, std::max(cur_.range.in, end - 0.04));
    const bool wasPaused = cur_.clock.started ? cur_.clock.paused : cur_.pendingPause;
    // A textura fica: o frame actual mantém-se até o descodificador novo ter o primeiro.
    if (cur_.video) VideoDecoder::retireAsync(std::move(cur_.video));
    openDecoders(cur_, t);
    cur_.pendingPause = wasPaused; // um salto não muda a pausa
    cur_.holding = false;
    cur_.endHandled = false;
}

void CuePlayer::seekInPass(double secondsFromIn) {
    if (!cur_.active) return;
    const double len = passLength(cur_);
    const double t = cur_.clock.now();
    int pass = 0;
    if (len > 0.0 && t > cur_.range.in) pass = static_cast<int>(std::floor((t - cur_.range.in) / len));
    if (cur_.range.passes > 0) pass = std::clamp(pass, 0, cur_.range.passes - 1);
    double off = std::max(0.0, secondsFromIn);
    if (len > 0.0) off = std::min(off, len - 0.04);
    seekCueTime(cur_.range.in + pass * len + off);
}

void CuePlayer::seekRemaining(double seconds) {
    if (!cur_.active) return;
    const double len = passLength(cur_);
    if (len <= 0.0) return;
    const double speed = std::max(0.01, cur_.cue.speed);
    double end = endTime(cur_);
    if (end <= 0.0) {
        // Loop infinito: fim da passagem actual.
        const double t = cur_.clock.now();
        const double pass = std::floor(std::max(0.0, t - cur_.range.in) / len);
        end = cur_.range.in + (pass + 1.0) * len;
    }
    seekCueTime(end - seconds * speed);
}

void CuePlayer::updateCue(const Cue& cue, int index) {
    if (cur_.active && cur_.cue.id == cue.id) {
        // Só parâmetros "suaves": o que já está a tocar não é interrompido.
        Cue& c = cur_.cue;
        c.label = cue.label;
        c.name = cue.name;
        c.color = cue.color;
        c.notes = cue.notes;
        c.pauseAtEnd = cue.pauseAtEnd;
        c.clearAtEnd = cue.clearAtEnd;
        c.pauseAtStart = cue.pauseAtStart;
        c.fadeIn = cue.fadeIn;
        c.fadeOut = cue.fadeOut;
        c.crossfade = cue.crossfade;
        c.gotoCue = cue.gotoCue;
        c.muted = cue.muted;
        c.volumeDb = cue.volumeDb;
        c.normalize = cue.normalize;
        c.loudness = cue.loudness;
        c.peakDb = cue.peakDb;
        c.fit = cue.fit;
        c.scale = cue.scale;
        c.posX = cue.posX;
        c.posY = cue.posY;
        cur_.index = index;

        // In/Out, repetições e velocidade mudados com a cue no ar: aplica já, a partir
        // do ponto onde está (antes só contava na próxima vez que a cue tocasse).
        if (c.inPoint != cue.inPoint || c.outPoint != cue.outPoint || c.playCount != cue.playCount || c.speed != cue.speed) {
            const double fileDur = cur_.video ? std::max(0.0, cur_.video->duration()) : 0.0;
            int pass = 0;
            double local = 0.0;
            cur_.range.split(cur_.clock.now(), fileDur, pass, local);
            c.inPoint = cue.inPoint;
            c.outPoint = cue.outPoint;
            c.playCount = cue.playCount;
            c.speed = cue.speed;
            cur_.range.in = c.inPoint;
            cur_.range.out = c.outPoint;
            cur_.range.passes = c.playCount;
            const double len = passLength(cur_);
            if (cur_.range.passes > 0) pass = std::min(pass, cur_.range.passes - 1);
            if (len > 0.0) {
                // Mesma passagem; posição no ficheiro dentro do novo In..Out (se ficou depois
                // do novo Out, salta para o fim dessa passagem: segue para a próxima / acaba).
                const double off = std::clamp(local - cur_.range.in, 0.0, std::max(0.0, len - 0.04));
                seekCueTime(cur_.range.in + pass * len + off);
            } else {
                seekCueTime(cur_.range.in);
            }
        }
    }
    if (prev_.active && prev_.cue.id == cue.id) prev_.index = index;
}

double CuePlayer::passLength(const Slot& s) const {
    if (!s.video) return 0.0;
    if (s.video->isStill()) {
        // Imagem fixa: o ponto Out dá a duração; sem Out fica no ecrã até ao próximo GO.
        return s.cue.outPoint > s.cue.inPoint ? s.cue.outPoint - s.cue.inPoint : 0.0;
    }
    if (!s.video->opened()) return 0.0;
    const double d = s.video->duration();
    // Duração minúscula = provavelmente imagem que ainda não se sabe que é fixa.
    if (s.video->state() != VideoDecoder::State::NoVideo && d > 0.0 && d < 0.2) return 0.0;
    return s.range.passLength(d);
}

double CuePlayer::endTime(const Slot& s) const {
    const double len = passLength(s);
    if (len <= 0.0 || s.range.passes <= 0) return 0.0;
    return s.range.in + s.range.passes * len;
}

float CuePlayer::opacity(const Slot& s) const {
    if (!s.active) return 0.0f;
    double a = 1.0;
    const auto now = Clock::now();
    if (s.trans != Trans::None) {
        const double p = s.transSeconds > 0.0
                             ? std::chrono::duration<double>(now - s.transStart).count() / s.transSeconds
                             : 1.0;
        a *= s.trans == Trans::In ? clamp01(p) : 1.0 - clamp01(p);
    }
    // (Antes de o relógio arrancar - ex. a meio de um salto - usa o tempo congelado.)
    const double speed = std::max(0.01, s.cue.speed);
    const double t = s.clock.now();
    if (s.fadeInEnabled && s.cue.fadeIn > 0.0) a *= clamp01((t - s.range.in) / speed / s.cue.fadeIn);
    if (s.cue.fadeOut > 0.0) {
        const double end = endTime(s);
        if (end > 0.0) a *= clamp01((end - t) / speed / s.cue.fadeOut);
    }
    return static_cast<float>(a);
}

void CuePlayer::updateSlot(Slot& s, const Project& project) {
    if (!s.active) return;
    const auto now = Clock::now();
    if (s.video && !s.clock.started) {
        const auto vs = s.video->state();
        bool begin = false;
        double at = s.startTime;
        if (auto first = s.video->takeOldest()) {
            // O relógio arranca no 1.º frame: abrir um ficheiro lento não faz o vídeo começar "a meio".
            if (renderer_.upload(s.tex, *first)) s.hasFrame = true;
            at = first->time;
            s.video->recycle(std::move(first));
            begin = true;
        } else if (vs == VideoDecoder::State::NoVideo) {
            // Só áudio: arranca quando o som estiver pronto (ou se não houver som).
            begin = !s.audio || s.audio->hasData() || s.audio->finished();
        } else if (vs == VideoDecoder::State::Error || vs == VideoDecoder::State::Ended) {
            begin = true;
        }
        if (!begin && std::chrono::duration<double>(now - s.startedAt).count() > kStartTimeout) begin = true;
        if (begin) {
            s.clock.begin(at);
            if (s.pendingPause || s.holding) s.clock.setPaused(true);
            s.pendingPause = false;
        }
    } else if (s.video && s.clock.started) {
        if (auto frame = s.video->takeFrameFor(s.clock.now())) {
            if (renderer_.upload(s.tex, *frame)) s.hasFrame = true;
            s.video->recycle(std::move(frame));
        }
    }
    // Som: mesmo relógio, ganho = volume + normalização + master, vezes os fades.
    if (s.audioHandle) {
        double gain = 0.0;
        if (!s.cue.muted && s.clock.started)
            gain = dbToLinear(s.cue.gainDb(project.normalizeTargetLufs) + project.masterVolumeDb) * opacity(s);
        audio_.updateSource(s.audioHandle, s.clock, static_cast<float>(gain));
    }
}

void CuePlayer::update(const Project& project) {
    updateSlot(cur_, project);
    if (prev_.active) {
        const double p = prev_.transSeconds > 0.0
                             ? std::chrono::duration<double>(Clock::now() - prev_.transStart).count() / prev_.transSeconds
                             : 1.0;
        if (p >= 1.0) retire(prev_);
        else updateSlot(prev_, project);
    }
}

void CuePlayer::draw() {
    for (const Slot* s : {&prev_, &cur_}) {
        if (!s->active || !s->hasFrame) continue;
        const float a = opacity(*s);
        if (a <= 0.0f) continue;
        LayerProps p;
        p.fit = s->cue.fit;
        p.scale = s->cue.scale;
        p.posX = s->cue.posX;
        p.posY = s->cue.posY;
        renderer_.drawLayer(s->tex, p, a);
    }
}

bool CuePlayer::reachedEnd() const {
    if (!cur_.active || !cur_.clock.started || cur_.holding) return false;
    if (!cur_.video) return true;
    const auto vs = cur_.video->state();
    if (vs == VideoDecoder::State::Error) return secondsSinceStart() > 1.0; // ficheiro estragado: segue o show
    const double end = endTime(cur_);
    if (end > 0.0 && cur_.clock.now() >= end) return true;
    if (cur_.video->isStill()) return false; // imagem sem duração: fica até ao próximo GO
    if (vs == VideoDecoder::State::NoVideo) return cur_.range.passes > 0 && (!cur_.audio || cur_.audio->finished());
    return cur_.range.passes > 0 && cur_.video->drained();
}

double CuePlayer::secondsSinceStart() const {
    if (!cur_.active) return 0.0;
    return std::chrono::duration<double>(Clock::now() - cur_.startedAt).count();
}

double CuePlayer::cueTime() const { return cur_.active ? cur_.clock.now() : 0.0; }

void CuePlayer::fillStatus(protocol::EngineStatus& s) const {
    if (!cur_.active) {
        s.state = "stopped";
        return;
    }
    s.cue = cur_.index;
    s.cueId = cur_.cue.id;
    const double speed = std::max(0.01, cur_.cue.speed);
    const double t = cur_.clock.now();
    const double len = passLength(cur_);
    const double in = cur_.range.in;
    s.fileDuration = cur_.video ? std::max(0.0, cur_.video->duration()) : 0.0;
    s.infinite = cur_.range.passes <= 0;
    if (len > 0.0) {
        int pass = static_cast<int>(std::floor(std::max(0.0, t - in) / len));
        if (cur_.range.passes > 0) pass = std::clamp(pass, 0, cur_.range.passes - 1);
        const double local = std::clamp(t - pass * len, in, in + len);
        s.pass = pass;
        s.mediaTime = local;
        s.elapsed = (local - in) / speed;
        s.passDuration = len / speed;
        const double end = endTime(cur_);
        s.remaining = end > 0.0 ? std::max(0.0, (end - t) / speed) : std::max(0.0, (in + len - local) / speed);
    } else {
        s.mediaTime = t;
        s.elapsed = std::max(0.0, t - in) / speed;
    }
    const auto vs = cur_.video ? cur_.video->state() : VideoDecoder::State::Error;
    const bool audioBad = !cur_.audio || cur_.audio->state() == AudioDecoder::State::Error ||
                          cur_.audio->state() == AudioDecoder::State::NoAudio;
    if (vs == VideoDecoder::State::Error && audioBad) {
        s.state = "error";
        s.error = cur_.video ? cur_.video->error() : "sem descodificador";
    } else if (!cur_.clock.started) {
        s.state = "loading";
    } else if (cur_.holding) {
        s.state = "ended";
    } else if (cur_.clock.paused) {
        s.state = "paused";
    } else {
        s.state = "playing";
    }
    if (s.state != "error" && vs == VideoDecoder::State::Error) s.error = cur_.video->error();
}

} // namespace vthost
