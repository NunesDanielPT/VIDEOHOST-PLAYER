#pragma once

#include "audio_output.h"
#include "cue_player.h"
#include "ipc_server.h"
#include "ndi_output.h"
#include "output_window.h"
#include "renderer.h"
#include "vthost/core/heartbeat.h"
#include "vthost/core/project.h"
#include "vthost/core/protocol.h"

#include <chrono>
#include <map>
#include <set>
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
    bool play(int index, const CuePlayer::StartOptions& opts, std::string* error);
    void applySettings(const protocol::EngineSettings& s);
    void handleCueEnd();      // fim da cue: pausa, apaga, "ir para" ou a seguinte

    // CUEPLAYER (composições)
    struct CompPlayback {
        std::map<std::string, std::unique_ptr<CuePlayer>> players; // layerId -> player
        std::map<std::string, std::string> column;                 // layerId -> coluna no ar
    };
    void syncCompositions();
    void refreshCompCues(const std::string& compId); // volume/fades/transição ao vivo nos clips no ar
    bool launchClip(const std::string& comp, const std::string& layer, const std::string& column,
                    const CuePlayer::StartOptions& opts, std::string* error);
    bool launchColumn(const std::string& comp, const std::string& column, std::string* error);
    void clearLayer(const std::string& comp, const std::string& layer);
    void handleCompEnds();
    void drawCompositions();
    bool previewClip(const std::string& comp, const std::string& layer, const std::string& column, std::string* error);

    // Preview partilhado com o Studio (slot 0 = principal, 1 = monitor Preview do CUEPLAYER)
    bool initPreview();
    void writePreview(int slot, const std::string& canvasId, const std::uint8_t* data, int rowPitch, int width, int height);
    void syncCanvas();
    void syncOutputs();
    void rebuildSlices(); // geometria dos slices de cada saída (fontes, malhas)
    std::string sliceCanvas(const Slice& s, int& w, int& h) const; // canvas da fonte ("" = inválida)
    void recover();
    void renderFrame();
    void saveStateIfDue(bool force = false);
    protocol::EngineStatus buildStatus() const;

    Renderer renderer_;
    IpcServer ipc_;
    AudioOutput audio_;
    std::unique_ptr<CuePlayer> player_; // MEDIAPLAYER (criado depois do renderer)
    std::map<std::string, CompPlayback> comps_;
    std::unique_ptr<CuePlayer> previewPlayer_; // monitor Preview (sem som)
    std::string previewSource_ = "media";      // "media" | "comp:<id>"
    std::set<std::string> layerCanvases_;      // "layer:<comp>:<layer>" usados por slices
    std::map<std::string, std::vector<SliceDraw>> ndiSlices_; // saídas NDI
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

    HANDLE previewMapping_[2] = {nullptr, nullptr};
    std::uint8_t* previewBlock_[2] = {nullptr, nullptr};
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
