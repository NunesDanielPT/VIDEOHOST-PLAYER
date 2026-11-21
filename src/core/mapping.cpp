#include "vthost/core/mapping.h"

#include <algorithm>
#include <cmath>

namespace vthost {

const char* toString(WarpMode m) {
    switch (m) {
    case WarpMode::None: return "none";
    case WarpMode::Perspective: return "perspective";
    case WarpMode::Mesh: return "mesh";
    }
    return "none";
}

WarpMode warpModeFromString(const std::string& s) {
    if (s == "perspective") return WarpMode::Perspective;
    if (s == "mesh") return WarpMode::Mesh;
    return WarpMode::None;
}

void OutputMapping::resetMesh() {
    meshCols = std::clamp(meshCols, 1, kMaxMeshDivisions);
    meshRows = std::clamp(meshRows, 1, kMaxMeshDivisions);
    mesh.clear();
    for (int r = 0; r <= meshRows; ++r)
        for (int c = 0; c <= meshCols; ++c)
            mesh.push_back({static_cast<double>(c) / meshCols, static_cast<double>(r) / meshRows});
}

bool OutputMapping::meshIsValid() const {
    return meshCols >= 1 && meshRows >= 1 && meshCols <= kMaxMeshDivisions && meshRows <= kMaxMeshDivisions &&
           mesh.size() == static_cast<std::size_t>((meshCols + 1) * (meshRows + 1));
}

void OutputMapping::sanitize() {
    auto fin = [](double v, double def) { return std::isfinite(v) ? v : def; };
    srcX = std::clamp(fin(srcX, 0.0), 0.0, 100000.0);
    srcY = std::clamp(fin(srcY, 0.0), 0.0, 100000.0);
    srcW = std::clamp(fin(srcW, 0.0), 0.0, 100000.0);
    srcH = std::clamp(fin(srcH, 0.0), 0.0, 100000.0);
    for (auto& c : corners) {
        c.x = std::clamp(fin(c.x, 0.0), -4.0, 5.0);
        c.y = std::clamp(fin(c.y, 0.0), -4.0, 5.0);
    }
    if (!meshIsValid()) resetMesh();
    for (auto& p : mesh) {
        p.x = std::clamp(fin(p.x, 0.0), -4.0, 5.0);
        p.y = std::clamp(fin(p.y, 0.0), -4.0, 5.0);
    }
    blendLeft = std::clamp(fin(blendLeft, 0.0), 0.0, 10000.0);
    blendRight = std::clamp(fin(blendRight, 0.0), 0.0, 10000.0);
    blendTop = std::clamp(fin(blendTop, 0.0), 0.0, 10000.0);
    blendBottom = std::clamp(fin(blendBottom, 0.0), 0.0, 10000.0);
    blendSmoothness = std::clamp(fin(blendSmoothness, 2.0), 0.5, 8.0);
    blendGamma = std::clamp(fin(blendGamma, 2.2), 1.0, 4.0);
    brightness = std::clamp(fin(brightness, 1.0), 0.0, 2.0);
    contrast = std::clamp(fin(contrast, 1.0), 0.0, 2.0);
    gainR = std::clamp(fin(gainR, 1.0), 0.0, 2.0);
    gainG = std::clamp(fin(gainG, 1.0), 0.0, 2.0);
    gainB = std::clamp(fin(gainB, 1.0), 0.0, 2.0);
}

namespace {

// Homografia do quadrado unitário para o quadrilátero dos cantos (Heckbert).
struct Homography {
    double a, b, c, d, e, f, g, h;
};

Homography squareToQuad(const std::array<WarpPoint, 4>& q) {
    const double x0 = q[0].x, y0 = q[0].y, x1 = q[1].x, y1 = q[1].y;
    const double x2 = q[2].x, y2 = q[2].y, x3 = q[3].x, y3 = q[3].y;
    const double dx3 = x0 - x1 + x2 - x3, dy3 = y0 - y1 + y2 - y3;
    Homography H{};
    if (std::abs(dx3) < 1e-12 && std::abs(dy3) < 1e-12) {
        H = {x1 - x0, x3 - x0, x0, y1 - y0, y3 - y0, y0, 0.0, 0.0};
    } else {
        const double dx1 = x1 - x2, dx2 = x3 - x2, dy1 = y1 - y2, dy2 = y3 - y2;
        double det = dx1 * dy2 - dx2 * dy1;
        if (std::abs(det) < 1e-12) det = 1e-12; // cantos degenerados: evita divisão por zero
        const double g = (dx3 * dy2 - dx2 * dy3) / det;
        const double h = (dx1 * dy3 - dx3 * dy1) / det;
        H = {x1 - x0 + g * x1, x3 - x0 + h * x3, x0, y1 - y0 + g * y1, y3 - y0 + h * y3, y0, g, h};
    }
    return H;
}

WarpPoint applyH(const Homography& H, double u, double v) {
    double w = H.g * u + H.h * v + 1.0;
    if (std::abs(w) < 1e-9) w = 1e-9;
    return {(H.a * u + H.b * v + H.c) / w, (H.d * u + H.e * v + H.f) / w};
}

double catmull(double p0, double p1, double p2, double p3, double t) {
    const double t2 = t * t, t3 = t2 * t;
    return 0.5 * ((2.0 * p1) + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                  (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

// Ponto da malha com pontos-fantasma extrapolados nas bordas (grelha regular => identidade exata).
WarpPoint meshAt(const OutputMapping& m, int c, int r) {
    auto raw = [&](int cc, int rr) {
        return m.mesh[static_cast<std::size_t>(rr * (m.meshCols + 1) + cc)];
    };
    auto clampedCol = [&](int cc, int rr) -> WarpPoint {
        if (cc < 0) {
            const WarpPoint a = raw(0, rr), b = raw(1, rr);
            return {2 * a.x - b.x, 2 * a.y - b.y};
        }
        if (cc > m.meshCols) {
            const WarpPoint a = raw(m.meshCols, rr), b = raw(m.meshCols - 1, rr);
            return {2 * a.x - b.x, 2 * a.y - b.y};
        }
        return raw(cc, rr);
    };
    if (r < 0) {
        const WarpPoint a = clampedCol(c, 0), b = clampedCol(c, 1);
        return {2 * a.x - b.x, 2 * a.y - b.y};
    }
    if (r > m.meshRows) {
        const WarpPoint a = clampedCol(c, m.meshRows), b = clampedCol(c, m.meshRows - 1);
        return {2 * a.x - b.x, 2 * a.y - b.y};
    }
    return clampedCol(c, r);
}

WarpPoint meshInterp(const OutputMapping& m, double u, double v) {
    const double fu = std::clamp(u, 0.0, 1.0) * m.meshCols;
    const double fv = std::clamp(v, 0.0, 1.0) * m.meshRows;
    const int i = std::min(static_cast<int>(fu), m.meshCols - 1);
    const int j = std::min(static_cast<int>(fv), m.meshRows - 1);
    const double tu = fu - i, tv = fv - j;
    double xs[4], ys[4];
    for (int k = 0; k < 4; ++k) {
        const int r = j - 1 + k;
        const WarpPoint p0 = meshAt(m, i - 1, r), p1 = meshAt(m, i, r), p2 = meshAt(m, i + 1, r), p3 = meshAt(m, i + 2, r);
        xs[k] = catmull(p0.x, p1.x, p2.x, p3.x, tu);
        ys[k] = catmull(p0.y, p1.y, p2.y, p3.y, tu);
    }
    return {catmull(xs[0], xs[1], xs[2], xs[3], tv), catmull(ys[0], ys[1], ys[2], ys[3], tv)};
}

double edgeCurve(double x, double p, double gamma) {
    x = std::clamp(x, 0.0, 1.0);
    // Curva em S (Bourke): as duas metades somam 1 na zona de sobreposição.
    const double c = x < 0.5 ? 0.5 * std::pow(2.0 * x, p) : 1.0 - 0.5 * std::pow(2.0 * (1.0 - x), p);
    return std::pow(c, 1.0 / gamma);
}

} // namespace

WarpPoint warpPoint(const OutputMapping& m, double u, double v) {
    switch (m.mode) {
    case WarpMode::None: return {u, v};
    case WarpMode::Perspective: return applyH(squareToQuad(m.corners), u, v);
    case WarpMode::Mesh: {
        const WarpPoint q = m.meshIsValid() ? meshInterp(m, u, v) : WarpPoint{u, v};
        return applyH(squareToQuad(m.corners), q.x, q.y);
    }
    }
    return {u, v};
}

bool unwarpPerspective(const OutputMapping& m, const WarpPoint& out, WarpPoint& meshSpace) {
    if (m.mode == WarpMode::None) {
        meshSpace = out;
        return true;
    }
    const Homography H = squareToQuad(m.corners);
    // Inversa da matriz [[a b c][d e f][g h 1]].
    const double A = H.a, B = H.b, C = H.c, D = H.d, E = H.e, F = H.f, G = H.g, Hh = H.h;
    const double det = A * (E - F * Hh) - B * (D - F * G) + C * (D * Hh - E * G);
    if (std::abs(det) < 1e-12) return false;
    const double i00 = (E - F * Hh) / det, i01 = (C * Hh - B) / det, i02 = (B * F - C * E) / det;
    const double i10 = (F * G - D) / det, i11 = (A - C * G) / det, i12 = (C * D - A * F) / det;
    const double i20 = (D * Hh - E * G) / det, i21 = (B * G - A * Hh) / det, i22 = (A * E - B * D) / det;
    const double w = i20 * out.x + i21 * out.y + i22;
    if (std::abs(w) < 1e-12) return false;
    meshSpace = {(i00 * out.x + i01 * out.y + i02) / w, (i10 * out.x + i11 * out.y + i12) / w};
    return std::isfinite(meshSpace.x) && std::isfinite(meshSpace.y);
}

double softEdgeFactor(const OutputMapping& m, double u, double v, int outW, int outH) {
    if (outW <= 0 || outH <= 0) return 1.0;
    double f = 1.0;
    const double p = m.blendSmoothness, g = m.blendGamma;
    if (m.blendLeft > 0) f *= edgeCurve(u * outW / m.blendLeft, p, g);
    if (m.blendRight > 0) f *= edgeCurve((1.0 - u) * outW / m.blendRight, p, g);
    if (m.blendTop > 0) f *= edgeCurve(v * outH / m.blendTop, p, g);
    if (m.blendBottom > 0) f *= edgeCurve((1.0 - v) * outH / m.blendBottom, p, g);
    return std::clamp(f, 0.0, 1.0);
}

std::vector<WarpVertex> buildWarpGrid(const OutputMapping& m, int div) {
    div = std::clamp(div, 1, 128);
    std::vector<WarpPoint> pts(static_cast<std::size_t>((div + 1) * (div + 1)));
    for (int r = 0; r <= div; ++r)
        for (int c = 0; c <= div; ++c)
            pts[static_cast<std::size_t>(r * (div + 1) + c)] =
                warpPoint(m, static_cast<double>(c) / div, static_cast<double>(r) / div);
    std::vector<WarpVertex> out;
    out.reserve(static_cast<std::size_t>(div * div * 6));
    auto vert = [&](int c, int r) {
        const WarpPoint& p = pts[static_cast<std::size_t>(r * (div + 1) + c)];
        return WarpVertex{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(c) / div,
                          static_cast<float>(r) / div};
    };
    for (int r = 0; r < div; ++r)
        for (int c = 0; c < div; ++c) {
            out.push_back(vert(c, r));
            out.push_back(vert(c + 1, r));
            out.push_back(vert(c, r + 1));
            out.push_back(vert(c + 1, r));
            out.push_back(vert(c + 1, r + 1));
            out.push_back(vert(c, r + 1));
        }
    return out;
}

std::vector<LayoutCell> computeLayout(int cols, int rows, int outW, int outH, int overlapH, int overlapV,
                                      int& canvasW, int& canvasH) {
    cols = std::clamp(cols, 1, 16);
    rows = std::clamp(rows, 1, 16);
    outW = std::clamp(outW, 16, 16384);
    outH = std::clamp(outH, 16, 16384);
    overlapH = std::clamp(overlapH, 0, outW / 2);
    overlapV = std::clamp(overlapV, 0, outH / 2);
    canvasW = cols * outW - (cols - 1) * overlapH;
    canvasH = rows * outH - (rows - 1) * overlapV;
    std::vector<LayoutCell> cells;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            LayoutCell cell{c, r, {}};
            OutputMapping& m = cell.mapping;
            m.srcX = c * (outW - overlapH);
            m.srcY = r * (outH - overlapV);
            m.srcW = outW;
            m.srcH = outH;
            m.blendLeft = c > 0 ? overlapH : 0;
            m.blendRight = c < cols - 1 ? overlapH : 0;
            m.blendTop = r > 0 ? overlapV : 0;
            m.blendBottom = r < rows - 1 ? overlapV : 0;
            m.resetMesh();
            cells.push_back(cell);
        }
    return cells;
}

} // namespace vthost
