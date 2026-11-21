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
    for (const auto& l : s.compLayers)
        layers.push_back({{"comp", l.comp}, {"layer", l.layer}, {"column", l.column}, {"time", l.cueTime}});
    json j = {{"compLayers", layers},{"format", "vthost-show-state"},
              {"version", 2},
              {"projectPath", s.projectPath},
              {"showMode", s.showMode},
              {"cue", s.cueId},
              {"cueTime", s.cueTime},
              {"paused", s.paused},
              {"testCard", s.testCard}};
    return j.dump(2, ' ', false, json::error_handler_t::replace);
}

bool parseShowState(const std::string& text, ShowState& out) {
    try {
        const json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return false;
        auto str = [&](const char* k) {
            auto it = j.find(k);
            return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
        };
        auto flag = [&](const char* k) {
            auto it = j.find(k);
            return it != j.end() && it->is_boolean() && it->get<bool>();
        };
        if (str("format") != "vthost-show-state") return false;
        ShowState s;
        s.projectPath = str("projectPath");
        s.showMode = flag("showMode");
        s.cueId = str("cue");
        if (auto it = j.find("cueTime"); it != j.end() && it->is_number()) {
            const double t = it->get<double>();
            s.cueTime = std::isfinite(t) ? std::clamp(t, 0.0, 1e9) : 0.0;
        }
        s.paused = flag("paused");
        s.testCard = flag("testCard");
        if (auto a = j.find("compLayers"); a != j.end() && a->is_array())
            for (const auto& l : *a) {
                if (!l.is_object() || s.compLayers.size() >= 4096) continue;
                CompLayerPlayback p;
                auto get = [&](const char* k) {
                    auto it = l.find(k);
                    return (it != l.end() && it->is_string()) ? it->get<std::string>() : std::string{};
                };
                p.comp = get("comp");
                p.layer = get("layer");
                p.column = get("column");
                if (auto t = l.find("time"); t != l.end() && t->is_number()) {
                    const double v = t->get<double>();
                    p.cueTime = std::isfinite(v) ? std::clamp(v, 0.0, 1e9) : 0.0;
                }
                if (!p.comp.empty() && !p.layer.empty() && !p.column.empty()) s.compLayers.push_back(std::move(p));
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
