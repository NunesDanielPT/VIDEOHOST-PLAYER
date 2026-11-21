#pragma once
// (Interno ao núcleo) LayerProps <-> JSON, partilhado por projeto, estado do show
// e protocolo. Chaves "planas" (opacity, x, y...) compatíveis com versões antigas.

#include "vthost/core/project.h"

#include <nlohmann/json.hpp>

#include <cmath>

namespace vthost {

inline nlohmann::json propsToJson(const LayerProps& p) {
    using nlohmann::json;
    json pin = json::array();
    for (const auto& c : p.cornerPin) pin.push_back({c.x, c.y});
    return {{"opacity", p.opacity},       {"blend", toString(p.blend)},   {"x", p.posX},
            {"y", p.posY},                {"scale", p.scale},             {"rotation", p.rotation},
            {"fit", toString(p.fit)},     {"flipH", p.flipH},             {"flipV", p.flipV},
            {"cornerPin", pin},           {"brightness", p.brightness},   {"contrast", p.contrast},
            {"saturation", p.saturation}, {"gamma", p.gamma},             {"hue", p.hue},
            {"invert", p.invert},         {"cropLeft", p.cropLeft},       {"cropRight", p.cropRight},
            {"cropTop", p.cropTop},       {"cropBottom", p.cropBottom},   {"feather", p.feather}};
}

inline void propsFromJson(const nlohmann::json& j, LayerProps& p) {
    p = LayerProps{};
    if (!j.is_object()) return;
    auto num = [&](const char* k, double def) {
        auto it = j.find(k);
        if (it == j.end() || !it->is_number()) return def;
        const double v = it->get<double>();
        return std::isfinite(v) ? v : def;
    };
    auto flag = [&](const char* k) {
        auto it = j.find(k);
        return it != j.end() && it->is_boolean() && it->get<bool>();
    };
    auto text = [&](const char* k) {
        auto it = j.find(k);
        return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
    };
    p.opacity = num("opacity", 1.0);
    p.blend = blendModeFromString(text("blend"));
    p.posX = num("x", 0.0);
    p.posY = num("y", 0.0);
    p.scale = num("scale", 1.0);
    p.rotation = num("rotation", 0.0);
    p.fit = fitModeFromString(text("fit"));
    p.flipH = flag("flipH");
    p.flipV = flag("flipV");
    if (auto pin = j.find("cornerPin"); pin != j.end() && pin->is_array() && pin->size() == 4)
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& c = (*pin)[i];
            if (c.is_array() && c.size() == 2 && c[0].is_number() && c[1].is_number())
                p.cornerPin[i] = {c[0].get<double>(), c[1].get<double>()};
        }
    p.brightness = num("brightness", 0.0);
    p.contrast = num("contrast", 1.0);
    p.saturation = num("saturation", 1.0);
    p.gamma = num("gamma", 1.0);
    p.hue = num("hue", 0.0);
    p.invert = flag("invert");
    p.cropLeft = num("cropLeft", 0.0);
    p.cropRight = num("cropRight", 0.0);
    p.cropTop = num("cropTop", 0.0);
    p.cropBottom = num("cropBottom", 0.0);
    p.feather = num("feather", 0.0);
    p.clamp();
}

} // namespace vthost
