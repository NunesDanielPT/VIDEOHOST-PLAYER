#include "vthost/core/protocol.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace vthost::protocol {
namespace {

std::string dump(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

std::string str(const json& j, const char* k) {
    auto it = j.find(k);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

std::optional<double> num(const json& j, const char* k) {
    auto it = j.find(k);
    if (it == j.end() || !it->is_number()) return std::nullopt;
    const double v = it->get<double>();
    if (!std::isfinite(v)) return std::nullopt;
    return v;
}

bool boolean(const json& j, const char* k, bool def = false) {
    auto it = j.find(k);
    return (it != j.end() && it->is_boolean()) ? it->get<bool>() : def;
}

int integer(const json& j, const char* k, int def) {
    const double v = num(j, k).value_or(def);
    if (v < -1e9 || v > 1e9) return def;
    return static_cast<int>(v);
}

} // namespace

ParsedCommand parseCommand(const std::string& line) {
    ParsedCommand r;
    if (line.size() > kMaxLineBytes) {
        r.error = "mensagem demasiado grande";
        return r;
    }
    try {
        const json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            r.error = "JSON inválido";
            return r;
        }
        const std::string cmd = str(j, "cmd");
        Command c;
        if (cmd == "ping") {
            c.type = CommandType::Ping;
        } else if (cmd == "load_project") {
            c.type = CommandType::LoadProject;
            c.path = str(j, "path");
            if (c.path.empty()) {
                r.error = "load_project: falta 'path'";
                return r;
            }
        } else if (cmd == "play") {
            c.type = CommandType::Play;
            auto q = num(j, "cue");
            if (!q || *q < 0 || *q > 100000) {
                r.error = "play: 'cue' inválida";
                return r;
            }
            c.cue = static_cast<int>(*q);
            c.paused = boolean(j, "paused");
            c.cut = boolean(j, "cut");
        } else if (cmd == "transport") {
            c.type = CommandType::Transport;
            const std::string a = str(j, "action");
            if (a == "pause") c.action = TransportAction::Pause;
            else if (a == "resume") c.action = TransportAction::Resume;
            else if (a == "toggle") c.action = TransportAction::Toggle;
            else if (a == "stop") c.action = TransportAction::Stop;
            else {
                r.error = "transport: 'action' inválida";
                return r;
            }
        } else if (cmd == "seek" || cmd == "seek_remaining") {
            c.type = cmd == "seek" ? CommandType::Seek : CommandType::SeekRemaining;
            auto t = num(j, "time");
            if (!t) {
                r.error = cmd + ": falta 'time'";
                return r;
            }
            c.time = std::clamp(*t, 0.0, kMaxMediaSeconds);
        } else if (cmd == "set_settings") {
            c.type = CommandType::SetSettings;
            c.settings.vsync = boolean(j, "vsync", true);
            c.settings.lockCursor = boolean(j, "lockCursor", true);
            const double fps = num(j, "previewFps").value_or(30.0);
            c.settings.previewFps = fps <= 0.0 ? 0 : static_cast<int>(std::clamp(fps, 5.0, 60.0));
            const std::string pr = str(j, "priority");
            c.settings.priority = (pr == "normal" || pr == "high") ? pr : "above_normal";
            c.settings.audioDevice = str(j, "audioDevice");
        } else if (cmd == "set_show_mode") {
            c.type = CommandType::SetShowMode;
            c.on = boolean(j, "on");
        } else if (cmd == "set_test_card") {
            c.type = CommandType::SetTestCard;
            c.on = boolean(j, "on");
        } else if (cmd == "get_status") {
            c.type = CommandType::GetStatus;
        } else if (cmd == "list_displays") {
            c.type = CommandType::ListDisplays;
        } else if (cmd == "list_audio_devices") {
            c.type = CommandType::ListAudioDevices;
        } else if (cmd == "shutdown") {
            c.type = CommandType::Shutdown;
        } else if (cmd == "restart") {
            c.type = CommandType::Restart;
        } else {
            r.error = "comando desconhecido: " + cmd;
            return r;
        }
        r.command = c;
    } catch (const std::exception& e) {
        r.error = e.what();
    } catch (...) {
        r.error = "erro desconhecido";
    }
    return r;
}

std::string serializeCommand(const Command& c) {
    json j;
    switch (c.type) {
    case CommandType::Ping: j["cmd"] = "ping"; break;
    case CommandType::LoadProject: j = {{"cmd", "load_project"}, {"path", c.path}}; break;
    case CommandType::Play: j = {{"cmd", "play"}, {"cue", c.cue}, {"paused", c.paused}, {"cut", c.cut}}; break;
    case CommandType::Transport: {
        const char* a = "toggle";
        if (c.action == TransportAction::Pause) a = "pause";
        else if (c.action == TransportAction::Resume) a = "resume";
        else if (c.action == TransportAction::Stop) a = "stop";
        j = {{"cmd", "transport"}, {"action", a}};
        break;
    }
    case CommandType::Seek: j = {{"cmd", "seek"}, {"time", c.time}}; break;
    case CommandType::SeekRemaining: j = {{"cmd", "seek_remaining"}, {"time", c.time}}; break;
    case CommandType::SetSettings:
        j = {{"cmd", "set_settings"},
             {"vsync", c.settings.vsync},
             {"previewFps", c.settings.previewFps},
             {"priority", c.settings.priority},
             {"audioDevice", c.settings.audioDevice},
             {"lockCursor", c.settings.lockCursor}};
        break;
    case CommandType::SetShowMode: j = {{"cmd", "set_show_mode"}, {"on", c.on}}; break;
    case CommandType::SetTestCard: j = {{"cmd", "set_test_card"}, {"on", c.on}}; break;
    case CommandType::GetStatus: j["cmd"] = "get_status"; break;
    case CommandType::ListDisplays: j["cmd"] = "list_displays"; break;
    case CommandType::ListAudioDevices: j["cmd"] = "list_audio_devices"; break;
    case CommandType::Shutdown: j["cmd"] = "shutdown"; break;
    case CommandType::Restart: j["cmd"] = "restart"; break;
    }
    return dump(j);
}

std::string okResponse() { return dump(json{{"ok", true}}); }

std::string errorResponse(const std::string& message) { return dump(json{{"ok", false}, {"error", message}}); }

std::string statusResponse(const EngineStatus& s) {
    json outputs = json::array();
    for (const auto& o : s.outputs)
        outputs.push_back({{"id", o.id}, {"monitor", o.monitor}, {"ok", o.ok}, {"error", o.error},
                           {"type", o.type}, {"detail", o.detail}});
    json j = {{"ok", true},
              {"status",
               {{"version", s.version},
                {"project", s.projectName},
                {"projectPath", s.projectPath},
                {"showMode", s.showMode},
                {"fps", s.fps},
                {"uptime", s.uptimeSeconds},
                {"recovered", s.recovered},
                {"testCard", s.testCard},
                {"cue", s.cue},
                {"cueId", s.cueId},
                {"state", s.state},
                {"error", s.error},
                {"mediaTime", s.mediaTime},
                {"elapsed", s.elapsed},
                {"remaining", s.remaining},
                {"passDuration", s.passDuration},
                {"pass", s.pass},
                {"infinite", s.infinite},
                {"fileDuration", s.fileDuration},
                {"nextCue", s.nextCue},
                {"audioDevice", s.audioDevice},
                {"audioOk", s.audioOk},
                {"audioError", s.audioError},
                {"levelL", s.levelL},
                {"levelR", s.levelR},
                {"renderMs", s.renderMs},
                {"vramUsedMb", s.vramUsedMb},
                {"vramBudgetMb", s.vramBudgetMb},
                {"gpu", s.gpuName},
                {"outputs", outputs},
                {"warnings", s.warnings}}}};
    return dump(j);
}

std::string displaysResponse(const std::vector<DisplayInfo>& displays) {
    json arr = json::array();
    for (const auto& d : displays)
        arr.push_back({{"id", d.id}, {"name", d.name}, {"adapter", d.adapter}, {"width", d.width},
                       {"height", d.height}, {"refresh", d.refreshHz}, {"x", d.x}, {"y", d.y},
                       {"primary", d.primary}, {"index", d.index}});
    return dump(json{{"ok", true}, {"displays", arr}});
}

std::string audioDevicesResponse(const std::vector<AudioDeviceInfo>& devices) {
    json arr = json::array();
    for (const auto& d : devices) arr.push_back({{"id", d.id}, {"name", d.name}, {"default", d.isDefault}});
    return dump(json{{"ok", true}, {"audioDevices", arr}});
}

ParsedResponse parseResponse(const std::string& line) {
    ParsedResponse r;
    try {
        const json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            r.error = "resposta inválida do motor";
            return r;
        }
        r.ok = boolean(j, "ok");
        r.error = str(j, "error");
        auto st = j.find("status");
        if (st != j.end() && st->is_object()) {
            const json& s = *st;
            EngineStatus es;
            es.version = str(s, "version");
            es.projectName = str(s, "project");
            es.projectPath = str(s, "projectPath");
            es.showMode = boolean(s, "showMode");
            es.fps = num(s, "fps").value_or(0.0);
            es.uptimeSeconds = num(s, "uptime").value_or(0.0);
            es.recovered = boolean(s, "recovered");
            es.testCard = boolean(s, "testCard");
            es.cue = integer(s, "cue", -1);
            es.cueId = str(s, "cueId");
            es.state = str(s, "state");
            if (es.state.empty()) es.state = "stopped";
            es.error = str(s, "error");
            es.mediaTime = std::max(0.0, num(s, "mediaTime").value_or(0.0));
            es.elapsed = std::max(0.0, num(s, "elapsed").value_or(0.0));
            es.remaining = std::max(0.0, num(s, "remaining").value_or(0.0));
            es.passDuration = std::max(0.0, num(s, "passDuration").value_or(0.0));
            es.pass = std::max(0, integer(s, "pass", 0));
            es.infinite = boolean(s, "infinite");
            es.fileDuration = std::max(0.0, num(s, "fileDuration").value_or(0.0));
            es.nextCue = integer(s, "nextCue", -1);
            es.audioDevice = str(s, "audioDevice");
            es.audioOk = boolean(s, "audioOk");
            es.audioError = str(s, "audioError");
            es.levelL = std::clamp(num(s, "levelL").value_or(0.0), 0.0, 4.0);
            es.levelR = std::clamp(num(s, "levelR").value_or(0.0), 0.0, 4.0);
            es.renderMs = std::max(0.0, num(s, "renderMs").value_or(0.0));
            es.vramUsedMb = std::max(0.0, num(s, "vramUsedMb").value_or(0.0));
            es.vramBudgetMb = std::max(0.0, num(s, "vramBudgetMb").value_or(0.0));
            es.gpuName = str(s, "gpu");
            if (auto a = s.find("outputs"); a != s.end() && a->is_array())
                for (const auto& o : *a)
                    if (o.is_object())
                        es.outputs.push_back({str(o, "id"), integer(o, "monitor", 0), boolean(o, "ok"),
                                              str(o, "error"), str(o, "type"), str(o, "detail")});
            if (auto a = s.find("warnings"); a != s.end() && a->is_array())
                for (const auto& w : *a)
                    if (w.is_string()) es.warnings.push_back(w.get<std::string>());
            r.status = std::move(es);
        }
        if (auto ds = j.find("displays"); ds != j.end() && ds->is_array()) {
            std::vector<DisplayInfo> list;
            for (const auto& d : *ds) {
                if (!d.is_object()) continue;
                DisplayInfo di;
                di.id = str(d, "id");
                di.name = str(d, "name");
                di.adapter = str(d, "adapter");
                di.width = integer(d, "width", 0);
                di.height = integer(d, "height", 0);
                di.refreshHz = num(d, "refresh").value_or(0.0);
                di.x = integer(d, "x", 0);
                di.y = integer(d, "y", 0);
                di.primary = boolean(d, "primary");
                di.index = integer(d, "index", 0);
                list.push_back(std::move(di));
            }
            r.displays = std::move(list);
        }
        if (auto ad = j.find("audioDevices"); ad != j.end() && ad->is_array()) {
            std::vector<AudioDeviceInfo> list;
            for (const auto& d : *ad)
                if (d.is_object()) list.push_back({str(d, "id"), str(d, "name"), boolean(d, "default")});
            r.audioDevices = std::move(list);
        }
    } catch (const std::exception& e) {
        r.ok = false;
        r.error = e.what();
    } catch (...) {
        r.ok = false;
        r.error = "erro desconhecido";
    }
    return r;
}

} // namespace vthost::protocol
