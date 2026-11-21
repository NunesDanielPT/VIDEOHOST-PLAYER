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
    p.layers = {{"L1", "Layer 1", 1.0, BlendMode::Normal, true},
                {"L2", "Layer 2", 1.0, BlendMode::Normal, true}};
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
                it.opacity = std::clamp(getNumber(l, "opacity", 1.0), 0.0, 1.0);
                it.blend = blendModeFromString(getString(l, "blend", "normal"));
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
        j["layers"].push_back({{"id", l.id}, {"name", l.name}, {"opacity", l.opacity},
                               {"blend", toString(l.blend)}, {"visible", l.visible}});
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
