#pragma once
// Fotografia do estado do show, gravada várias vezes por segundo pelo motor.
// Se o motor crashar, o watchdog relança-o com --recover e o motor retoma a
// partir daqui: mesmo projecto, mesma cue, mesma posição (e pausa).

#include <string>
#include <vector>

namespace vthost {

// Clip no ar numa layer do CUEPLAYER.
struct CompLayerPlayback {
    std::string comp;
    std::string layer;
    std::string column;
    double cueTime = 0.0;
};

struct ShowState {
    std::string projectPath;
    bool showMode = false;
    std::string cueId;       // cue no ar ("" = nada)
    double cueTime = 0.0;    // tempo de cue acumulado (posição no ficheiro + passagens já feitas)
    bool paused = false;
    bool testCard = false;
    std::vector<CompLayerPlayback> compLayers;
};

std::string serializeShowState(const ShowState& s);
bool parseShowState(const std::string& json, ShowState& out);

bool saveShowState(const ShowState& s, std::string* error = nullptr);
bool loadShowState(ShowState& out);
void clearShowState(); // paragem intencional: nada a recuperar

} // namespace vthost
