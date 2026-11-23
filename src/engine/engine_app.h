#pragma once

#include "audio_output.h"
#include "cue_player.h"
#include "dsm.h"
#include "ipc_server.h"
#include "ndi_output.h"
#include "output_window.h"
#include "renderer.h"
#include "vthost/core/heartbeat.h"
#include "vthost/core/project.h"
#include "vthost/core/protocol.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vthost {

// Códigos de saída do motor (o watchdog usa-os para decidir o que fazer).
enum EngineExit : int {
    kExitRequested = 0,     // shutdown pedido: não relançar
    kExitTooManyErrors = 3, // falhas seguidas no render loop
    kExitDeviceLost = 4,    // GPU/driver perdido: relançar recria o dispositivo
    kExitInitFailed = 5,
    kExitRestart = 6,       // reinício pedido (ex. outra placa gráfica): relançar já e retomar o show
    kExitAlreadyRunning = 10,
};

class EngineApp {
public:
    struct Options {
        std::string projectPath;
        bool recover = false;
    };

    ~EngineApp();
    int run(const Options& options);

private:
    using Clock = std::chrono::steady_clock;

    bool initHeartbeat();
    void beat();
    void setRunState(EngineRunState s);

    std::string handle(const std::string& line);
    bool loadProject(const std::string& path, std::string* error);
    bool play(int index, const CuePlayer::StartOptions& opts, std::string* error);
    void applySettings(const protocol::EngineSettings& s);
    void handleCueEnd();      // fim da cue: pausa, apaga, "ir para" ou a seguinte

    // Preview partilhado com o Studio
    bool initPreview();
    void writePreview(const std::uint8_t* data, int rowPitch, int width, int height);
    void syncCanvas();
    void syncOutputs();
    void recover();
    void renderFrame();
    void saveStateIfDue(bool force = false);
    protocol::EngineStatus buildStatus() const;
    void updateDsm();                 // texto dos tempos das saídas em modo DSM
    const Output* outputConfig(const std::string& id) const;

    Renderer renderer_;
    IpcServer ipc_;
    AudioOutput audio_;
    std::unique_ptr<CuePlayer> player_; // criado depois do renderer
    Project project_;
    std::string projectPath_;
    std::vector<std::unique_ptr<OutputWindow>> outputs_; // Display / Window
    std::vector<std::unique_ptr<NdiOutput>> ndi_;
    std::vector<Output> outputConfig_;                   // configuração aplicada
    std::vector<std::string> warnings_;
    bool showMode_ = false;
    protocol::EngineSettings settings_;
    bool testCard_ = false;
    Clock::time_point lastPreview_{};
    bool running_ = true;
    int exitCode_ = kExitRequested;
    bool recovered_ = false;

    HANDLE previewMapping_ = nullptr;
    std::uint8_t* previewBlock_ = nullptr;
    std::uint64_t previewFrames_ = 0;

    HANDLE heartbeatMapping_ = nullptr;
    HeartbeatBlock* heartbeat_ = nullptr;

    Clock::time_point startTime_ = Clock::now();
    Clock::time_point lastStateSave_{};
    Clock::time_point lastMonitorCheck_{};
    Clock::time_point fpsWindowStart_ = Clock::now();
    int fpsFrames_ = 0;
    double fps_ = 0.0;
    double renderMs_ = 0.0;
    int previousCue_ = -1; // cue que estava no ar quando a actual começou (regras dos blocos)
    void keepCursorOffOutputs();
    POINT lastCursor_{};
    bool haveCursor_ = false;
    DsmPanelRenderer dsmPanels_;
    std::map<std::string, DsmGeometry> ndiDsm_; // saídas NDI em modo DSM (1920x1080)
    Clock::time_point lastDsm_{}; // média do tempo de trabalho por frame (sem a espera pelo ecrã)
};

} // namespace vthost
