#include "vthost/core/project.h"

#include "vthost/core/atomic_file.h"

#include <algorithm>
#include <cmath>
#include <set>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace vthost {

namespace {
double clampFinite(double v, double lo, double hi, double def) {
    if (!std::isfinite(v)) return def;
    return std::clamp(v, lo, hi);
}
} // namespace

bool isValidCueColor(const std::string& c) {
    if (c.empty()) return true;
    for (const char* k : kCueColors)
        if (c == k) return true;
    return false;
}

std::string Cue::displayName() const {
    if (!name.empty()) return name;
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

double Cue::effectiveOut(double fileDuration) const {
    const double d = (std::isfinite(fileDuration) && fileDuration > 0.0) ? fileDuration : 0.0;
    if (outPoint > inPoint && (d <= 0.0 || outPoint <= d)) return outPoint;
    return d;
}

double Cue::passSeconds(double fileDuration) const {
    const double out = effectiveOut(fileDuration);
    if (out <= inPoint) return 0.0;
    return (out - inPoint) / std::max(0.01, speed);
}

double Cue::totalSeconds(double fileDuration) const {
    if (playCount <= 0) return 0.0;
    return passSeconds(fileDuration) * playCount;
}

double normalizationGainDb(double loudnessLufs, std::optional<double> peakDb, double targetLufs) {
    if (!std::isfinite(loudnessLufs) || loudnessLufs < -70.0) return 0.0; // silêncio: não mexe
    double g = std::clamp(targetLufs - loudnessLufs, -20.0, 20.0);
    if (peakDb && std::isfinite(*peakDb)) g = std::min(g, std::max(-20.0, -1.0 - *peakDb));
    return g;
}

double Cue::gainDb(double normalizeTargetLufs) const {
    double g = volumeDb;
    if (normalize && loudness) g += normalizationGainDb(*loudness, peakDb, normalizeTargetLufs);
    return g;
}

void Cue::clamp() {
    inPoint = clampFinite(inPoint, 0.0, kMaxMediaSeconds, 0.0);
    outPoint = clampFinite(outPoint, 0.0, kMaxMediaSeconds, 0.0);
    if (outPoint > 0.0 && outPoint <= inPoint + 0.04) outPoint = 0.0; // trim inválido: até ao fim
    speed = clampFinite(speed, 0.25, 4.0, 1.0);
    playCount = std::clamp(playCount, 0, 9999);
    fadeIn = clampFinite(fadeIn, 0.0, 60.0, 0.0);
    fadeOut = clampFinite(fadeOut, 0.0, 60.0, 0.0);
    crossfade = clampFinite(crossfade, 0.0, 60.0, 0.0);
    volumeDb = clampFinite(volumeDb, -60.0, 12.0, 0.0);
    if (loudness && (!std::isfinite(*loudness) || *loudness < -100.0 || *loudness > 10.0)) loudness.reset();
    if (peakDb && (!std::isfinite(*peakDb) || *peakDb < -200.0 || *peakDb > 30.0)) peakDb.reset();
    scale = clampFinite(scale, 0.01, 10.0, 1.0);
    posX = clampFinite(posX, -100000.0, 100000.0, 0.0);
    posY = clampFinite(posY, -100000.0, 100000.0, 0.0);
    if (!isValidCueColor(color)) color.clear();
    if (thumbTime && (!std::isfinite(*thumbTime) || *thumbTime < 0.0 || *thumbTime > kMaxMediaSeconds)) thumbTime.reset();
    if (label.size() > 32) label.resize(32);
}

const char* toString(OutputType t) {
    switch (t) {
    case OutputType::Display: return "display";
    case OutputType::Window: return "window";
    case OutputType::Ndi: return "ndi";
    case OutputType::DeckLink: return "decklink";
    }
    return "display";
}

OutputType outputTypeFromString(const std::string& s) {
    if (s == "window") return OutputType::Window;
    if (s == "ndi") return OutputType::Ndi;
    if (s == "decklink") return OutputType::DeckLink;
    return OutputType::Display;
}

const Cue* Project::findCue(const std::string& id) const {
    const int i = cueIndex(id);
    return i >= 0 ? &cues[static_cast<std::size_t>(i)] : nullptr;
}

int Project::cueIndex(const std::string& id) const {
    if (id.empty()) return -1;
    for (std::size_t i = 0; i < cues.size(); ++i)
        if (cues[i].id == id) return static_cast<int>(i);
    return -1;
}

int Project::nextCue(int current) const {
    const int n = static_cast<int>(cues.size());
    if (n == 0) return -1;
    if (current < 0 || current >= n) return 0;
    if (const int j = cueIndex(cues[static_cast<std::size_t>(current)].gotoCue); j >= 0) return j;
    if (current + 1 < n) return current + 1;
    return loopPlaylist ? 0 : -1;
}

std::string Project::newCueId() const {
    for (int i = 1;; ++i) {
        std::string id = "Q" + std::to_string(i);
        if (cueIndex(id) < 0) return id;
    }
}

std::string Project::newOutputId() const {
    for (int i = 1;; ++i) {
        std::string id = "O" + std::to_string(i);
        bool used = false;
        for (const auto& o : outputs) used = used || o.id == id;
        if (!used) return id;
    }
}

const Composition* Project::findComposition(const std::string& id) const {
    for (const auto& c : compositions)
        if (c.id == id) return &c;
    return nullptr;
}

std::string Project::newCompositionId() const {
    for (int i = 1;; ++i) {
        std::string id = "comp" + std::to_string(i);
        if (!findComposition(id)) return id;
    }
}

Project Project::makeDefault() {
    Project p;
    Output o;
    o.id = "O1";
    o.name = "Output 1";
    o.type = OutputType::Display;
    o.monitor = 1; // segundo ecrã (se não existir, abre janela no principal)
    Slice full = makeFullSlice(SliceSourceKind::MediaPlayer, o.width, o.height);
    full.keepAspect = true;
    o.slices.push_back(full);
    p.outputs.push_back(o);
    p.compositions.push_back(Composition::makeDefault("comp1", "Composition 1"));
    return p;
}

// ---------------------------------------------------------------- slices

const char* toString(SliceSourceKind k) {
    switch (k) {
    case SliceSourceKind::MediaPlayer: return "mediaplayer";
    case SliceSourceKind::Composition: return "composition";
    case SliceSourceKind::Layer: return "layer";
    }
    return "mediaplayer";
}

SliceSourceKind sliceSourceFromString(const std::string& s) {
    if (s == "composition") return SliceSourceKind::Composition;
    if (s == "layer") return SliceSourceKind::Layer;
    return SliceSourceKind::MediaPlayer;
}

void Slice::clamp() {
    auto c = [](double& v, double lo, double hi, double def) { v = clampFinite(v, lo, hi, def); };
    c(inX, 0.0, kMaxCanvasSize, 0.0);
    c(inY, 0.0, kMaxCanvasSize, 0.0);
    c(inW, 0.0, kMaxCanvasSize, 0.0);
    c(inH, 0.0, kMaxCanvasSize, 0.0);
    c(x, -4.0 * kMaxCanvasSize, 4.0 * kMaxCanvasSize, 960.0);
    c(y, -4.0 * kMaxCanvasSize, 4.0 * kMaxCanvasSize, 540.0);
    c(width, 1.0, 4.0 * kMaxCanvasSize, 1920.0);
    c(height, 1.0, 4.0 * kMaxCanvasSize, 1080.0);
    c(rotation, -3600.0, 3600.0, 0.0);
    for (auto& p : cornerOffsets) {
        c(p.x, -4.0 * kMaxCanvasSize, 4.0 * kMaxCanvasSize, 0.0);
        c(p.y, -4.0 * kMaxCanvasSize, 4.0 * kMaxCanvasSize, 0.0);
    }
    meshCols = std::clamp(meshCols, 0, kMaxMeshDivisions);
    meshRows = std::clamp(meshRows, 0, kMaxMeshDivisions);
    if (meshCols == 0 || meshRows == 0) {
        meshCols = meshRows = 0;
        mesh.clear();
    } else if (mesh.size() != static_cast<std::size_t>((meshCols + 1) * (meshRows + 1))) {
        mesh.clear();
        for (int r = 0; r <= meshRows; ++r)
            for (int k = 0; k <= meshCols; ++k)
                mesh.push_back({static_cast<double>(k) / meshCols, static_cast<double>(r) / meshRows});
    }
    for (auto& p : mesh) {
        c(p.x, -2.0, 3.0, 0.0);
        c(p.y, -2.0, 3.0, 0.0);
    }
    for (double* v : {&brightness, &contrast, &red, &green, &blue}) c(*v, -1.0, 1.0, 0.0);
    for (double* v : {&edgeLeft, &edgeRight, &edgeTop, &edgeBottom}) c(*v, 0.0, 4.0 * kMaxCanvasSize, 0.0);
    c(edgeGamma, 0.2, 5.0, 2.2);
    c(edgePower, 0.5, 8.0, 2.0);
    if (source == SliceSourceKind::MediaPlayer) {
        compositionId.clear();
        layerId.clear();
    } else if (source == SliceSourceKind::Composition) {
        layerId.clear();
    }
}

Slice makeFullSlice(SliceSourceKind source, int screenW, int screenH, const std::string& id) {
    Slice s;
    s.id = id;
    s.name = "Slice 1";
    s.source = source;
    s.x = screenW / 2.0;
    s.y = screenH / 2.0;
    s.width = screenW;
    s.height = screenH;
    return s;
}

OutputMapping sliceToMapping(const Slice& s, int screenW, int screenH, int sourceW, int sourceH) {
    OutputMapping m;
    const double sw = std::max(1, screenW), sh = std::max(1, screenH);
    const double srcW = std::max(1, sourceW), srcH = std::max(1, sourceH);
    // Input Selection: recorte dentro da fonte.
    if (s.inW > 0.0 && s.inH > 0.0) {
        m.srcX = std::clamp(s.inX, 0.0, srcW - 1.0);
        m.srcY = std::clamp(s.inY, 0.0, srcH - 1.0);
        m.srcW = std::clamp(s.inW, 1.0, srcW - m.srcX);
        m.srcH = std::clamp(s.inH, 1.0, srcH - m.srcY);
    }
    const double regionW = m.srcW > 0.0 ? m.srcW : srcW;
    const double regionH = m.srcH > 0.0 ? m.srcH : srcH;
    // Retângulo do slice (com a proporção da fonte, se pedido).
    double w = s.width, h = s.height;
    if (s.keepAspect && regionW > 0.0 && regionH > 0.0) {
        const double a = regionW / regionH;
        if (w / h > a) w = h * a;
        else h = w / a;
    }
    const double rad = s.rotation * 3.14159265358979323846 / 180.0;
    const double cs = std::cos(rad), sn = std::sin(rad);
    const WarpPoint local[4] = {{-w / 2, -h / 2}, {w / 2, -h / 2}, {w / 2, h / 2}, {-w / 2, h / 2}};
    WarpPoint screen[4];
    for (int i = 0; i < 4; ++i) {
        screen[i].x = s.x + local[i].x * cs - local[i].y * sn + s.cornerOffsets[static_cast<std::size_t>(i)].x;
        screen[i].y = s.y + local[i].x * sn + local[i].y * cs + s.cornerOffsets[static_cast<std::size_t>(i)].y;
    }
    // Espelho: troca os cantos onde cada canto da imagem aparece.
    int order[4] = {0, 1, 2, 3};
    if (s.flipH && s.flipV) order[0] = 2, order[1] = 3, order[2] = 0, order[3] = 1;
    else if (s.flipH) order[0] = 1, order[1] = 0, order[2] = 3, order[3] = 2;
    else if (s.flipV) order[0] = 3, order[1] = 2, order[2] = 1, order[3] = 0;
    for (int i = 0; i < 4; ++i)
        m.corners[static_cast<std::size_t>(i)] = {screen[order[i]].x / sw, screen[order[i]].y / sh};
    if (s.meshCols > 0 && s.meshRows > 0 &&
        s.mesh.size() == static_cast<std::size_t>((s.meshCols + 1) * (s.meshRows + 1))) {
        m.mode = WarpMode::Mesh;
        m.meshCols = s.meshCols;
        m.meshRows = s.meshRows;
        m.mesh = s.mesh;
    } else {
        m.mode = WarpMode::Perspective;
        m.resetMesh();
    }
    // Soft edge: em px do slice; o motor normaliza pela largura do screen.
    if (s.softEdge) {
        m.blendLeft = s.edgeLeft / w * sw;
        m.blendRight = s.edgeRight / w * sw;
        m.blendTop = s.edgeTop / h * sh;
        m.blendBottom = s.edgeBottom / h * sh;
    }
    m.blendGamma = s.edgeGamma;
    m.blendSmoothness = s.edgePower;
    m.brightness = 1.0 + s.brightness;
    m.contrast = 1.0 + s.contrast;
    m.gainR = 1.0 + s.red;
    m.gainG = 1.0 + s.green;
    m.gainB = 1.0 + s.blue;
    m.sanitize();
    return m;
}

// ---------------------------------------------------------------- CUEPLAYER

const char* toString(ClipEnd e) {
    switch (e) {
    case ClipEnd::Loop: return "loop";
    case ClipEnd::Hold: return "hold";
    case ClipEnd::Clear: return "clear";
    }
    return "loop";
}

ClipEnd clipEndFromString(const std::string& s) {
    if (s == "hold") return ClipEnd::Hold;
    if (s == "clear") return ClipEnd::Clear;
    return ClipEnd::Loop;
}

std::string Clip::displayName() const {
    if (!name.empty()) return name;
    const std::size_t slash = path.find_last_of("/\\");
    std::string n = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::size_t dot = n.find_last_of('.');
    if (dot != std::string::npos && dot > 0) n.resize(dot);
    return n;
}

void Clip::clamp() {
    inPoint = clampFinite(inPoint, 0.0, kMaxMediaSeconds, 0.0);
    outPoint = clampFinite(outPoint, 0.0, kMaxMediaSeconds, 0.0);
    if (outPoint > 0.0 && outPoint <= inPoint + 0.04) outPoint = 0.0;
    speed = clampFinite(speed, 0.25, 4.0, 1.0);
    volumeDb = clampFinite(volumeDb, -60.0, 12.0, 0.0);
    if (loudness && (!std::isfinite(*loudness) || *loudness < -100.0 || *loudness > 10.0)) loudness.reset();
    if (peakDb && (!std::isfinite(*peakDb) || *peakDb < -200.0 || *peakDb > 30.0)) peakDb.reset();
    if (thumbTime && (!std::isfinite(*thumbTime) || *thumbTime < 0.0 || *thumbTime > kMaxMediaSeconds)) thumbTime.reset();
}

void CompLayer::clamp() {
    props.clamp();
    transition = clampFinite(transition, 0.0, 60.0, 0.0);
    volumeDb = clampFinite(volumeDb, -60.0, 12.0, 0.0);
}

const CompLayer* Composition::findLayer(const std::string& lid) const {
    for (const auto& l : layers)
        if (l.id == lid) return &l;
    return nullptr;
}

const CompColumn* Composition::findColumn(const std::string& cid) const {
    for (const auto& c : columns)
        if (c.id == cid) return &c;
    return nullptr;
}

std::string Composition::newLayerId() const {
    for (int i = 1;; ++i) {
        std::string lid = "L" + std::to_string(i);
        if (!findLayer(lid)) return lid;
    }
}

std::string Composition::newColumnId() const {
    for (int i = 1;; ++i) {
        std::string cid = "K" + std::to_string(i);
        if (!findColumn(cid)) return cid;
    }
}

Composition Composition::makeDefault(const std::string& id, const std::string& name) {
    Composition c;
    c.id = id;
    c.name = name;
    for (int i = 1; i <= 3; ++i) {
        CompLayer l;
        l.id = "L" + std::to_string(i);
        l.name = "Layer " + std::to_string(i);
        c.layers.push_back(l);
    }
    for (int i = 1; i <= 9; ++i) c.columns.push_back({"K" + std::to_string(i), "Column " + std::to_string(i), {}});
    return c;
}

Cue clipToCue(const Clip& clip, const CompLayer& layer, const Composition& comp) {
    Cue q;
    q.id = comp.id + "/" + layer.id;
    q.path = clip.path;
    q.name = clip.displayName();
    q.inPoint = clip.inPoint;
    q.outPoint = clip.outPoint;
    q.speed = clip.speed;
    q.playCount = clip.end == ClipEnd::Loop ? 0 : 1;
    q.pauseAtEnd = clip.end == ClipEnd::Hold;
    q.clearAtEnd = clip.end == ClipEnd::Clear;
    q.crossfade = layer.transition;
    q.volumeDb = clip.volumeDb + layer.volumeDb + comp.volumeDb;
    q.muted = !layer.audio || layer.bypass;
    q.normalize = clip.normalize;
    q.loudness = clip.loudness;
    q.peakDb = clip.peakDb;
    q.fit = layer.props.fit;
    q.scale = layer.props.scale;
    q.posX = layer.props.posX;
    q.posY = layer.props.posY;
    q.clamp();
    return q;
}

namespace {

// Leitores tolerantes: devolvem o valor por omissão se o campo faltar ou tiver o tipo errado.
std::string getString(const json& j, const char* key, const std::string& def = {}) {
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : def;
}

double getNumber(const json& j, const char* key, double def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return def;
    const double v = it->get<double>();
    return std::isfinite(v) ? v : def;
}

std::optional<double> getOptNumber(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return std::nullopt;
    const double v = it->get<double>();
    if (!std::isfinite(v)) return std::nullopt;
    return v;
}

int getInt(const json& j, const char* key, int def) {
    const double v = getNumber(j, key, def);
    if (v < -1e9 || v > 1e9) return def;
    return static_cast<int>(v);
}

bool getBool(const json& j, const char* key, bool def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_boolean()) ? it->get<bool>() : def;
}

Cue cueFromJson(const json& c) {
    Cue q;
    q.id = getString(c, "id");
    q.label = getString(c, "label");
    q.name = getString(c, "name");
    q.path = getString(c, "path");
    q.color = getString(c, "color");
    q.notes = getString(c, "notes");
    q.thumbTime = getOptNumber(c, "thumb");
    q.inPoint = getNumber(c, "in", 0.0);
    q.outPoint = getNumber(c, "out", 0.0);
    q.speed = getNumber(c, "speed", 1.0);
    q.playCount = getInt(c, "playCount", 1);
    q.pauseAtStart = getBool(c, "pauseAtStart", false);
    q.pauseAtEnd = getBool(c, "pauseAtEnd", false);
    q.clearAtEnd = getBool(c, "clearAtEnd", false);
    q.fadeIn = getNumber(c, "fadeIn", 0.0);
    q.fadeOut = getNumber(c, "fadeOut", 0.0);
    q.crossfade = getNumber(c, "crossfade", 0.0);
    q.gotoCue = getString(c, "goto");
    q.muted = getBool(c, "muted", false);
    q.volumeDb = getNumber(c, "volumeDb", 0.0);
    q.normalize = getBool(c, "normalize", true);
    q.loudness = getOptNumber(c, "loudness");
    q.peakDb = getOptNumber(c, "peakDb");
    q.fit = fitModeFromString(getString(c, "fit", "fit"));
    q.scale = getNumber(c, "scale", 1.0);
    q.posX = getNumber(c, "x", 0.0);
    q.posY = getNumber(c, "y", 0.0);
    q.clamp();
    return q;
}

json cueToJson(const Cue& q) {
    json c{{"id", q.id},
           {"label", q.label},
           {"name", q.name},
           {"path", q.path},
           {"color", q.color},
           {"notes", q.notes},
           {"in", q.inPoint},
           {"out", q.outPoint},
           {"speed", q.speed},
           {"playCount", q.playCount},
           {"pauseAtStart", q.pauseAtStart},
           {"pauseAtEnd", q.pauseAtEnd},
           {"clearAtEnd", q.clearAtEnd},
           {"fadeIn", q.fadeIn},
           {"fadeOut", q.fadeOut},
           {"crossfade", q.crossfade},
           {"goto", q.gotoCue},
           {"muted", q.muted},
           {"volumeDb", q.volumeDb},
           {"normalize", q.normalize},
           {"fit", toString(q.fit)},
           {"scale", q.scale},
           {"x", q.posX},
           {"y", q.posY}};
    if (q.loudness) c["loudness"] = *q.loudness;
    if (q.peakDb) c["peakDb"] = *q.peakDb;
    if (q.thumbTime) c["thumb"] = *q.thumbTime;
    return c;
}

json propsToJson(const LayerProps& p) {
    return {{"opacity", p.opacity}, {"blend", toString(p.blend)}, {"x", p.posX},        {"y", p.posY},
            {"scale", p.scale},     {"rotation", p.rotation},     {"fit", toString(p.fit)}, {"flipH", p.flipH},
            {"flipV", p.flipV}};
}

LayerProps propsFromJson(const json& j) {
    LayerProps p;
    p.opacity = getNumber(j, "opacity", 1.0);
    p.blend = blendModeFromString(getString(j, "blend", "normal"));
    p.posX = getNumber(j, "x", 0.0);
    p.posY = getNumber(j, "y", 0.0);
    p.scale = getNumber(j, "scale", 1.0);
    p.rotation = getNumber(j, "rotation", 0.0);
    p.fit = fitModeFromString(getString(j, "fit", "fit"));
    p.flipH = getBool(j, "flipH", false);
    p.flipV = getBool(j, "flipV", false);
    p.clamp();
    return p;
}

json pointToJson(const WarpPoint& p) { return json::array({p.x, p.y}); }
WarpPoint pointFromJson(const json& j) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) return {};
    return {j[0].get<double>(), j[1].get<double>()};
}

