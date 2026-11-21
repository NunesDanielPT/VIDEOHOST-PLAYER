#include "engine_app.h"

#include "displays.h"
#include "vthost/core/log.h"
#include "vthost/core/paths.h"
#include "vthost/core/preview.h"
#include "vthost/core/show_state.h"

#include <windows.h>

#include <timeapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <set>

namespace vthost {

using namespace std::chrono_literals;

namespace {
const std::string kCanvasId = "C1"; // o player tem um só canvas (resolução do projecto)
} // namespace

EngineApp::~EngineApp() {
    player_.reset();
    outputs_.clear();
    if (previewBlock_) UnmapViewOfFile(previewBlock_);
    if (previewMapping_) CloseHandle(previewMapping_);
    if (heartbeat_) UnmapViewOfFile(heartbeat_);
    if (heartbeatMapping_) CloseHandle(heartbeatMapping_);
}

// ---------------------------------------------------------------- heartbeat

bool EngineApp::initHeartbeat() {
    // O watchdog cria o bloco; se o motor correr sozinho (debug), cria-o ele.
    heartbeatMapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, paths::kHeartbeatName);
    if (!heartbeatMapping_)
        heartbeatMapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                               sizeof(HeartbeatBlock), paths::kHeartbeatName);
    if (!heartbeatMapping_) return false;
    heartbeat_ = static_cast<HeartbeatBlock*>(
        MapViewOfFile(heartbeatMapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(HeartbeatBlock)));
    if (!heartbeat_) return false;
    heartbeat_->magic = kHeartbeatMagic;
    heartbeat_->enginePid = GetCurrentProcessId();
    setRunState(EngineRunState::Starting);
    return true;
}

void EngineApp::beat() {
    if (heartbeat_) InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&heartbeat_->counter));
}

void EngineApp::setRunState(EngineRunState s) {
    if (heartbeat_)
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&heartbeat_->state), static_cast<LONG>(s));
}

// ---------------------------------------------------------------- projecto

bool EngineApp::loadProject(const std::string& path, std::string* error) {
    auto r = loadProjectFile(path);
    if (!r.project) {
        if (error) *error = r.error;
        log::error("load_project falhou: " + r.error);
        return false;
    }
    project_ = std::move(*r.project);
    projectPath_ = path;
    warnings_ = r.warnings;
    for (const auto& w : warnings_) log::warn("projecto: " + w);
    log::info("projecto carregado: " + project_.name + " (" + path + ", " + std::to_string(project_.cues.size()) + " cues)");

    syncCanvas();
    syncOutputs();
    // A cue no ar continua; recebe os parâmetros novos (volume, fades...) e o índice actual.
    if (player_ && player_->active()) {
        const int idx = project_.cueIndex(player_->cueId());
        if (idx >= 0) player_->updateCue(project_.cues[static_cast<std::size_t>(idx)], idx);
        else player_->updateCue(player_->cue(), -1); // cue apagada: acaba e pára
    }
    return true;
}

void EngineApp::syncCanvas() {
    renderer_.ensureCanvas(kCanvasId, project_.width, project_.height);
    renderer_.keepOnlyCanvases({kCanvasId});
    for (auto& w : outputs_) w->setCanvas(kCanvasId, project_.width, project_.height);
}

namespace {
bool sameOutput(const Output& a, const Output& b) {
    return a.id == b.id && a.name == b.name && a.type == b.type && a.enabled == b.enabled &&
           a.displayId == b.displayId && a.monitor == b.monitor && a.windowWidth == b.windowWidth &&
           a.windowHeight == b.windowHeight && a.ndiName == b.ndiName && a.ndiAlpha == b.ndiAlpha &&
           a.deviceId == b.deviceId;
}
} // namespace

