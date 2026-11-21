#pragma once
// Protocolo Studio <-> Motor: uma mensagem JSON por linha ("\n"), num named pipe
// local (\\.\pipe\vthost-engine). Cada pedido recebe exactamente uma resposta.
//
// Pedidos:
//   {"cmd":"ping"}
//   {"cmd":"load_project","path":"C:/shows/x.vthost"}   // a cue no ar continua (actualiza volume/fades/...)
//   {"cmd":"play","cue":2,"paused":false,"cut":false}  // começa a cue 2 (paused = pronta no 1.º frame)
//   {"cmd":"transport","action":"pause|resume|toggle|stop"}
//   {"cmd":"seek","time":12.5}          // dentro da cue actual (segundos desde o ponto In, a velocidade 1)
//   {"cmd":"seek_remaining","time":30}  // GOTO 30/20/10: fica a faltar `time` segundos
//   {"cmd":"set_settings","vsync":true,"previewFps":30,"priority":"above_normal","audioDevice":""}
//   {"cmd":"set_show_mode","on":true}
//   {"cmd":"set_test_card","on":true}
//   {"cmd":"get_status"}
//   {"cmd":"list_displays"}             // ecrãs ligados: placa gráfica, nome, resolução
//   {"cmd":"list_audio_devices"}        // placas de som (saídas)
//   {"cmd":"shutdown"}                  // pára o motor de propósito (sem recuperação)
//
// Respostas: {"ok":true, ...} ou {"ok":false,"error":"..."}

#include "vthost/core/project.h"

#include <optional>
#include <string>
#include <vector>

namespace vthost::protocol {

inline constexpr std::size_t kMaxLineBytes = 1024 * 1024;

enum class CommandType {
    Ping,
    LoadProject,
    Play,
    Transport,
    Seek,
    SeekRemaining,
    SetSettings,
    SetShowMode,
    SetTestCard,
    GetStatus,
    ListDisplays,
    ListAudioDevices,
    Shutdown
};
enum class TransportAction { Pause, Resume, Toggle, Stop };

// Preferências do motor (enviadas pelo Studio ao ligar e quando mudam).
struct EngineSettings {
    bool vsync = true;
    int previewFps = 30;                   // 0 = sem preview; 5..60
    std::string priority = "above_normal"; // "normal" | "above_normal" | "high"
    std::string audioDevice;               // id WASAPI ("" = placa de som por omissão)
    friend bool operator==(const EngineSettings&, const EngineSettings&) = default;
};

struct Command {
    CommandType type = CommandType::Ping;
    std::string path;
    int cue = -1;
    bool paused = false; // play: fica pronta no 1.º frame
    bool cut = false;    // play: sem crossfade nem fade in (SHIFT)
    bool on = false;
    double time = 0.0;
    TransportAction action = TransportAction::Toggle;
    EngineSettings settings;
};

struct ParsedCommand {
    std::optional<Command> command;
    std::string error;
};

ParsedCommand parseCommand(const std::string& line);
std::string serializeCommand(const Command& c);

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

struct AudioDeviceInfo {
    std::string id;   // id WASAPI
    std::string name; // ex. "Altifalantes (Realtek)"
    bool isDefault = false;
};

struct EngineStatus {
    std::string version;
    std::string projectName;
    std::string projectPath;
    bool showMode = false;
    double fps = 0.0;
    double uptimeSeconds = 0.0;
    bool recovered = false; // motor reiniciado pelo watchdog e show retomado
    bool testCard = false;

    // Cue no ar.
    int cue = -1;            // índice (-1 = nada)
    std::string cueId;
    std::string state = "stopped"; // "stopped" | "loading" | "playing" | "paused" | "ended" | "error"
    std::string error;
    double mediaTime = 0.0;  // posição no ficheiro (segundos) - cabeça de leitura da waveform
    double elapsed = 0.0;    // tempo real decorrido na passagem actual
    double remaining = 0.0;  // tempo real que falta (todas as passagens; infinito -> só a actual)
    double passDuration = 0.0; // tempo real de uma passagem
    int pass = 0;            // passagem actual (0 = primeira)
    bool infinite = false;   // loop infinito
    double fileDuration = 0.0;
    int nextCue = -1;        // o que vem a seguir (respeita "ir para")

    // Áudio.
    std::string audioDevice; // nome
    bool audioOk = false;
    std::string audioError;
    double levelL = 0.0;     // pico recente 0..1
    double levelR = 0.0;

    // Desempenho do motor (rodapé do Studio).
    double renderMs = 0.0;     // tempo médio para preparar um frame (sem esperar pelo ecrã)
    double vramUsedMb = 0.0;   // memória de vídeo usada pelo motor
    double vramBudgetMb = 0.0; // memória de vídeo que o Windows dá ao motor (0 = desconhecido)

    std::vector<OutputStatus> outputs;
    std::vector<std::string> warnings;
};

std::string okResponse();
std::string errorResponse(const std::string& message);
std::string statusResponse(const EngineStatus& s);
std::string displaysResponse(const std::vector<DisplayInfo>& displays);
std::string audioDevicesResponse(const std::vector<AudioDeviceInfo>& devices);

struct ParsedResponse {
    bool ok = false;
    std::string error;
    std::optional<EngineStatus> status;                      // respostas a get_status
    std::optional<std::vector<DisplayInfo>> displays;        // respostas a list_displays
    std::optional<std::vector<AudioDeviceInfo>> audioDevices; // respostas a list_audio_devices
};
ParsedResponse parseResponse(const std::string& line);

} // namespace vthost::protocol
