#include "vthost/core/protocol.h"

#include "mapping_json.h"
#include "props_json.h"

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
        } else if (cmd == "go") {
            c.type = CommandType::Go;
            auto col = num(j, "column");
            if (!col || *col < 0 || *col > 100000) {
                r.error = "go: 'column' inválida";
                return r;
            }
            c.column = static_cast<int>(*col);
            c.value = std::clamp(num(j, "fade").value_or(-1.0), -1.0, 60.0);
        } else if (cmd == "stop_all") {
            c.type = CommandType::StopAll;
            c.value = std::clamp(num(j, "fade").value_or(1.0), 0.0, 60.0);
        } else if (cmd == "set_layer_props") {
            c.type = CommandType::SetLayerProps;
            c.layer = str(j, "layer");
            auto pr = j.find("props");
            if (c.layer.empty() || pr == j.end() || !pr->is_object()) {
                r.error = "set_layer_props: faltam 'layer'/'props'";
                return r;
            }
            propsFromJson(*pr, c.props);
        } else if (cmd == "set_layer_visible") {
            c.type = CommandType::SetLayerVisible;
            c.layer = str(j, "layer");
            auto it = j.find("on");
            if (c.layer.empty() || it == j.end() || !it->is_boolean()) {
                r.error = "set_layer_visible: faltam 'layer'/'on'";
                return r;
            }
            c.on = it->get<bool>();
        } else if (cmd == "layer_seek") {
            c.type = CommandType::LayerSeek;
            c.layer = str(j, "layer");
            auto t = num(j, "time");
            if (c.layer.empty() || !t) {
                r.error = "layer_seek: faltam 'layer'/'time'";
                return r;
            }
            c.value = std::clamp(*t, 0.0, kMaxTimelineSeconds);
        } else if (cmd == "layer_pause") {
            c.type = CommandType::LayerPause;
            c.layer = str(j, "layer");
            auto it = j.find("on");
            if (c.layer.empty() || it == j.end() || !it->is_boolean()) {
                r.error = "layer_pause: faltam 'layer'/'on'";
                return r;
            }
            c.on = it->get<bool>();
        } else if (cmd == "timeline") {
            c.type = CommandType::Timeline;
            c.timeline = str(j, "timeline");
            const std::string action = str(j, "action");
            if (action == "play") c.action = TimelineAction::Play;
            else if (action == "pause") c.action = TimelineAction::Pause;
            else if (action == "stop") c.action = TimelineAction::Stop;
            else if (action == "seek") c.action = TimelineAction::Seek;
            else {
                r.error = "timeline: 'action' inválida";
                return r;
            }
            if (c.timeline.empty() && c.action != TimelineAction::Stop && c.action != TimelineAction::Pause) {
                r.error = "timeline: falta 'timeline'";
                return r;
            }
            c.value = std::clamp(num(j, "time").value_or(-1.0), -1.0, kMaxTimelineSeconds);
        } else if (cmd == "set_show_mode") {
            c.type = CommandType::SetShowMode;
            auto it = j.find("on");
            c.on = it != j.end() && it->is_boolean() && it->get<bool>();
        } else if (cmd == "get_status") {
            c.type = CommandType::GetStatus;
        } else if (cmd == "set_settings") {
            c.type = CommandType::SetSettings;
            auto b = [&](const char* k, bool def) {
                auto it = j.find(k);
                return (it != j.end() && it->is_boolean()) ? it->get<bool>() : def;
            };
            c.settings.vsync = b("vsync", true);
            c.settings.loopBoard = b("loopBoard", false);
            const double fps = num(j, "previewFps").value_or(30.0);
            c.settings.previewFps = fps <= 0.0 ? 0 : static_cast<int>(std::clamp(fps, 5.0, 60.0));
            const std::string pr = str(j, "priority");
            c.settings.priority = (pr == "normal" || pr == "high") ? pr : "above_normal";
        } else if (cmd == "set_output_mapping") {
            c.type = CommandType::SetOutputMapping;
            c.output = str(j, "output");
            auto mp = j.find("mapping");
            if (c.output.empty() || mp == j.end() || !mp->is_object()) {
                r.error = "set_output_mapping: faltam 'output'/'mapping'";
                return r;
            }
            mappingFromJson(*mp, c.mapping);
        } else if (cmd == "set_test_card") {
            c.type = CommandType::SetTestCard;
            auto it = j.find("on");
            c.on = it != j.end() && it->is_boolean() && it->get<bool>();
        } else if (cmd == "set_preview_canvas") {
            c.type = CommandType::SetPreviewCanvas;
            c.canvas = str(j, "canvas");
        } else if (cmd == "list_displays") {
            c.type = CommandType::ListDisplays;
        } else if (cmd == "shutdown") {
            c.type = CommandType::Shutdown;
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
    case CommandType::Go: j = {{"cmd", "go"}, {"column", c.column}, {"fade", c.value}}; break;
    case CommandType::SetSettings:
        j = {{"cmd", "set_settings"},
             {"vsync", c.settings.vsync},
             {"previewFps", c.settings.previewFps},
             {"priority", c.settings.priority},
             {"loopBoard", c.settings.loopBoard}};
        break;
    case CommandType::StopAll: j = {{"cmd", "stop_all"}, {"fade", c.value}}; break;
    case CommandType::SetLayerProps:
        j = {{"cmd", "set_layer_props"},
             {"layer", c.layer},
             {"props", propsToJson(c.props)}};
        break;
    case CommandType::SetLayerVisible:
        j = {{"cmd", "set_layer_visible"}, {"layer", c.layer}, {"on", c.on}};
        break;
    case CommandType::Timeline: {
        const char* a = "play";
        if (c.action == TimelineAction::Pause) a = "pause";
        else if (c.action == TimelineAction::Stop) a = "stop";
        else if (c.action == TimelineAction::Seek) a = "seek";
        j = {{"cmd", "timeline"}, {"action", a}, {"timeline", c.timeline}, {"time", c.value}};
        break;
    }
    case CommandType::SetShowMode: j = {{"cmd", "set_show_mode"}, {"on", c.on}}; break;
    case CommandType::GetStatus: j["cmd"] = "get_status"; break;
    case CommandType::ListDisplays: j["cmd"] = "list_displays"; break;
    case CommandType::SetOutputMapping:
        j = {{"cmd", "set_output_mapping"}, {"output", c.output}, {"mapping", mappingToJson(c.mapping)}};
        break;
    case CommandType::SetTestCard: j = {{"cmd", "set_test_card"}, {"on", c.on}}; break;
    case CommandType::SetPreviewCanvas: j = {{"cmd", "set_preview_canvas"}, {"canvas", c.canvas}}; break;
    case CommandType::LayerSeek: j = {{"cmd", "layer_seek"}, {"layer", c.layer}, {"time", c.value}}; break;
    case CommandType::LayerPause: j = {{"cmd", "layer_pause"}, {"layer", c.layer}, {"on", c.on}}; break;
    case CommandType::Shutdown: j["cmd"] = "shutdown"; break;
    }
    return dump(j);
}