void EngineApp::syncOutputs() {
    bool same = outputConfig_.size() == project_.outputs.size();
    for (std::size_t i = 0; same && i < outputConfig_.size(); ++i) same = sameOutput(outputConfig_[i], project_.outputs[i]);
    if (same) return;

    // Só recria as saídas que mudaram: as outras continuam sem piscar.
    std::vector<std::unique_ptr<OutputWindow>> oldWindows = std::move(outputs_);
    std::vector<std::unique_ptr<NdiOutput>> oldNdi = std::move(ndi_);
    outputs_.clear();
    ndi_.clear();
    for (const auto& o : project_.outputs) {
        if (!o.enabled) continue;
        if (o.type == OutputType::Display || o.type == OutputType::Window) {
            auto it = std::find_if(oldWindows.begin(), oldWindows.end(),
                                   [&](const auto& w) { return w && sameOutput(w->config(), o); });
            if (it != oldWindows.end()) {
                outputs_.push_back(std::move(*it));
                continue;
            }
            auto w = std::make_unique<OutputWindow>(renderer_, o);
            if (!w->create()) log::error("output " + o.id + ": " + w->error());
            w->setCanvas(kCanvasId, project_.width, project_.height);
            outputs_.push_back(std::move(w));
        } else if (o.type == OutputType::Ndi) {
            const std::string name = o.ndiName.empty() ? "VTHOST - " + o.name : o.ndiName;
            auto it = std::find_if(oldNdi.begin(), oldNdi.end(), [&](const auto& n) {
                return n && n->id() == o.id && n->detail() == "NDI: " + name + (o.ndiAlpha ? " (alpha)" : "");
            });
            if (it != oldNdi.end()) {
                ndi_.push_back(std::move(*it));
                continue;
            }
            ndi_.push_back(std::make_unique<NdiOutput>(o.id, name, o.ndiAlpha));
        }
        // DeckLink: reportado no estado (em desenvolvimento), sem objeto.
    }
    outputConfig_ = project_.outputs;
}

// ---------------------------------------------------------------- playlist

bool EngineApp::play(int index, const CuePlayer::StartOptions& opts, std::string* error) {
    if (index < 0 || index >= static_cast<int>(project_.cues.size())) {
        if (error) *error = "cue inexistente";
        return false;
    }
    player_->start(project_.cues[static_cast<std::size_t>(index)], index, opts);
    saveStateIfDue(true);
    return true;
}

void EngineApp::handleCueEnd() {
    if (!player_->active() || player_->endHandled() || !player_->reachedEnd()) return;
    // Proteção: um ficheiro que "acaba" logo não pode disparar a seguinte a cada frame.
    if (player_->secondsSinceStart() < 0.25) return;
    player_->setEndHandled();
    const Cue cue = player_->cue();
    const int idx = player_->index();
    if (cue.pauseAtEnd) {
        player_->holdAtEnd();
        log::info("fim da cue " + std::to_string(idx + 1) + ": pausa no último frame");
    } else if (cue.clearAtEnd || idx < 0) {
        player_->stop(0.0);
        log::info("fim da cue " + std::to_string(idx + 1) + ": apagar");
    } else {
        const int next = project_.nextCue(idx);
        if (next < 0) {
            player_->stop(0.0); // fim da playlist
            log::info("fim da playlist");
        } else {
            CuePlayer::StartOptions o;
            o.paused = project_.cues[static_cast<std::size_t>(next)].pauseAtStart;
            std::string err;
            play(next, o, &err);
        }
    }
    saveStateIfDue(true);
}

void EngineApp::recover() {
    ShowState st;
    if (!loadShowState(st) || st.projectPath.empty()) {
        log::warn("recuperação: sem estado guardado");
        return;
    }
    std::string err;
    if (!loadProject(st.projectPath, &err)) {
        log::error("recuperação: não foi possível reabrir o projecto: " + err);
        return;
    }
    showMode_ = st.showMode;
    testCard_ = st.testCard;
    const int idx = project_.cueIndex(st.cueId);
    if (idx >= 0) {
        CuePlayer::StartOptions o;
        o.startTime = st.cueTime;
        o.paused = st.paused;
        o.cut = true;
        play(idx, o, &err);
    }
    recovered_ = true;
    log::warn("RECUPERAÇÃO: show retomado após reinício do motor");
}

