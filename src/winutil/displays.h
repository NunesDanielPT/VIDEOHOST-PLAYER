#pragma once
// Deteção dos ecrãs ligados: placa gráfica (DXGI), nome do monitor/projetor
// (DisplayConfig), resolução e frequência. Ordem estável: principal = 0, os
// restantes da esquerda para a direita.

#include "vthost/core/protocol.h"

#include <windows.h>

#include <string>
#include <vector>

namespace vthost {

std::vector<protocol::DisplayInfo> enumerateDisplays();

// Procura um ecrã pelo id ("\\.\DISPLAY2"); se não existir e `allowIndexFallback`,
// usa o índice. Devolve false se não encontrar.
bool findDisplay(const std::vector<protocol::DisplayInfo>& displays, const std::string& id, int index,
                 bool allowIndexFallback, protocol::DisplayInfo& out);

std::string describeDisplay(const protocol::DisplayInfo& d); // "OMEN 27 · 2560×1440 @ 144 Hz"

} // namespace vthost
