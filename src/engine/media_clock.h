#pragma once
// Relógio de uma cue: tempo de media (segundos dentro do ficheiro, acumulado
// entre repetições) em função do relógio do sistema, com velocidade e pausa.
// A imagem e o som usam o mesmo relógio: ficam sempre sincronizados.

#include <algorithm>
#include <chrono>

namespace vthost {

struct MediaClock {
    using Clock = std::chrono::steady_clock;

    bool started = false; // só arranca quando o primeiro frame existe
    bool paused = false;
    double base = 0.0;    // tempo de media em `start`
    Clock::time_point start{};
    double frozen = 0.0;  // tempo de media durante a pausa (ou antes de arrancar)
    double speed = 1.0;

    double at(Clock::time_point t) const {
        if (!started || paused) return frozen;
        return base + std::chrono::duration<double>(t - start).count() * speed;
    }
    double now() const { return at(Clock::now()); }

    void begin(double mediaTime) {
        started = true;
        base = mediaTime;
        frozen = mediaTime;
        start = Clock::now();
    }
    void setPaused(bool p) {
        if (p == paused) return;
        if (p) {
            frozen = now();
        } else {
            base = frozen;
            start = Clock::now();
        }
        paused = p;
    }
    void setSpeed(double s) {
        s = std::clamp(s, 0.25, 4.0);
        if (s == speed) return;
        if (started && !paused) {
            base = now();
            start = Clock::now();
        }
        speed = s;
    }
};

} // namespace vthost
