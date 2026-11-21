#include "vthost/core/show_state.h"

#include "vthost/core/atomic_file.h"
#include "vthost/core/paths.h"

#include <algorithm>
#include <cmath>
#include <system_error>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace vthost {

std::string serializeShowState(const ShowState& s) {
    json layers = json::array();
    for (const auto& l : s.layers)
        layers.push_back({{"layer", l.layerId}, {"media", l.mediaId}, {"position", l.positionSeconds},
                          {"opacity", l.props.opacity}, {"blend", toString(l.props.blend)},
                          {"x", l.props.posX}, {"y", l.props.posY}, {"scale", l.props.scale},
                          {"rotation", l.props.rotation}});
    json j = {{"format", "vthost-show-state"},
              {"projectPath", s.projectPath},
              {"showMode", s.showMode},
              {"activeColumn", s.activeColumn},
              {"timeline", s.timelineId},
              {"timelineTime", s.timelineTime},
              {"timelinePlaying", s.timelinePlaying},
              {"layers", layers}};
    return j.dump(2, ' ', false, json::error_handler_t::replace);
}

bool parseShowState(const std::string& text, ShowState& out) {
    try {
        const json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object() || j.value("format", "") != "vthost-show-state")
            return false;
        ShowState s;
        s.projectPath = j.value("projectPath", "");
        s.showMode = j.value("showMode", false);
        s.activeColumn = j.value("activeColumn", -1);
        s.timelineId = j.value("timeline", "");
        const double tt = j.value("timelineTime", 0.0);
        s.timelineTime = std::isfinite(tt) ? std::clamp(tt, 0.0, kMaxTimelineSeconds) : 0.0;
        s.timelinePlaying = j.value("timelinePlaying", false);
        if (auto a = j.find("layers"); a != j.end() && a->is_array()) {
            for (const auto& l : *a) {
                if (!l.is_object()) continue;
                LayerPlayback lp;
                lp.layerId = l.value("layer", "");
                lp.mediaId = l.value("media", "");
                double pos = l.value("position", 0.0);
                lp.positionSeconds = std::isfinite(pos) ? std::max(0.0, pos) : 0.0;
                lp.props.opacity = l.value("opacity", 1.0);
                lp.props.blend = blendModeFromString(l.value("blend", "normal"));
                lp.props.posX = l.value("x", 0.0);
                lp.props.posY = l.value("y", 0.0);
                lp.props.scale = l.value("scale", 1.0);
                lp.props.rotation = l.value("rotation", 0.0);
                lp.props.clamp();
                if (!lp.layerId.empty()) s.layers.push_back(std::move(lp));
            }
        }
        out = std::move(s);
        return true;
    } catch (...) {
        return false;
    }
}

bool saveShowState(const ShowState& s, std::string* error) {
    // Sem .bak: é gravado muitas vezes e só interessa a versão mais recente.
    return writeFileAtomic(paths::showStateFile(), serializeShowState(s), false, error);
}

bool loadShowState(ShowState& out) {
    auto text = readFileLimited(paths::showStateFile(), 4u * 1024u * 1024u);
    return text && parseShowState(*text, out);
}

void clearShowState() {
    std::error_code ec;
    std::filesystem::remove(paths::showStateFile(), ec);
}

} // namespace vthost
