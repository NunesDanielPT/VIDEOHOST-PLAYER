#include "vthost/core/project.h"

#include "vthost/core/atomic_file.h"

#include <algorithm>
#include <cmath>
#include <set>

#include <nlohmann/json.hpp>

using nlohmann::json;

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
}

LayerQuad computeLayerQuad(const LayerProps& p, int mediaW, int mediaH, int canvasW, int canvasH) {
    LayerQuad q{canvasW / 2.0, canvasH / 2.0, canvasW / 2.0, canvasH / 2.0, 0.0};
    if (mediaW > 0 && mediaH > 0 && canvasW > 0 && canvasH > 0) {
        const double fit = std::min(static_cast<double>(canvasW) / mediaW, static_cast<double>(canvasH) / mediaH);
        q.halfW = mediaW * fit / 2.0;
        q.halfH = mediaH * fit / 2.0;
    }
    q.halfW *= p.scale;
    q.halfH *= p.scale;
    q.cx += p.posX;
    q.cy += p.posY;
    q.rotationRad = p.rotation * 3.14159265358979323846 / 180.0;
    return q;
}

double Timeline::duration() const {
    double d = 60.0;
    for (const auto& c : clips) d = std::max(d, c.end());
    for (const auto& s : segments) d = std::max(d, s.end);
    return std::min(d, kMaxTimelineSeconds);
}

const Clip* Timeline::clipAt(const std::string& layerId, double time) const {
    // Se houver sobreposição, ganha o clip que começou mais tarde.
    const Clip* best = nullptr;
    for (const auto& c : clips)
        if (c.layerId == layerId && time >= c.start && time < c.end() && (!best || c.start >= best->start))
            best = &c;
    return best;
}

const Clip* Timeline::findClip(const std::string& id) const {
    for (const auto& c : clips)
        if (c.id == id) return &c;
    return nullptr;
}

const Timeline* Project::findTimeline(const std::string& id) const {
    for (const auto& t : timelines)
        if (t.id == id) return &t;
    return nullptr;
}

const MediaItem* Project::findMedia(const std::string& id) const {
    for (const auto& m : media)
        if (m.id == id) return &m;
    return nullptr;
}

const Layer* Project::findLayer(const std::string& id) const {
    for (const auto& l : layers)
        if (l.id == id) return &l;
    return nullptr;
}

