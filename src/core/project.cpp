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

Project Project::makeDefault() {
    Project p;
    Output o;
    o.id = "O1";
    o.name = "Output 1";
    o.type = OutputType::Display;
    o.monitor = 1; // segundo ecrã (se não existir, abre janela no principal)
    p.outputs.push_back(o);
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
    return c;
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
                Output it;
                it.id = getString(o, "id");
                if (it.id.empty() || !outputIds.insert(it.id).second) {
                    r.warnings.push_back("output ignorado (id vazio ou duplicado)");
                    continue;
                }
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
                p.outputs.push_back(std::move(it));
            }
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
    j["outputs"] = json::array();
    for (const auto& o : p.outputs)
        j["outputs"].push_back({{"id", o.id},
                                {"name", o.name},
                                {"type", toString(o.type)},
                                {"enabled", o.enabled},
                                {"display", o.displayId},
                                {"monitor", o.monitor},
                                {"windowWidth", o.windowWidth},
                                {"windowHeight", o.windowHeight},
                                {"ndiName", o.ndiName},
                                {"ndiAlpha", o.ndiAlpha},
                                {"deviceId", o.deviceId}});
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