json sliceToJson(const Slice& s) {
    json corners = json::array();
    for (const auto& c : s.cornerOffsets) corners.push_back(pointToJson(c));
    json mesh = json::array();
    for (const auto& p : s.mesh) mesh.push_back(pointToJson(p));
    return {{"id", s.id},           {"name", s.name},           {"enabled", s.enabled},
            {"source", toString(s.source)}, {"composition", s.compositionId}, {"layer", s.layerId},
            {"inX", s.inX},         {"inY", s.inY},             {"inW", s.inW},
            {"inH", s.inH},         {"x", s.x},                 {"y", s.y},
            {"width", s.width},     {"height", s.height},       {"rotation", s.rotation},
            {"flipH", s.flipH},     {"flipV", s.flipV},         {"keepAspect", s.keepAspect},
            {"corners", corners},   {"meshCols", s.meshCols},   {"meshRows", s.meshRows},
            {"mesh", mesh},         {"brightness", s.brightness}, {"contrast", s.contrast},
            {"red", s.red},         {"green", s.green},         {"blue", s.blue},
            {"softEdge", s.softEdge}, {"edgeLeft", s.edgeLeft}, {"edgeRight", s.edgeRight},
            {"edgeTop", s.edgeTop}, {"edgeBottom", s.edgeBottom}, {"edgeGamma", s.edgeGamma},
            {"edgePower", s.edgePower}};
}

