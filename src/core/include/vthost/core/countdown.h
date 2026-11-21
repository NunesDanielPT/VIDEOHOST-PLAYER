#pragma once
// Countdown do show: quanto falta para acabar a cue no ar (todas as repetições;
// em loop infinito, a passagem actual). Usado pelo Studio (tempos no preview,
// janela de countdown) e pela página web na rede local.

#include "vthost/core/project.h"
#include "vthost/core/protocol.h"

#include <string>

namespace vthost {

struct Countdown {
    bool active = false;    // há algo a contar
    bool running = false;   // a decorrer (false = em pausa / parado)
    bool loops = false;     // loop infinito (recomeça no fim)
    double remaining = 0.0; // segundos
    double duration = 0.0;  // total (0 = desconhecido)
    double elapsed = 0.0;
    int cueNumber = 0;      // 1.. (0 = nada)
    std::string cueLabel;   // ID visível (ex. "INTRO")
    std::string cueName;
    std::string nextName;   // cue que vem a seguir ("" = nenhuma)
    std::string mediaName;  // vídeo contado (nome do ficheiro, sem pasta)
    std::string mode;       // "cue" | "idle"
    std::string state;      // estado do motor ("playing", "paused", "ended", "loading"...)
};

Countdown computeCountdown(const Project& project, const protocol::EngineStatus& status);

// Avança `c` em `seconds` (interpolação entre estados do motor).
Countdown advanceCountdown(const Countdown& c, double seconds);

// JSON para a página web (inclui o nome do projecto).
std::string countdownJson(const Countdown& c, const std::string& projectName);

// "01:23" / "1:02:03" (arredonda para cima: 0.2 s por tocar mostra "00:01").
std::string formatCountdown(double seconds);

} // namespace vthost