std::string okResponse() { return dump(json{{"ok", true}}); }

std::string errorResponse(const std::string& message) {
    return dump(json{{"ok", false}, {"error", message}});
}

std::string statusResponse(const EngineStatus& s) {
    json layers = json::array();
    for (const auto& l : s.layers)
        layers.push_back({{"id", l.id}, {"media", l.mediaId}, {"state", l.state},
                          {"position", l.position}, {"error", l.error}, {"duration", l.duration},
                          {"paused", l.paused}});
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
                {"activeColumn", s.activeColumn},
                {"activeColumnTime", s.activeColumnTime},
                {"fps", s.fps},
                {"uptime", s.uptimeSeconds},
                {"recovered", s.recovered},
                {"timeline", s.timelineId},
                {"timelineTime", s.timelineTime},
                {"timelinePlaying", s.timelinePlaying},
                {"testCard", s.testCard},
                {"previewCanvas", s.previewCanvas},
                {"layers", layers},
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

ParsedResponse parseResponse(const std::string& line) {
    ParsedResponse r;
    try {
        const json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            r.error = "resposta inválida do motor";
            return r;
        }
        auto ok = j.find("ok");
        r.ok = ok != j.end() && ok->is_boolean() && ok->get<bool>();
        r.error = str(j, "error");
        auto st = j.find("status");
        if (st != j.end() && st->is_object()) {
            const json& s = *st;
            EngineStatus es;
            es.version = str(s, "version");
            es.projectName = str(s, "project");
            es.projectPath = str(s, "projectPath");
            es.showMode = s.value("showMode", false);
            es.activeColumn = static_cast<int>(num(s, "activeColumn").value_or(-1));
            es.activeColumnTime = std::max(0.0, num(s, "activeColumnTime").value_or(0.0));
            es.fps = num(s, "fps").value_or(0.0);
            es.uptimeSeconds = num(s, "uptime").value_or(0.0);
            es.recovered = s.value("recovered", false);
            es.timelineId = str(s, "timeline");
            es.timelineTime = num(s, "timelineTime").value_or(0.0);
            es.timelinePlaying = s.value("timelinePlaying", false);
            es.testCard = s.value("testCard", false);
            es.previewCanvas = str(s, "previewCanvas");
            if (auto a = s.find("layers"); a != s.end() && a->is_array())
                for (const auto& l : *a)
                    if (l.is_object())
                        es.layers.push_back({str(l, "id"), str(l, "media"), str(l, "state"),
                                             num(l, "position").value_or(0.0), str(l, "error"),
                                             std::max(0.0, num(l, "duration").value_or(0.0)),
                                             l.contains("paused") && l["paused"].is_boolean() &&
                                                 l["paused"].get<bool>()});
            if (auto a = s.find("outputs"); a != s.end() && a->is_array())
                for (const auto& o : *a)
                    if (o.is_object())
                        es.outputs.push_back({str(o, "id"), static_cast<int>(num(o, "monitor").value_or(0)),
                                              o.value("ok", false), str(o, "error"), str(o, "type"),
                                              str(o, "detail")});
            if (auto a = s.find("warnings"); a != s.end() && a->is_array())
                for (const auto& w : *a)
                    if (w.is_string()) es.warnings.push_back(w.get<std::string>());
            r.status = std::move(es);
        }
        auto ds = j.find("displays");
        if (ds != j.end() && ds->is_array()) {
            std::vector<DisplayInfo> list;
            for (const auto& d : *ds) {
                if (!d.is_object()) continue;
                DisplayInfo di;
                di.id = str(d, "id");
                di.name = str(d, "name");
                di.adapter = str(d, "adapter");
                di.width = static_cast<int>(num(d, "width").value_or(0));
                di.height = static_cast<int>(num(d, "height").value_or(0));
                di.refreshHz = num(d, "refresh").value_or(0.0);
                di.x = static_cast<int>(num(d, "x").value_or(0));
                di.y = static_cast<int>(num(d, "y").value_or(0));
                di.primary = d.value("primary", false);
                di.index = static_cast<int>(num(d, "index").value_or(0));
                list.push_back(std::move(di));
            }
            r.displays = std::move(list);
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
