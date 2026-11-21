#include "engine_app.h"

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

EngineApp::~EngineApp() {
    players_.clear();
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
    log::info("projecto carregado: " + project_.name + " (" + path + ")");

    renderer_.resizeCanvas(project_.canvasWidth, project_.canvasHeight);
    syncLayers();
    syncOutputs();
    if (activeColumn_ >= static_cast<int>(project_.columns.size())) activeColumn_ = -1;
    if (!activeTimeline_.empty() && !project_.findTimeline(activeTimeline_)) timelineStop();
    // Clips da timeline podem ter mudado: reavalia no próximo frame.
    if (!activeTimeline_.empty())
        for (auto& [id, p] : players_) p->clearTag();
    return true;
}

void EngineApp::syncLayers() {
    // Layers novas ganham um player; as removidas desaparecem. As que continuam
    // NÃO são interrompidas: editar o projecto não pára o que está no ecrã.
    std::set<std::string> ids;
    for (const auto& l : project_.layers) {
        ids.insert(l.id);
        if (!players_.count(l.id)) players_[l.id] = std::make_unique<LayerPlayer>(l.id);
        layerProps_[l.id] = l.props;
    }
    for (auto it = players_.begin(); it != players_.end();) {
        if (!ids.count(it->first)) {
            layerProps_.erase(it->first);
            it = players_.erase(it);
        } else {
            ++it;
        }
    }
}

void EngineApp::syncOutputs() {
    bool same = outputs_.size() == project_.outputs.size();
    for (std::size_t i = 0; same && i < outputs_.size(); ++i)
        same = outputs_[i]->id() == project_.outputs[i].id &&
               outputs_[i]->monitor() == project_.outputs[i].monitor;
    if (same) return;

    outputs_.clear();
    for (const auto& o : project_.outputs) {
        auto w = std::make_unique<OutputWindow>(renderer_, o.id, o.name, o.monitor);
        if (!w->create()) log::error("output " + o.id + ": " + w->error());
        outputs_.push_back(std::move(w));
    }
}

bool EngineApp::go(int column, std::string* error) {
    if (column < 0 || column >= static_cast<int>(project_.columns.size())) {
        if (error) *error = "coluna inexistente";
        return false;
    }
    leaveTimeline(); // o dashboard volta a mandar nas layers
    const Column& col = project_.columns[static_cast<std::size_t>(column)];
    for (const auto& layer : project_.layers) {
        auto& player = players_[layer.id];
        if (!player) player = std::make_unique<LayerPlayer>(layer.id);
        auto cell = col.cells.find(layer.id);
        if (cell == col.cells.end()) {
            // Célula vazia: a coluna define o estado completo, por isso a layer apaga-se.
            player->stop(col.fadeSeconds);
            continue;
        }
        const MediaItem* media = project_.findMedia(cell->second);
        if (!media) continue;
        if (player->mediaId() == media->id && player->tag().empty()) continue; // já está a tocar: não reinicia
        player->play(media->id, media->path, media->loop, col.fadeSeconds);
    }
    activeColumn_ = column;
    log::info("GO coluna " + std::to_string(column) + " (" + col.name + ")");
    saveStateIfDue(true);
    return true;
}