Slice sliceFromJson(const json& j) {
    Slice s;
    s.id = getString(j, "id");
    s.name = getString(j, "name", s.id);
    s.enabled = getBool(j, "enabled", true);
    s.source = sliceSourceFromString(getString(j, "source", "mediaplayer"));
    s.compositionId = getString(j, "composition");
    s.layerId = getString(j, "layer");
    s.inX = getNumber(j, "inX", 0.0);
    s.inY = getNumber(j, "inY", 0.0);
    s.inW = getNumber(j, "inW", 0.0);
    s.inH = getNumber(j, "inH", 0.0);
    s.x = getNumber(j, "x", 960.0);
    s.y = getNumber(j, "y", 540.0);
    s.width = getNumber(j, "width", 1920.0);
    s.height = getNumber(j, "height", 1080.0);
    s.rotation = getNumber(j, "rotation", 0.0);
    s.flipH = getBool(j, "flipH", false);
    s.flipV = getBool(j, "flipV", false);
    s.keepAspect = getBool(j, "keepAspect", false);
    if (auto c = j.find("corners"); c != j.end() && c->is_array() && c->size() == 4)
        for (std::size_t i = 0; i < 4; ++i) s.cornerOffsets[i] = pointFromJson((*c)[i]);
    s.meshCols = getInt(j, "meshCols", 0);
    s.meshRows = getInt(j, "meshRows", 0);
    if (auto m = j.find("mesh"); m != j.end() && m->is_array() && m->size() <= 17u * 17u)
        for (const auto& p : *m) s.mesh.push_back(pointFromJson(p));
    s.brightness = getNumber(j, "brightness", 0.0);
    s.contrast = getNumber(j, "contrast", 0.0);
    s.red = getNumber(j, "red", 0.0);
    s.green = getNumber(j, "green", 0.0);
    s.blue = getNumber(j, "blue", 0.0);
    s.softEdge = getBool(j, "softEdge", false);
    s.edgeLeft = getNumber(j, "edgeLeft", 0.0);
    s.edgeRight = getNumber(j, "edgeRight", 0.0);
    s.edgeTop = getNumber(j, "edgeTop", 0.0);
    s.edgeBottom = getNumber(j, "edgeBottom", 0.0);
    s.edgeGamma = getNumber(j, "edgeGamma", 2.2);
    s.edgePower = getNumber(j, "edgePower", 2.0);
    s.clamp();
    return s;
}