Project Project::makeDefault() {
    Project p;
    p.layers = {{"L1", "Layer 1", {}, true}, {"L2", "Layer 2", {}, true}};
    p.timelines = {{"T1", "Timeline 1", {}, {}}};
    for (int i = 1; i <= 4; ++i) p.columns.push_back({"Cue " + std::to_string(i), 1.0, {}});
    p.outputs = {{"O1", "Output 1", 0, true}};
    return p;
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

int getInt(const json& j, const char* key, int def) {
    const double v = getNumber(j, key, def);
    if (v < -1e9 || v > 1e9) return def;
    return static_cast<int>(v);
}

bool getBool(const json& j, const char* key, bool def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_boolean()) ? it->get<bool>() : def;
}

} // namespace

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
        p.version = getInt(j, "version", kProjectFormatVersion);
        if (p.version > kProjectFormatVersion)
            r.warnings.push_back("projecto criado numa versão mais recente do VTHOST");
        p.name = getString(j, "name", "Untitled");

        if (auto c = j.find("canvas"); c != j.end() && c->is_object()) {
            p.canvasWidth = std::clamp(getInt(*c, "width", 1920), 16, kMaxCanvasSize);
            p.canvasHeight = std::clamp(getInt(*c, "height", 1080), 16, kMaxCanvasSize);
        }

        std::set<std::string> mediaIds;
        if (auto a = j.find("media"); a != j.end() && a->is_array()) {
            for (const auto& m : *a) {
                if (!m.is_object()) continue;
                MediaItem it{getString(m, "id"), getString(m, "path"), getBool(m, "loop", true)};
                if (it.id.empty() || it.path.empty() || !mediaIds.insert(it.id).second) {
                    r.warnings.push_back("media ignorada (id vazio/duplicado ou sem caminho)");
                    continue;
                }
                p.media.push_back(std::move(it));
            }
        }

        std::set<std::string> layerIds;
        if (auto a = j.find("layers"); a != j.end() && a->is_array()) {
            for (const auto& l : *a) {
                if (!l.is_object()) continue;
                Layer it;
                it.id = getString(l, "id");
                if (it.id.empty() || !layerIds.insert(it.id).second) {
                    r.warnings.push_back("layer ignorada (id vazio ou duplicado)");
                    continue;
                }
                it.name = getString(l, "name", it.id);
                it.props.opacity = getNumber(l, "opacity", 1.0);
                it.props.blend = blendModeFromString(getString(l, "blend", "normal"));
                it.props.posX = getNumber(l, "x", 0.0);
                it.props.posY = getNumber(l, "y", 0.0);
                it.props.scale = getNumber(l, "scale", 1.0);
                it.props.rotation = getNumber(l, "rotation", 0.0);
                it.props.clamp();
                it.visible = getBool(l, "visible", true);
                p.layers.push_back(std::move(it));
            }
        }

        if (auto a = j.find("columns"); a != j.end() && a->is_array()) {
            for (const auto& c : *a) {
                if (!c.is_object()) continue;
                Column col;
                col.name = getString(c, "name");
                col.fadeSeconds = std::clamp(getNumber(c, "fade", 1.0), 0.0, 60.0);
                if (auto cells = c.find("cells"); cells != c.end() && cells->is_object()) {
                    for (auto it = cells->begin(); it != cells->end(); ++it) {
                        if (!it.value().is_string()) continue;
                        const std::string mediaId = it.value().get<std::string>();
                        if (!layerIds.count(it.key()) || !mediaIds.count(mediaId)) {
                            r.warnings.push_back("célula com layer/media inexistente ignorada");
                            continue;
                        }
                        col.cells[it.key()] = mediaId;
                    }
                }
                p.columns.push_back(std::move(col));
            }
        }

        std::set<std::string> outputIds;
        if (auto a = j.find("outputs"); a != j.end() && a->is_array()) {
            for (const auto& o : *a) {
                if (!o.is_object()) continue;
                Output it{getString(o, "id"), getString(o, "name"),
                          std::clamp(getInt(o, "monitor", 0), 0, 63), getBool(o, "fullscreen", true)};
                if (it.id.empty() || !outputIds.insert(it.id).second) continue;
                if (it.name.empty()) it.name = it.id;
                p.outputs.push_back(std::move(it));
            }
        }

        std::set<std::string> timelineIds;
        if (auto a = j.find("timelines"); a != j.end() && a->is_array()) {
            for (const auto& t : *a) {
                if (!t.is_object()) continue;
                Timeline tl;
                tl.id = getString(t, "id");
                if (tl.id.empty() || !timelineIds.insert(tl.id).second) continue;
                tl.name = getString(t, "name", tl.id);
                std::set<std::string> clipIds;
                if (auto cs = t.find("clips"); cs != t.end() && cs->is_array()) {
                    for (const auto& c : *cs) {
                        if (!c.is_object()) continue;
                        Clip clip;
                        clip.id = getString(c, "id");
                        clip.layerId = getString(c, "layer");
                        clip.mediaId = getString(c, "media");
                        clip.start = std::clamp(getNumber(c, "start", 0.0), 0.0, kMaxTimelineSeconds);
                        clip.duration = std::clamp(getNumber(c, "duration", 5.0), 0.04, kMaxTimelineSeconds);
                        clip.inPoint = std::clamp(getNumber(c, "in", 0.0), 0.0, kMaxTimelineSeconds);
                        if (clip.id.empty() || !clipIds.insert(clip.id).second || !layerIds.count(clip.layerId) ||
                            !mediaIds.count(clip.mediaId)) {
                            r.warnings.push_back("clip de timeline inválido ignorado");
                            continue;
                        }
                        tl.clips.push_back(std::move(clip));
                    }
                }
                if (auto ss = t.find("segments"); ss != t.end() && ss->is_array()) {
                    for (const auto& sg : *ss) {
                        if (!sg.is_object()) continue;
                        Segment seg;
                        seg.name = getString(sg, "name");
                        seg.start = std::clamp(getNumber(sg, "start", 0.0), 0.0, kMaxTimelineSeconds);
                        seg.end = std::clamp(getNumber(sg, "end", seg.start + 10.0), 0.0, kMaxTimelineSeconds);
                        seg.loop = getBool(sg, "loop", false);
                        if (seg.end - seg.start < 0.1) continue; // segmento vazio: um loop de 0 s bloquearia a timeline
                        tl.segments.push_back(std::move(seg));
                    }
                }
                p.timelines.push_back(std::move(tl));
            }
        }

        r.project = std::move(p);
    } catch (const std::exception& e) {
        r.project.reset();
        r.error = std::string("erro a ler projecto: ") + e.what();
    } catch (...) {
        r.project.reset();
        r.error = "erro desconhecido a ler projecto";
    }
    return r;
}

std::string serializeProject(const Project& p) {
    json j;
    j["format"] = "vthost-project";
    j["version"] = kProjectFormatVersion;
    j["name"] = p.name;
    j["canvas"] = {{"width", p.canvasWidth}, {"height", p.canvasHeight}};
    j["media"] = json::array();
    for (const auto& m : p.media) j["media"].push_back({{"id", m.id}, {"path", m.path}, {"loop", m.loop}});
    j["layers"] = json::array();
    for (const auto& l : p.layers)
        j["layers"].push_back({{"id", l.id}, {"name", l.name}, {"opacity", l.props.opacity},
                               {"blend", toString(l.props.blend)}, {"x", l.props.posX}, {"y", l.props.posY},
                               {"scale", l.props.scale}, {"rotation", l.props.rotation}, {"visible", l.visible}});
    j["columns"] = json::array();
    for (const auto& c : p.columns) {
        json cells = json::object();
        for (const auto& [layer, media] : c.cells) cells[layer] = media;
        j["columns"].push_back({{"name", c.name}, {"fade", c.fadeSeconds}, {"cells", cells}});
    }
    j["outputs"] = json::array();
    for (const auto& o : p.outputs)
        j["outputs"].push_back({{"id", o.id}, {"name", o.name}, {"monitor", o.monitor},
                                {"fullscreen", o.fullscreen}});
    j["timelines"] = json::array();
    for (const auto& t : p.timelines) {
        json clips = json::array();
        for (const auto& c : t.clips)
            clips.push_back({{"id", c.id}, {"layer", c.layerId}, {"media", c.mediaId}, {"start", c.start},
                             {"duration", c.duration}, {"in", c.inPoint}});
        json segs = json::array();
        for (const auto& sg : t.segments)
            segs.push_back({{"name", sg.name}, {"start", sg.start}, {"end", sg.end}, {"loop", sg.loop}});
        j["timelines"].push_back({{"id", t.id}, {"name", t.name}, {"clips", clips}, {"segments", segs}});
    }
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