void EngineApp::saveStateIfDue(bool force) {
    const auto now = Clock::now();
    if (!force && now - lastStateSave_ < 500ms) return;
    lastStateSave_ = now;
    if (projectPath_.empty()) return;
    ShowState st;
    st.projectPath = projectPath_;
    st.showMode = showMode_;
    st.testCard = testCard_;
    if (player_ && player_->active()) {
        st.cueId = player_->cueId();
        st.cueTime = player_->cueTime();
        st.paused = player_->paused() || player_->holding();
    }
    std::string err;
    if (!saveShowState(st, &err)) {
        static auto lastWarn = Clock::time_point{};
        if (now - lastWarn > 30s) {
            log::warn("não foi possível gravar o estado do show: " + err);
            lastWarn = now;
        }
    }
}

// ---------------------------------------------------------------- comandos

std::string EngineApp::handle(const std::string& line) {
    using namespace protocol;
    auto parsed = parseCommand(line);
    if (!parsed.command) return errorResponse(parsed.error);
    const Command& c = *parsed.command;
    std::string err;
    switch (c.type) {
    case CommandType::Ping:
        return okResponse();
    case CommandType::LoadProject:
        return loadProject(c.path, &err) ? okResponse() : errorResponse(err);
    case CommandType::Play: {
        CuePlayer::StartOptions o;
        o.paused = c.paused;
        o.cut = c.cut;
        return play(c.cue, o, &err) ? okResponse() : errorResponse(err);
    }
    case CommandType::Transport:
        if (c.action == TransportAction::Stop) {
            player_->stop(0.0);
            log::info("stop");
            saveStateIfDue(true);
            return okResponse();
        }
        if (!player_->active()) return errorResponse("nada no ar");
        if (player_->holding() && c.action != TransportAction::Pause) {
            // Parada no fim: "play" continua para a cue seguinte.
            const int next = project_.nextCue(player_->index());
            if (next < 0) return errorResponse("fim da playlist");
            CuePlayer::StartOptions o;
            return play(next, o, &err) ? okResponse() : errorResponse(err);
        }
        if (c.action == TransportAction::Pause) player_->setPaused(true);
        else if (c.action == TransportAction::Resume) player_->setPaused(false);
        else player_->setPaused(!player_->paused());
        saveStateIfDue(true);
        return okResponse();
    case CommandType::Seek:
        if (!player_->active()) return errorResponse("nada no ar");
        player_->seekInPass(c.time);
        return okResponse();
    case CommandType::SeekRemaining:
        if (!player_->active()) return errorResponse("nada no ar");
        player_->seekRemaining(c.time);
        return okResponse();
    case CommandType::SetSettings:
        applySettings(c.settings);
        return okResponse();
    case CommandType::SetShowMode:
        showMode_ = c.on;
        log::info(showMode_ ? "Show Mode ligado" : "Show Mode desligado");
        saveStateIfDue(true);
        return okResponse();
    case CommandType::SetTestCard:
        testCard_ = c.on;
        log::info(testCard_ ? "test card ligado" : "test card desligado");
        return okResponse();
    case CommandType::GetStatus:
        return statusResponse(buildStatus());
    case CommandType::ListDisplays:
        return displaysResponse(enumerateDisplays());
    case CommandType::ListAudioDevices:
        return audioDevicesResponse(AudioOutput::listDevices());
    case CommandType::Shutdown:
        if (showMode_) return errorResponse("Show Mode activo: desligue-o para parar o motor");
        setRunState(EngineRunState::StoppingOnPurpose);
        clearShowState();
        running_ = false;
        exitCode_ = kExitRequested;
        log::info("shutdown pedido");
        return okResponse();
    }
    return errorResponse("comando não tratado");
}