void EngineApp::stopAll(double fade) {
    leaveTimeline();
    for (auto& [id, p] : players_) p->stop(fade);
    activeColumn_ = -1;
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
    activeColumn_ = st.activeColumn;
    for (const auto& lp : st.layers) {
        auto it = players_.find(lp.layerId);
        if (it == players_.end()) continue;
        layerProps_[lp.layerId] = lp.props;
        if (!st.timelineId.empty()) continue; // a timeline repõe as media sozinha
        if (const MediaItem* m = project_.findMedia(lp.mediaId))
            it->second->play(m->id, m->path, m->loop, 0.0, lp.positionSeconds);
    }
    if (!st.timelineId.empty() && project_.findTimeline(st.timelineId)) {
        activeTimeline_ = st.timelineId;
        tlTime_ = st.timelineTime;
        tlLastEval_ = st.timelineTime;
        tlAnchor_ = Clock::now();
        tlPlaying_ = st.timelinePlaying;
        activeColumn_ = -1;
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
    st.activeColumn = activeColumn_;
    st.timelineId = activeTimeline_;
    st.timelineTime = timelineTime();
    st.timelinePlaying = tlPlaying_;
    for (const auto& l : project_.layers) {
        auto it = players_.find(l.id);
        if (it == players_.end()) continue;
        const auto pr = layerProps_.find(l.id);
        st.layers.push_back({l.id, it->second->mediaId(), it->second->position(),
                             pr != layerProps_.end() ? pr->second : l.props});
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
        if (showMode_) return errorResponse("Show Mode activo: desligue-o para alterar o projecto");
        return loadProject(c.path, &err) ? okResponse() : errorResponse(err);
    case CommandType::Go:
        return go(c.column, &err) ? okResponse() : errorResponse(err);
    case CommandType::StopAll:
        stopAll(c.value);
        return okResponse();
    case CommandType::SetLayerProps:
        if (!project_.findLayer(c.layer)) return errorResponse("layer inexistente");
        layerProps_[c.layer] = c.props; // ao vivo, também em Show Mode
        return okResponse();
    case CommandType::Timeline:
        switch (c.action) {
        case TimelineAction::Play:
            return timelinePlay(c.timeline, c.value, &err) ? okResponse() : errorResponse(err);
        case TimelineAction::Pause:
            timelinePause();
            return okResponse();
        case TimelineAction::Stop:
            timelineStop();
            return okResponse();
        case TimelineAction::Seek:
            return timelineSeek(c.timeline, c.value, &err) ? okResponse() : errorResponse(err);
        }
        return errorResponse("acção de timeline desconhecida");
    case CommandType::SetShowMode:
        showMode_ = c.on;
        log::info(showMode_ ? "Show Mode ligado" : "Show Mode desligado");
        saveStateIfDue(true);
        return okResponse();
    case CommandType::GetStatus:
        return statusResponse(buildStatus());
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
    s.activeColumn = activeColumn_;
    s.fps = fps_;
    s.uptimeSeconds = std::chrono::duration<double>(Clock::now() - startTime_).count();
    s.recovered = recovered_;
    s.timelineId = activeTimeline_;
    s.timelineTime = timelineTime();
    s.timelinePlaying = tlPlaying_;
    for (const auto& l : project_.layers) {
        auto it = players_.find(l.id);
        if (it == players_.end()) continue;
        s.layers.push_back({l.id, it->second->mediaId(), it->second->stateName(), it->second->position(),
                            it->second->error()});
    }
    for (const auto& o : outputs_) s.outputs.push_back({o->id(), o->monitor(), o->ok(), o->error()});
    s.warnings = warnings_;
    if (renderer_.usingSoftwareFallback()) s.warnings.push_back("GPU indisponível: a usar renderização por software");
    return s;
}

// ---------------------------------------------------------------- timeline

double EngineApp::timelineTime() const {
    if (!tlPlaying_) return tlTime_;
    return tlTime_ + std::chrono::duration<double>(Clock::now() - tlAnchor_).count();
}

bool EngineApp::timelinePlay(const std::string& id, double time, std::string* error) {
    if (!project_.findTimeline(id)) {
        if (error) *error = "timeline inexistente";
        return false;
    }
    if (activeTimeline_ != id) {
        activeTimeline_ = id;
        activeColumn_ = -1;
        tlTime_ = 0.0;
        tlLastEval_ = 0.0;
        for (auto& [lid, p] : players_) p->clearTag();
    }
    if (time >= 0.0 && !timelineSeek(id, time, error)) return false;
    tlTime_ = timelineTime();
    tlAnchor_ = Clock::now();
    tlPlaying_ = true;
    for (auto& [lid, p] : players_) p->setPaused(false);
    log::info("timeline " + id + " play @ " + std::to_string(tlTime_));
    saveStateIfDue(true);
    return true;
}

void EngineApp::timelinePause() {
    if (activeTimeline_.empty() || !tlPlaying_) return;
    tlTime_ = timelineTime();
    tlPlaying_ = false;
    for (auto& [lid, p] : players_) p->setPaused(true);
    saveStateIfDue(true);
}

void EngineApp::timelineStop() {
    if (activeTimeline_.empty()) return;
    activeTimeline_.clear();
    tlPlaying_ = false;
    tlTime_ = 0.0;
    for (auto& [lid, p] : players_) {
        p->setPaused(false);
        p->stop(0.3);
    }
    saveStateIfDue(true);
}

bool EngineApp::timelineSeek(const std::string& id, double time, std::string* error) {
    const Timeline* tl = project_.findTimeline(id);
    if (!tl) {
        if (error) *error = "timeline inexistente";
        return false;
    }
    if (activeTimeline_ != id) {
        // Seek numa timeline parada: fica activa e em pausa nesse ponto.
        activeTimeline_ = id;
        activeColumn_ = -1;
        tlPlaying_ = false;
    }
    tlTime_ = std::clamp(time, 0.0, tl->duration());
    tlLastEval_ = tlTime_;
    tlAnchor_ = Clock::now();
    for (auto& [lid, p] : players_) {
        p->clearTag(); // recomeça cada clip no ponto certo
        p->setPaused(!tlPlaying_);
    }
    return true;
}

void EngineApp::leaveTimeline() {
    if (activeTimeline_.empty()) return;
    activeTimeline_.clear();
    tlPlaying_ = false;
    for (auto& [lid, p] : players_) p->setPaused(false);
}

void EngineApp::updateTimeline() {
    if (activeTimeline_.empty()) return;
    const Timeline* tl = project_.findTimeline(activeTimeline_);
    if (!tl) {
        timelineStop();
        return;
    }
    double t = timelineTime();

    // Segmentos em loop: ao passar o fim, volta ao início do segmento.
    for (const auto& seg : tl->segments) {
        if (!seg.loop || !tlPlaying_) continue;
        const double len = seg.end - seg.start;
        if (len < 0.1) continue;
        if (tlLastEval_ >= seg.start - 0.001 && tlLastEval_ < seg.end && t >= seg.end) {
            t = seg.start + std::fmod(t - seg.end, len);
            tlTime_ = t;
            tlAnchor_ = Clock::now();
            for (auto& [lid, p] : players_) p->clearTag();
            break;
        }
    }

    const double dur = tl->duration();
    if (tlPlaying_ && t >= dur) {
        // Fim da timeline: fica parada no último frame.
        t = dur;
        tlTime_ = dur;
        tlPlaying_ = false;
        for (auto& [lid, p] : players_) p->setPaused(true);
    }
    tlLastEval_ = t;

    for (const auto& layer : project_.layers) {
        auto& player = players_[layer.id];
        if (!player) player = std::make_unique<LayerPlayer>(layer.id);
        const Clip* clip = tl->clipAt(layer.id, t);
        if (clip) {
            if (player->tag() == clip->id) continue;
            const MediaItem* media = project_.findMedia(clip->mediaId);
            if (!media) continue;
            player->play(media->id, media->path, media->loop, 0.0, clip->inPoint + (t - clip->start), clip->id);
            player->setPaused(!tlPlaying_);
        } else if (!player->mediaId().empty()) {
            player->stop(0.0);
        }
    }
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
    h->canvasWidth = static_cast<std::uint32_t>(renderer_.canvasWidth());
    h->canvasHeight = static_cast<std::uint32_t>(renderer_.canvasHeight());
    std::uint8_t* dst = previewBlock_ + sizeof(PreviewHeader);
    const std::size_t row = static_cast<std::size_t>(width) * 4;
    for (int y = 0; y < height; ++y) std::memcpy(dst + y * row, data + static_cast<std::size_t>(y) * rowPitch, row);
    h->frameNumber++;
    MemoryBarrier();
    InterlockedIncrement64(seq); // par = pronto
}

// ---------------------------------------------------------------- render loop

void EngineApp::renderFrame() {
    updateTimeline();
    for (auto& [id, p] : players_) p->update(renderer_);

    renderer_.beginCanvas();
    // layers[0] é a de cima (como na grelha do Studio): desenha de baixo para cima.
    for (auto it = project_.layers.rbegin(); it != project_.layers.rend(); ++it) {
        if (!it->visible) continue;
        auto p = players_.find(it->id);
        if (p == players_.end()) continue;
        const auto pr = layerProps_.find(it->id);
        p->second->draw(renderer_, pr != layerProps_.end() ? pr->second : it->props);
    }

    // Preview para o Studio a ~30 fps (um frame sim, outro não).
    if (previewBlock_ && (++previewFrames_ & 1) == 0)
        renderer_.capturePreview([this](const std::uint8_t* d, int pitch, int w, int h) { writePreview(d, pitch, w, h); });

    bool first = true;
    for (auto& o : outputs_) {
        o->present(first); // vsync só no primeiro, para não somar esperas
        first = false;
    }
    if (outputs_.empty()) Sleep(16);
}

int EngineApp::run(const Options& options) {
    if (!initHeartbeat()) log::warn("heartbeat indisponível (watchdog não vai detectar bloqueios)");
    if (!initPreview()) log::warn("preview para o Studio indisponível");

    std::string err;
    if (!renderer_.init(&err)) {
        log::error("renderer: " + err);
        return kExitInitFailed;
    }
    renderer_.resizeCanvas(project_.canvasWidth, project_.canvasHeight);

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
