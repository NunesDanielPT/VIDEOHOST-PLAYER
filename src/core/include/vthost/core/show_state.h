#pragma once
// Fotografia do estado do show, gravada várias vezes por segundo pelo motor.
// Se o motor crashar, o watchdog relança-o com --recover e o motor retoma a
// partir daqui: mesmo projecto, mesma coluna, mesma media e posição em cada layer.

#include "vthost/core/project.h"

#include <string>
#include <vector>

namespace vthost {

struct LayerPlayback {
    std::string layerId;
    std::string mediaId;
    double positionSeconds = 0.0;
    LayerProps props; // propriedades ao vivo (podem diferir do ficheiro em Show Mode)
};

struct ShowState {
    std::string projectPath;
    bool showMode = false;
    int activeColumn = -1;
    std::string timelineId;  // timeline em reprodução/pausa ("" = modo dashboard)
    double timelineTime = 0.0;
    bool timelinePlaying = false;
    std::vector<LayerPlayback> layers;
};

std::string serializeShowState(const ShowState& s);
bool parseShowState(const std::string& json, ShowState& out);

bool saveShowState(const ShowState& s, std::string* error = nullptr);
bool loadShowState(ShowState& out);
void clearShowState(); // paragem intencional: nada a recuperar

} // namespace vthost