json outputToJsonObj(const Output& o) {
    json slices = json::array();
    for (const auto& s : o.slices) slices.push_back(sliceToJson(s));
    return {{"id", o.id},
            {"name", o.name},
            {"type", toString(o.type)},
            {"enabled", o.enabled},
            {"display", o.displayId},
            {"monitor", o.monitor},
            {"windowWidth", o.windowWidth},
            {"windowHeight", o.windowHeight},
            {"ndiName", o.ndiName},
            {"ndiAlpha", o.ndiAlpha},
            {"deviceId", o.deviceId},
            {"width", o.width},
            {"height", o.height},
            {"slices", slices}};
}

Output outputFromJsonObj(const json& o) {
    Output it;
    it.id = getString(o, "id");
    it.name = getString(o, "name", it.id);
    it.type = outputTypeFromString(getString(o, "type", "display"));
    it.enabled = getBool(o, "enabled", true);
    it.displayId = getString(o, "display");
    it.monitor = std::clamp(getInt(o, "monitor", 0), 0, 16);
    it.windowWidth = std::clamp(getInt(o, "windowWidth", 1280), 160, kMaxCanvasSize);
    it.windowHeight = std::clamp(getInt(o, "windowHeight", 720), 90, kMaxCanvasSize);
    it.ndiName = getString(o, "ndiName");
    it.ndiAlpha = getBool(o, "ndiAlpha", false);
    it.deviceId = getString(o, "deviceId");
    it.width = std::clamp(getInt(o, "width", 1920), 16, kMaxCanvasSize);
    it.height = std::clamp(getInt(o, "height", 1080), 16, kMaxCanvasSize);
    if (auto a = o.find("slices"); a != o.end() && a->is_array()) {
        std::set<std::string> ids;
        for (const auto& sj : *a) {
            if (!sj.is_object() || it.slices.size() >= 64) continue;
            Slice s = sliceFromJson(sj);
            if (s.id.empty() || !ids.insert(s.id).second) s.id = "S" + std::to_string(it.slices.size() + 1) + "x";
            ids.insert(s.id);
            it.slices.push_back(std::move(s));
        }
    } else {
        // Saída antiga (sem slices): mostra o MEDIAPLAYER inteiro, com a proporção certa.
        Slice s = makeFullSlice(SliceSourceKind::MediaPlayer, it.width, it.height);
        s.keepAspect = true;
        it.slices.push_back(s);
    }
    return it;
}