protocol::EngineStatus EngineApp::buildStatus() const {
    protocol::EngineStatus s;
    s.version = VTHOST_VERSION;
    s.projectName = project_.name;
    s.projectPath = projectPath_;
    s.showMode = showMode_;
    s.fps = fps_;
    s.uptimeSeconds = std::chrono::duration<double>(Clock::now() - startTime_).count();
    s.recovered = recovered_;
    s.testCard = testCard_;
    if (player_) {
        player_->fillStatus(s);
        if (player_->active() && player_->index() >= 0) s.nextCue = project_.nextCue(player_->index());
    }
    s.audioDevice = audio_.deviceName();
    s.audioOk = audio_.ok();
    s.audioError = audio_.error();
    s.levelL = audio_.levelL();
    s.levelR = audio_.levelR();
    for (const auto& cfg : project_.outputs) {
        protocol::OutputStatus os;
        os.id = cfg.id;
        os.monitor = cfg.monitor;
        os.type = toString(cfg.type);
        if (!cfg.enabled) {
            os.ok = true;
            os.detail = "desativada";
        } else if (cfg.type == OutputType::Display || cfg.type == OutputType::Window) {
            for (const auto& w : outputs_)
                if (w->id() == cfg.id) {
                    os.ok = w->ok() && w->error().empty();
                    os.error = w->error();
                    os.detail = w->detail();
                }
        } else if (cfg.type == OutputType::Ndi) {
            for (const auto& n : ndi_)
                if (n->id() == cfg.id) {
                    os.ok = n->ok();
                    os.error = n->error();
                    os.detail = n->detail();
                }
        } else {
            os.ok = false;
            os.error = "Blackmagic DeckLink: em desenvolvimento";
        }
        s.outputs.push_back(std::move(os));
    }
    s.warnings = warnings_;
    if (renderer_.usingSoftwareFallback()) s.warnings.push_back("GPU indisponível: a usar renderização por software");
    return s;
}

void EngineApp::applySettings(const protocol::EngineSettings& s) {
    if (!(s == settings_)) {
        DWORD cls = ABOVE_NORMAL_PRIORITY_CLASS;
        if (s.priority == "normal") cls = NORMAL_PRIORITY_CLASS;
        else if (s.priority == "high") cls = HIGH_PRIORITY_CLASS;
        SetPriorityClass(GetCurrentProcess(), cls);
        log::info("preferências: vsync=" + std::string(s.vsync ? "on" : "off") + " preview=" +
                  std::to_string(s.previewFps) + "fps prioridade=" + s.priority +
                  " áudio=" + (s.audioDevice.empty() ? std::string("omissão") : s.audioDevice));
    }
    audio_.setDevice(s.audioDevice);
    settings_ = s;
}

// ---------------------------------------------------------------- preview

bool EngineApp::initPreview() {
    previewMapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                         static_cast<DWORD>(kPreviewBlockBytes), kPreviewName);
    if (!previewMapping_) return false;
    previewBlock_ = static_cast<std::uint8_t*>(MapViewOfFile(previewMapping_, FILE_MAP_ALL_ACCESS, 0, 0, kPreviewBlockBytes));
    if (!previewBlock_) return false;
    auto* h = reinterpret_cast<PreviewHeader*>(previewBlock_);
    h->magic = kPreviewMagic;
    return true;
}

void EngineApp::writePreview(const std::uint8_t* data, int rowPitch, int width, int height) {
    if (!previewBlock_ || width <= 0 || height <= 0 || width > kPreviewMaxWidth || height > kPreviewMaxHeight) return;
    auto* h = reinterpret_cast<PreviewHeader*>(previewBlock_);
    auto* seq = reinterpret_cast<volatile LONG64*>(&h->sequence);
    if (InterlockedIncrement64(seq) % 2 == 0) InterlockedIncrement64(seq); // garante ímpar = a escrever
    h->width = static_cast<std::uint32_t>(width);
    h->height = static_cast<std::uint32_t>(height);
    h->stride = static_cast<std::uint32_t>(width * 4);
    int cw = 0, ch = 0;
    renderer_.canvasSize(kCanvasId, cw, ch);
    h->canvasWidth = static_cast<std::uint32_t>(cw);
    h->canvasHeight = static_cast<std::uint32_t>(ch);
    std::uint8_t* dst = previewBlock_ + sizeof(PreviewHeader);
    const std::size_t row = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height; ++y) std::memcpy(dst + y * row, data + static_cast<std::size_t>(y) * rowPitch, row);
    h->frameNumber++;
    MemoryBarrier();
    InterlockedIncrement64(seq); // par = pronto
}

// ---------------------------------------------------------------- render loop

