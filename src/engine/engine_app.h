#pragma once

#include "ipc_server.h"
#include "layer_player.h"
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
    bool go(int column, std::string* error, double fadeOverride = -1.0, bool retrigger = false);
    void applySettings(const protocol::EngineSettings& s);
    void stopAll(double fade);

    // Timeline
    bool timelinePlay(const std::string& id, double time, std::string* error);
    void timelinePause();
    void timelineStop();
    bool timelineSeek(const std::string& id, double time, std::string* error);
    void leaveTimeline();
    double timelineTime() const;
    void updateTimeline();

    // Preview partilhado com o Studio
    bool initPreview();
    void writePreview(const std::uint8_t* data, int rowPitch, int width, int height);
    void syncLayers();
    void syncOutputs();
    void syncCanvases();
    void handleMediaEnds(); // modos de loop: once-stop / auto-follow
    void recover();
    void renderFrame();
    void saveStateIfDue(bool force = false);
    protocol::EngineStatus buildStatus() const;

    Renderer renderer_;
    IpcServer ipc_;
    Project project_;
    std::string projectPath_;
    std::vector<std::unique_ptr<OutputWindow>> outputs_; // Display / Window
    std::vector<std::unique_ptr<NdiOutput>> ndi_;
    std::vector<Output> outputConfig_;                   // configuração aplicada
    std::map<std::string, float> clipFade_;              // fades dos clips da timeline, por layer
    std::map<std::string, std::unique_ptr<LayerPlayer>> players_;
    std::map<std::string, LayerProps> layerProps_;
    std::vector<std::string> warnings_;
    bool showMode_ = false;
    protocol::EngineSettings settings_;
    bool testCard_ = false;
    std::string previewCanvas_; // "" = primeiro canvas
    Clock::time_point lastPreview_{};
    int activeColumn_ = -1;
    Clock::time_point columnStart_ = Clock::now(); // instante do último GO (follow por tempo)
    bool running_ = true;
    int exitCode_ = kExitRequested;
    bool recovered_ = false;

    std::string activeTimeline_; // "" = modo dashboard
    double tlTime_ = 0.0;        // tempo da timeline em tlAnchor_
    Clock::time_point tlAnchor_ = Clock::now();
    bool tlPlaying_ = false;
    double tlLastEval_ = 0.0;

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
};

} // namespace vthost