json clipToJson(const Clip& c) {
    json j{{"path", c.path}, {"name", c.name},         {"in", c.inPoint},         {"out", c.outPoint},
           {"speed", c.speed}, {"end", toString(c.end)}, {"volumeDb", c.volumeDb}, {"normalize", c.normalize}};
    if (c.loudness) j["loudness"] = *c.loudness;
    if (c.peakDb) j["peakDb"] = *c.peakDb;
    if (c.thumbTime) j["thumb"] = *c.thumbTime;
    return j;
}

Clip clipFromJson(const json& j) {
    Clip c;
    c.path = getString(j, "path");
    c.name = getString(j, "name");
    c.inPoint = getNumber(j, "in", 0.0);
    c.outPoint = getNumber(j, "out", 0.0);
    c.speed = getNumber(j, "speed", 1.0);
    c.end = clipEndFromString(getString(j, "end", "loop"));
    c.volumeDb = getNumber(j, "volumeDb", 0.0);
    c.normalize = getBool(j, "normalize", true);
    c.loudness = getOptNumber(j, "loudness");
    c.peakDb = getOptNumber(j, "peakDb");
    c.thumbTime = getOptNumber(j, "thumb");
    c.clamp();
    return c;
}

json layerToJsonObj(const CompLayer& l) {
    json j{{"id", l.id},       {"name", l.name},     {"props", propsToJson(l.props)}, {"video", l.video},
           {"audio", l.audio}, {"solo", l.solo},     {"bypass", l.bypass},            {"transition", l.transition},
           {"volumeDb", l.volumeDb}};
    return j;
}