void EngineApp::renderFrame() {
    player_->update(project_);
    handleCueEnd();

    renderer_.beginCanvas(kCanvasId);
    player_->draw();

    // Saídas NDI: canvas em resolução total, copiado sem esperar pela GPU.
    bool anyNdi = false;
    for (const auto& n : ndi_) anyNdi = anyNdi || n->ok();
    if (anyNdi)
        renderer_.captureCanvas(kCanvasId, [this](const std::uint8_t* d, int pitch, int w, int h) {
            for (auto& n : ndi_) n->submit(d, pitch, w, h, fps_);
        });

    // Preview para o Studio ao ritmo escolhido nas preferências.
    const auto now = Clock::now();
    if (previewBlock_ && settings_.previewFps > 0 &&
        now - lastPreview_ >= std::chrono::duration<double>(1.0 / settings_.previewFps)) {
        lastPreview_ = now;
        renderer_.capturePreview(kCanvasId,
                                 [this](const std::uint8_t* d, int pitch, int w, int h) { writePreview(d, pitch, w, h); });
    }

    bool first = true;
    for (auto& o : outputs_) {
        o->present(first && settings_.vsync, testCard_); // vsync só no primeiro, para não somar esperas
        first = false;
    }
    if (outputs_.empty() || !settings_.vsync) Sleep(outputs_.empty() ? 16 : 1);
}

int EngineApp::run(const Options& options) {
    if (!initHeartbeat()) log::warn("heartbeat indisponível (watchdog não vai detectar bloqueios)");
    if (!initPreview()) log::warn("preview para o Studio indisponível");

    std::string err;
    if (!renderer_.init(&err)) {
        log::error("renderer: " + err);
        return kExitInitFailed;
    }
    player_ = std::make_unique<CuePlayer>(renderer_, audio_);
    syncCanvas();

    // Nada de protector de ecrã, monitores a desligar ou PC a suspender durante o show.
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    timeBeginPeriod(1);

    if (options.recover) recover();
    else if (!options.projectPath.empty() && !loadProject(options.projectPath, &err))
        log::error("projecto inicial: " + err);

    ipc_.start(paths::kPipeName);
    setRunState(EngineRunState::Running);
    log::info("motor a correr");

    int consecutiveFailures = 0;
    while (running_) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        try {
            ipc_.drain([this](const std::string& line) { return handle(line); });
            renderFrame();

            if (renderer_.deviceLost()) {
                log::error("dispositivo gráfico perdido: a reiniciar o motor");
                exitCode_ = kExitDeviceLost;
                break;
            }
            for (auto& o : outputs_) {
                if (o->swapChainLost()) {
                    log::error("swap chain perdida no output " + o->id() + ": a reiniciar o motor");
                    exitCode_ = kExitDeviceLost;
                    running_ = false;
                }
            }

            const auto now = Clock::now();
            if (now - lastMonitorCheck_ > 2s) {
                lastMonitorCheck_ = now;
                for (auto& o : outputs_)
                    if (!o->error().empty()) o->placeOnMonitor(); // tenta voltar ao monitor certo
            }
            consecutiveFailures = 0;
        } catch (const std::exception& e) {
            log::error(std::string("erro no render loop: ") + e.what());
            if (++consecutiveFailures > 30) {
                exitCode_ = kExitTooManyErrors;
                break;
            }
            Sleep(10);
        } catch (...) {
            log::error("erro desconhecido no render loop");
            if (++consecutiveFailures > 30) {
                exitCode_ = kExitTooManyErrors;
                break;
            }
            Sleep(10);
        }

        beat();
        ++fpsFrames_;
        const auto now = Clock::now();
        const double win = std::chrono::duration<double>(now - fpsWindowStart_).count();
        if (win >= 1.0) {
            fps_ = fpsFrames_ / win;
            fpsFrames_ = 0;
            fpsWindowStart_ = now;
        }
        if (running_) saveStateIfDue();
    }

    if (exitCode_ != kExitRequested) saveStateIfDue(true);
    else Sleep(150); // deixa a resposta ao "shutdown" chegar ao Studio
    ipc_.stop();
    timeEndPeriod(1);
    SetThreadExecutionState(ES_CONTINUOUS);
    log::info("motor terminou com código " + std::to_string(exitCode_));
    return exitCode_;
}

} // namespace vthost
