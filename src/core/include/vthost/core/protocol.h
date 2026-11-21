#pragma once
// Protocolo Studio <-> Motor: uma mensagem JSON por linha ("\n"), num named pipe
// local (\\.\pipe\vthost-engine). Cada pedido recebe exactamente uma resposta.
//
// Pedidos:
//   {"cmd":"ping"}
//   {"cmd":"load_project","path":"C:/shows/x.vthost"}
//   {"cmd":"go","column":2}                 // dispara a coluna (cue) 2
//   {"cmd":"stop_all","fade":1.0}
//   {"cmd":"set_layer_props","layer":"L1","props":{"opacity":0.5,"blend":"add","x":0,"y":0,"scale":1,"rotation":0}}
//   {"cmd":"set_layer_visible","layer":"L1","on":false}   // olho da layer, ao vivo
//   {"cmd":"timeline","action":"play|pause|stop|seek","timeline":"T1","time":12.5}
//   {"cmd":"set_show_mode","on":true}
//   {"cmd":"get_status"}
//   {"cmd":"list_displays"}                // ecrãs ligados: placa gráfica, nome, resolução
//   {"cmd":"shutdown"}                     // pára o motor de propósito (sem recuperação)
//
// Respostas: {"ok":true, ...} ou {"ok":false,"error":"..."}

#include "vthost/core/project.h"

#include <optional>
#include <string>
#include <vector>

namespace vthost::protocol {

inline constexpr std::size_t kMaxLineBytes = 1024 * 1024;

enum class CommandType { Ping, LoadProject, Go, StopAll, SetLayerProps, SetLayerVisible, Timeline, SetShowMode, GetStatus, ListDisplays, Shutdown };
enum class TimelineAction { Play, Pause, Stop, Seek };

struct Command {
    CommandType type = CommandType::Ping;
    std::string path;
    std::string layer;
    int column = -1;
    double value = 0.0; // fade (stop_all) / tempo (timeline; -1 = posição actual)
    bool on = false;
    LayerProps props;
    std::string timeline;
    TimelineAction action = TimelineAction::Play;
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
    std::string type;   // "display" | "window" | "ndi" | "decklink"
    std::string detail; // ex. "OMEN 27 · 2560×1440 @ 144 Hz" ou "NDI: VTHOST - Output 1"
};

// Ecrã detectado no Windows.
struct DisplayInfo {
    std::string id;      // nome de dispositivo, ex. "\\.\DISPLAY2"
    std::string name;    // nome do monitor/projetor, ex. "OMEN 27"
    std::string adapter; // placa gráfica, ex. "NVIDIA GeForce RTX 5060 Ti"
    int width = 0;
    int height = 0;
    double refreshHz = 0.0;
    int x = 0;
    int y = 0;
    bool primary = false;
    int index = 0;       // ordem usada pelo VTHOST (0 = principal)
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
    std::string timelineId; // "" = modo dashboard
    double timelineTime = 0.0;
    bool timelinePlaying = false;
    std::vector<LayerStatus> layers;
    std::vector<OutputStatus> outputs;
    std::vector<std::string> warnings;
};

std::string okResponse();
std::string errorResponse(const std::string& message);
std::string statusResponse(const EngineStatus& s);
std::string displaysResponse(const std::vector<DisplayInfo>& displays);

struct ParsedResponse {
    bool ok = false;
    std::string error;
    std::optional<EngineStatus> status; // presente nas respostas a get_status
    std::optional<std::vector<DisplayInfo>> displays; // presente nas respostas a list_displays
};
ParsedResponse parseResponse(const std::string& line);

} // namespace vthost::protocol