CompLayer layerFromJsonObj(const json& j) {
    CompLayer l;
    l.id = getString(j, "id");
    l.name = getString(j, "name", l.id);
    if (auto p = j.find("props"); p != j.end() && p->is_object()) l.props = propsFromJson(*p);
    l.video = getBool(j, "video", true);
    l.audio = getBool(j, "audio", true);
    l.solo = getBool(j, "solo", false);
    l.bypass = getBool(j, "bypass", false);
    l.transition = getNumber(j, "transition", 0.0);
    l.volumeDb = getNumber(j, "volumeDb", 0.0);
    l.clamp();
    return l;
}

json compositionToJson(const Composition& c) {
    json layers = json::array();
    for (const auto& l : c.layers) layers.push_back(layerToJsonObj(l));
    json columns = json::array();
    for (const auto& col : c.columns) {
        json cells = json::object();
        for (const auto& [lid, clip] : col.cells) cells[lid] = clipToJson(clip);
        columns.push_back({{"id", col.id}, {"name", col.name}, {"cells", cells}});
    }
    return {{"id", c.id},         {"name", c.name},           {"width", c.width},   {"height", c.height},
            {"opacity", c.opacity}, {"volumeDb", c.volumeDb}, {"layers", layers},   {"columns", columns}};
}

