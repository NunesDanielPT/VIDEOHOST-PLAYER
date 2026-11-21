#pragma once
// Protocolo Studio <-> Motor: uma mensagem JSON por linha ("\n"), num named pipe
// local (\\.\pipe\vthost-engine). Cada pedido recebe exactamente uma resposta.
//
// Pedidos:
//   {"cmd":"ping"}
//   {"cmd":"load_project","path":"C:/shows/x.vthost"}
//   {"cmd":"go","column":2}                 // dispara a coluna (cue) 2
//   {"cmd":"stop_all","fade":1.0}
//   {"cmd":"set_layer_opacity","layer":"L1","opacity":0.5}
//   {"cmd":"set_show_mode","on":true}
//   {"cmd":"get_status"}
//   {"cmd":"shutdown"}                     // pára o motor de propósito (sem recuperação)
//
// Respostas: {"ok":true, ...} ou {"ok":false,"error":"..."}

#include <optional>
#include <string>
#include <vector>

namespace vthost::protocol {

inline constexpr std::size_t kMaxLineBytes = 1024 * 1024;

enum class CommandType { Ping, LoadProject, Go, StopAll, SetLayerOpacity, SetShowMode, GetStatus, Shutdown };

struct Command {
    CommandType type = CommandType::Ping;
    std::string path;
    std::string layer;
    int column = -1;
    double value = 0.0; // opacity / fade
    bool on = false;
};

struct ParsedCommand {
    std::optional<Command> command;
    std::string error;
};

ParsedCommand parseCommand(const std::string& line);
std::string serializeCommand(const Command& c);

struct LayerStatus {
    std::string id;
    std::string mediaId;
    std::string state; // "idle" | "loading" | "playing" | "error"
    double position = 0.0;
    std::string error;
};

struct OutputStatus {
    std::string id;
    int monitor = 0;
    bool ok = false;
    std::string error;
};

struct EngineStatus {
    std::string version;
    std::string projectName;
    std::string projectPath;
    bool showMode = false;
    int activeColumn = -1;
    double fps = 0.0;
    double uptimeSeconds = 0.0;
    bool recovered = false; // motor reiniciado pelo watchdog e show retomado
    std::vector<LayerStatus> layers;
    std::vector<OutputStatus> outputs;
    std::vector<std::string> warnings;
};

std::string okResponse();
std::string errorResponse(const std::string& message);
std::string statusResponse(const EngineStatus& s);

struct ParsedResponse {
    bool ok = false;
    std::string error;
    std::optional<EngineStatus> status; // presente nas respostas a get_status
};
ParsedResponse parseResponse(const std::string& line);

} // namespace vthost::protocol
