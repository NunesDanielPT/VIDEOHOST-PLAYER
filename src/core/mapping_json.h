#pragma once
// (Interno ao núcleo) OutputMapping <-> JSON, partilhado por projeto e protocolo.

#include "vthost/core/mapping.h"

#include <nlohmann/json.hpp>

#include <cmath>

namespace vthost {

inline nlohmann::json mappingToJson(const OutputMapping& m) {
    using nlohmann::json;
    json corners = json::array();
    for (const auto& c : m.corners) corners.push_back({c.x, c.y});
    json mesh = json::array();
    for (const auto& pt : m.mesh) mesh.push_back({pt.x, pt.y});
    return {{"x", m.srcX}, {"y", m.srcY}, {"w", m.srcW}, {"h", m.srcH}, {"warp", toString(m.mode)},
            {"corners", corners}, {"meshCols", m.meshCols}, {"meshRows", m.meshRows}, {"mesh", mesh},
            {"blendLeft", m.blendLeft}, {"blendRight", m.blendRight}, {"blendTop", m.blendTop},
            {"blendBottom", m.blendBottom}, {"blendSmoothness", m.blendSmoothness}, {"blendGamma", m.blendGamma},
            {"brightness", m.brightness}};
}

// Tolerante: campos em falta ficam com o valor por omissão; no fim é sempre sanitizado.
inline void mappingFromJson(const nlohmann::json& j, OutputMapping& m) {
    m = OutputMapping{};
    if (!j.is_object()) {
        m.sanitize();
        return;
    }
    auto num = [&](const char* k, double def) {
        auto it = j.find(k);
        if (it == j.end() || !it->is_number()) return def;
        const double v = it->get<double>();
        return std::isfinite(v) ? v : def;
    };
    auto pt = [](const nlohmann::json& p, WarpPoint& out) {
        if (p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number()) {
            out = {p[0].get<double>(), p[1].get<double>()};
            return true;
        }
        return false;
    };
    m.srcX = num("x", 0.0);
    m.srcY = num("y", 0.0);
    m.srcW = num("w", 0.0);
    m.srcH = num("h", 0.0);
    if (auto w = j.find("warp"); w != j.end() && w->is_string()) m.mode = warpModeFromString(w->get<std::string>());
    if (auto cs = j.find("corners"); cs != j.end() && cs->is_array() && cs->size() == 4)
        for (std::size_t k = 0; k < 4; ++k) pt((*cs)[k], m.corners[k]);
    m.meshCols = static_cast<int>(num("meshCols", 2));
    m.meshRows = static_cast<int>(num("meshRows", 2));
    if (auto ms = j.find("mesh"); ms != j.end() && ms->is_array())
        for (const auto& p : *ms) {
            WarpPoint w;
            if (pt(p, w)) m.mesh.push_back(w);
        }
    m.blendLeft = num("blendLeft", 0.0);
    m.blendRight = num("blendRight", 0.0);
    m.blendTop = num("blendTop", 0.0);
    m.blendBottom = num("blendBottom", 0.0);
    m.blendSmoothness = num("blendSmoothness", 2.0);
    m.blendGamma = num("blendGamma", 2.2);
    m.brightness = num("brightness", 1.0);
    m.sanitize();
}

} // namespace vthost
