#include "vthost/core/layer_props.h"

#include <algorithm>
#include <cmath>

namespace vthost {

const char* toString(BlendMode m) {
    switch (m) {
    case BlendMode::Normal: return "normal";
    case BlendMode::Add: return "add";
    case BlendMode::Multiply: return "multiply";
    case BlendMode::Screen: return "screen";
    }
    return "normal";
}

BlendMode blendModeFromString(const std::string& s) {
    if (s == "add") return BlendMode::Add;
    if (s == "multiply") return BlendMode::Multiply;
    if (s == "screen") return BlendMode::Screen;
    return BlendMode::Normal;
}

void LayerProps::clamp() {
    auto fin = [](double v, double def) { return std::isfinite(v) ? v : def; };
    opacity = std::clamp(fin(opacity, 1.0), 0.0, 1.0);
    posX = std::clamp(fin(posX, 0.0), -100000.0, 100000.0);
    posY = std::clamp(fin(posY, 0.0), -100000.0, 100000.0);
    scale = std::clamp(fin(scale, 1.0), 0.01, 10.0);
    rotation = std::clamp(fin(rotation, 0.0), -3600.0, 3600.0);
    for (auto& c : cornerPin) {
        c.x = std::clamp(fin(c.x, 0.0), -100000.0, 100000.0);
        c.y = std::clamp(fin(c.y, 0.0), -100000.0, 100000.0);
    }
    brightness = std::clamp(fin(brightness, 0.0), -1.0, 1.0);
    contrast = std::clamp(fin(contrast, 1.0), 0.0, 3.0);
    saturation = std::clamp(fin(saturation, 1.0), 0.0, 3.0);
    gamma = std::clamp(fin(gamma, 1.0), 0.2, 5.0);
    hue = std::clamp(fin(hue, 0.0), -180.0, 180.0);
    cropLeft = std::clamp(fin(cropLeft, 0.0), 0.0, 1.0);
    cropRight = std::clamp(fin(cropRight, 0.0), 0.0, 1.0);
    cropTop = std::clamp(fin(cropTop, 0.0), 0.0, 1.0);
    cropBottom = std::clamp(fin(cropBottom, 0.0), 0.0, 1.0);
    feather = std::clamp(fin(feather, 0.0), 0.0, 0.5);
}

const char* toString(FitMode m) {
    switch (m) {
    case FitMode::Fit: return "fit";
    case FitMode::Fill: return "fill";
    case FitMode::Stretch: return "stretch";
    case FitMode::Native: return "native";
    }
    return "fit";
}

FitMode fitModeFromString(const std::string& s) {
    if (s == "fill") return FitMode::Fill;
    if (s == "stretch") return FitMode::Stretch;
    if (s == "native") return FitMode::Native;
    return FitMode::Fit;
}

LayerQuad computeLayerQuad(const LayerProps& p, int mediaW, int mediaH, int canvasW, int canvasH) {
    LayerQuad q{canvasW / 2.0, canvasH / 2.0, canvasW / 2.0, canvasH / 2.0, 0.0};
    if (mediaW > 0 && mediaH > 0 && canvasW > 0 && canvasH > 0 && p.fit != FitMode::Stretch) {
        const double sx = static_cast<double>(canvasW) / mediaW, sy = static_cast<double>(canvasH) / mediaH;
        double f = std::min(sx, sy);
        if (p.fit == FitMode::Fill) f = std::max(sx, sy);
        else if (p.fit == FitMode::Native) f = 1.0;
        q.halfW = mediaW * f / 2.0;
        q.halfH = mediaH * f / 2.0;
    }
    q.halfW *= p.scale;
    q.halfH *= p.scale;
    q.cx += p.posX;
    q.cy += p.posY;
    q.rotationRad = p.rotation * 3.14159265358979323846 / 180.0;
    return q;
}

std::array<WarpPoint, 4> computeLayerCorners(const LayerProps& p, int mediaW, int mediaH, int canvasW, int canvasH) {
    const LayerQuad q = computeLayerQuad(p, mediaW, mediaH, canvasW, canvasH);
    const double c = std::cos(q.rotationRad), s = std::sin(q.rotationRad);
    const double local[4][2] = {{-q.halfW, -q.halfH}, {q.halfW, -q.halfH}, {q.halfW, q.halfH}, {-q.halfW, q.halfH}};
    std::array<WarpPoint, 4> out{};
    for (std::size_t i = 0; i < 4; ++i) {
        out[i].x = q.cx + local[i][0] * c - local[i][1] * s + p.cornerPin[i].x;
        out[i].y = q.cy + local[i][0] * s + local[i][1] * c + p.cornerPin[i].y;
    }
    return out;
}

} // namespace vthost
