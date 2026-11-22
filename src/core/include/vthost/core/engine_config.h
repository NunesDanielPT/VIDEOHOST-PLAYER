#pragma once
// Configuração lida pelo motor ao arrancar (antes de criar o dispositivo gráfico),
// escrita pelo Studio em %LOCALAPPDATA%\VTHOST\engine_config.json.
// Mudar a placa gráfica só tem efeito quando o motor volta a arrancar.

#include <filesystem>
#include <string>

namespace vthost {

struct EngineConfig {
    // Placa gráfica: "" = automático (a de alto desempenho, normalmente a dedicada);
    // senão o nome da placa tal como o DXGI a descreve (ex. "NVIDIA GeForce RTX 4060 Laptop GPU").
    std::string gpu;
    bool operator==(const EngineConfig&) const = default;
};

std::string engineConfigToJson(const EngineConfig& c);
EngineConfig engineConfigFromJson(const std::string& json); // tolerante: lixo -> omissões

EngineConfig loadEngineConfig(const std::filesystem::path& file);
bool saveEngineConfig(const std::filesystem::path& file, const EngineConfig& c, std::string* error = nullptr);

} // namespace vthost
