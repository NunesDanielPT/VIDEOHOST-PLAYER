#pragma once
// Countdown do show: quanto falta para acabar o que está no ar.
//
// Dashboard: a cue activa. Se a cue tiver avanço por tempo, conta o tempo até à
// cue seguinte; senão conta o vídeo com mais tempo por tocar entre as layers da cue.
// Timeline: tempo até ao fim do último clip.
//
// Usado pelo Studio (ecrã grande) e pela página web na rede local.

#include "vthost/core/project.h"
#include "vthost/core/protocol.h"

#include <string>

namespace vthost {

struct Countdown {
    bool active = false;    // há algo a contar
    bool running = false;   // a decorrer (false = em pausa / parado)
    bool loops = false;     // o vídeo contado está em loop (recomeça no fim)
    double remaining = 0.0; // segundos
    double duration = 0.0;  // total (0 = desconhecido)
    double elapsed = 0.0;
    int cueNumber = 0;      // 1.. (0 = timeline / nada)
    std::string cueName;
    std::string nextName;   // cue que vem a seguir ("" = nenhuma)
    std::string mediaName;  // vídeo contado (nome do ficheiro, sem pasta)
    std::string mode;       // "cue" | "timer" | "timeline" | "idle"
};

Countdown computeCountdown(const Project& project, const protocol::EngineStatus& status, bool loopBoard);

// Avança `c` em `seconds` (interpolação entre estados do motor).
Countdown advanceCountdown(const Countdown& c, double seconds);

// JSON para a página web (inclui o nome do projecto).
std::string countdownJson(const Countdown& c, const std::string& projectName);

// "01:23" / "1:02:03" (arredonda para cima: 0.2 s por tocar mostra "00:01").
std::string formatCountdown(double seconds);

} // namespace vthost