Composition compositionFromJson(const json& j, std::vector<std::string>& warnings) {
    Composition c;
    c.id = getString(j, "id");
    c.name = getString(j, "name", c.id);
    c.width = std::clamp(getInt(j, "width", 1920), 16, kMaxCanvasSize);
    c.height = std::clamp(getInt(j, "height", 1080), 16, kMaxCanvasSize);
    c.opacity = clampFinite(getNumber(j, "opacity", 1.0), 0.0, 1.0, 1.0);
    c.volumeDb = clampFinite(getNumber(j, "volumeDb", 0.0), -60.0, 12.0, 0.0);
    std::set<std::string> lids;
    if (auto a = j.find("layers"); a != j.end() && a->is_array())
        for (const auto& lj : *a) {
            if (!lj.is_object() || c.layers.size() >= 64) continue;
            CompLayer l = layerFromJsonObj(lj);
            if (l.id.empty() || lids.count(l.id)) l.id = c.newLayerId();
            lids.insert(l.id);
            c.layers.push_back(std::move(l));
        }
    std::set<std::string> cids;
    if (auto a = j.find("columns"); a != j.end() && a->is_array())
        for (const auto& cj : *a) {
            if (!cj.is_object() || c.columns.size() >= 512) continue;
            CompColumn col;
            col.id = getString(cj, "id");
            if (col.id.empty() || cids.count(col.id)) col.id = c.newColumnId();
            cids.insert(col.id);
            col.name = getString(cj, "name", col.id);
            if (auto cells = cj.find("cells"); cells != cj.end() && cells->is_object())
                for (auto it = cells->begin(); it != cells->end(); ++it) {
                    if (!it.value().is_object() || !lids.count(it.key())) {
                        warnings.push_back("clip numa layer inexistente ignorado");
                        continue;
                    }
                    Clip clip = clipFromJson(it.value());
                    if (!clip.path.empty()) col.cells[it.key()] = std::move(clip);
                }
            c.columns.push_back(std::move(col));
        }
    return c;
}

} // namespace

std::string outputToJson(const Output& o) { return outputToJsonObj(o).dump(-1, ' ', false, json::error_handler_t::replace); }

std::optional<Output> outputFromJson(const std::string& text) {
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    try {
        Output o = outputFromJsonObj(j);
        if (o.id.empty()) return std::nullopt;
        return o;
    } catch (...) {
        return std::nullopt;
    }
}

std::string compLayerToJson(const CompLayer& l) { return layerToJsonObj(l).dump(-1, ' ', false, json::error_handler_t::replace); }

std::optional<CompLayer> compLayerFromJson(const std::string& text) {
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    try {
        CompLayer l = layerFromJsonObj(j);
        if (l.id.empty()) return std::nullopt;
        return l;
    } catch (...) {
        return std::nullopt;
    }
}

ParseResult parseProject(const std::string& text) {
    ParseResult r;
    try {
        const json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object()) {
            r.error = "JSON inválido";
            return r;
        }
        if (getString(j, "format") != "vthost-project") {
            r.error = "não é um projecto VTHOST";
            return r;
        }
        Project p;
        const int fileVersion = getInt(j, "version", kProjectFormatVersion);
        if (fileVersion > kProjectFormatVersion)
            r.warnings.push_back("projecto criado numa versão mais recente do VTHOST");
        p.version = kProjectFormatVersion;
        p.name = getString(j, "name", "Untitled");
        p.width = std::clamp(getInt(j, "width", 1920), 16, kMaxCanvasSize);
        p.height = std::clamp(getInt(j, "height", 1080), 16, kMaxCanvasSize);
        p.loopPlaylist = getBool(j, "loopPlaylist", false);
        p.normalizeTargetLufs = clampFinite(getNumber(j, "normalizeTarget", -16.0), -40.0, -5.0, -16.0);
        p.masterVolumeDb = clampFinite(getNumber(j, "masterVolumeDb", 0.0), -60.0, 12.0, 0.0);

        if (fileVersion < 2) {
            // Projecto da versão "media server" (0.6 e anteriores): cada media passa a uma cue.
            if (auto c = j.find("canvases"); c != j.end() && c->is_array() && !c->empty() && c->front().is_object()) {
                p.width = std::clamp(getInt(c->front(), "width", 1920), 16, kMaxCanvasSize);
                p.height = std::clamp(getInt(c->front(), "height", 1080), 16, kMaxCanvasSize);
            }
            if (auto a = j.find("media"); a != j.end() && a->is_array())
                for (const auto& m : *a) {
                    if (!m.is_object() || getString(m, "path").empty()) continue;
                    Cue q;
                    q.id = p.newCueId();
                    q.path = getString(m, "path");
                    q.playCount = getString(m, "loopMode", "infinite") == "infinite" ? 0 : 1;
                    q.pauseAtEnd = true;
                    p.cues.push_back(std::move(q));
                }
            r.warnings.push_back("projecto antigo convertido: cada media passou a uma cue da playlist");
        } else if (auto a = j.find("cues"); a != j.end() && a->is_array()) {
            std::set<std::string> ids;
            for (const auto& c : *a) {
                if (!c.is_object()) continue;
                Cue q = cueFromJson(c);
                if (q.path.empty()) {
                    r.warnings.push_back("cue sem ficheiro ignorada");
                    continue;
                }
                if (q.id.empty() || !ids.insert(q.id).second) q.id.clear();
                p.cues.push_back(std::move(q));
            }
            // Ids estáveis para cues sem id ou repetidas.
            for (auto& q : p.cues)
                if (q.id.empty()) q.id = p.newCueId();
            for (auto& q : p.cues)
                if (!q.gotoCue.empty() && p.cueIndex(q.gotoCue) < 0) {
                    r.warnings.push_back("\"ir para\" uma cue inexistente ignorado");
                    q.gotoCue.clear();
                }
        }

        std::set<std::string> outputIds;
        if (auto a = j.find("outputs"); a != j.end() && a->is_array()) {
            for (const auto& o : *a) {
                if (!o.is_object()) continue;
                Output it = outputFromJsonObj(o);
                if (it.id.empty() || !outputIds.insert(it.id).second) {
                    r.warnings.push_back("output ignorado (id vazio ou duplicado)");
                    continue;
                }
                p.outputs.push_back(std::move(it));
            }
        }

        std::set<std::string> compIds;
        if (auto a = j.find("compositions"); a != j.end() && a->is_array()) {
            for (const auto& cj : *a) {
                if (!cj.is_object() || p.compositions.size() >= 64) continue;
                Composition c = compositionFromJson(cj, r.warnings);
                if (c.id.empty() || compIds.count(c.id)) c.id = p.newCompositionId();
                compIds.insert(c.id);
                p.compositions.push_back(std::move(c));
            }
        }
        if (p.compositions.empty()) p.compositions.push_back(Composition::makeDefault("comp1", "Composition 1"));
        // Slices que apontam para composições/layers que já não existem passam a mostrar nada (fonte inválida).
        for (auto& o : p.outputs)
            for (auto& sl : o.slices) {
                if (sl.source == SliceSourceKind::MediaPlayer) continue;
                const Composition* c = p.findComposition(sl.compositionId);
                if (!c || (sl.source == SliceSourceKind::Layer && !c->findLayer(sl.layerId)))
                    r.warnings.push_back("slice \"" + sl.name + "\" com uma fonte que já não existe");
            }
        r.project = std::move(p);
    } catch (const std::exception& e) {
        r.error = e.what();
    } catch (...) {
        r.error = "erro desconhecido";
    }
    return r;
}

std::string serializeProject(const Project& p) {
    json j;
    j["format"] = "vthost-project";
    j["version"] = kProjectFormatVersion;
    j["name"] = p.name;
    j["width"] = p.width;
    j["height"] = p.height;
    j["loopPlaylist"] = p.loopPlaylist;
    j["normalizeTarget"] = p.normalizeTargetLufs;
    j["masterVolumeDb"] = p.masterVolumeDb;
    j["cues"] = json::array();
    for (const auto& q : p.cues) j["cues"].push_back(cueToJson(q));
    j["compositions"] = json::array();
    for (const auto& c : p.compositions) j["compositions"].push_back(compositionToJson(c));
    j["outputs"] = json::array();
    for (const auto& o : p.outputs) j["outputs"].push_back(outputToJsonObj(o));
    // error_handler::replace: UTF-8 inválido num nome não pode impedir a gravação.
    return j.dump(2, ' ', false, json::error_handler_t::replace);
}

ParseResult loadProjectFile(const std::string& utf8Path) {
    std::string err;
    auto text = readFileLimited(pathFromUtf8(utf8Path), 64u * 1024u * 1024u, &err);
    if (!text) {
        ParseResult r;
        r.error = err;
        return r;
    }
    return parseProject(*text);
}

bool saveProjectFile(const Project& p, const std::string& utf8Path, std::string* error) {
    return writeFileAtomic(pathFromUtf8(utf8Path), serializeProject(p), true, error);
}

} // namespace vthost
