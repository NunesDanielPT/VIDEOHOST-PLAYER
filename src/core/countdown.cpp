#include "vthost/core/countdown.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <nlohmann/json.hpp>

namespace vthost {
namespace {

double finiteOr0(double v) { return std::isfinite(v) ? v : 0.0; }

std::string baseName(const std::string& path) {
    std::size_t slash = path.find_last_of("/\\");
    std::string n = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::size_t dot = n.find_last_of('.');
    if (dot != std::string::npos && dot > 0) n.resize(dot);
    return n;
}

} // namespace

Countdown computeCountdown(const Project& project, const protocol::EngineStatus& s) {
    Countdown c;
    c.mode = "idle";
    c.state = s.state;
    const int n = static_cast<int>(project.cues.size());
    if (s.cue < 0 || s.cue >= n || s.state == "stopped" || s.state == "error") return c;
    const Cue& q = project.cues[static_cast<std::size_t>(s.cue)];
    c.mode = "cue";
    c.active = true;
    c.cueNumber = s.cue + 1;
    c.cueLabel = q.label;
    c.cueName = q.displayName();
    c.mediaName = baseName(q.path);
    if (s.nextCue >= 0 && s.nextCue < n) c.nextName = project.cues[static_cast<std::size_t>(s.nextCue)].displayName();
    c.running = s.state == "playing";
    c.loops = s.infinite;
    const double pass = finiteOr0(s.passDuration);
    if (s.infinite) {
        c.duration = pass;
        c.elapsed = std::clamp(finiteOr0(s.elapsed), 0.0, pass);
        c.remaining = std::max(0.0, pass - c.elapsed);
    } else {
        c.elapsed = std::max(0.0, s.pass * pass + finiteOr0(s.elapsed));
        c.remaining = s.state == "ended" ? 0.0 : std::max(0.0, finiteOr0(s.remaining));
        c.duration = c.elapsed + c.remaining;
    }
    return c;
}

Countdown blockCountdown(const Countdown& c, const Project& p, int cueIndex,
                         const std::function<double(const Cue&)>& cueSeconds) {
    const int n = static_cast<int>(p.cues.size());
    if (!c.active || c.loops || cueIndex < 0 || cueIndex >= n || !cueSeconds) return c;
    const Cue& cur = p.cues[static_cast<std::size_t>(cueIndex)];
    const Block* b = p.findBlock(cur.block);
    if (!b) return c;
    double before = 0.0, after = 0.0;
    for (int i = 0; i < n; ++i) {
        const Cue& q = p.cues[static_cast<std::size_t>(i)];
        if (q.block != cur.block || i == cueIndex) continue;
        const double s = cueSeconds(q);
        if (!std::isfinite(s) || s < 0.0) return c; // duração desconhecida (ex. loop infinito)
        (i < cueIndex ? before : after) += s;
    }
    Countdown r = c;
    r.elapsed += before;
    r.remaining += after;
    if (r.duration > 0.0) r.duration += before + after;
    r.blockName = b->name.empty() ? b->id : b->name;
    return r;
}

Countdown advanceCountdown(const Countdown& c, double seconds) {
    Countdown r = c;
    if (!r.active || !r.running || !(seconds > 0.0) || !std::isfinite(seconds)) return r;
    r.elapsed += seconds;
    if (r.duration > 0.0 && r.elapsed > r.duration) {
        r.elapsed = r.loops ? std::fmod(r.elapsed, r.duration) : r.duration;
    }
    r.remaining = r.duration > 0.0 ? std::max(0.0, r.duration - r.elapsed) : std::max(0.0, r.remaining - seconds);
    return r;
}

std::string formatCountdown(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    seconds = std::min(seconds, 359999.0);
    const long long total = static_cast<long long>(std::ceil(seconds - 1e-6));
    const long long h = total / 3600, m = (total / 60) % 60, sec = total % 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", h, m, sec);
    else std::snprintf(buf, sizeof buf, "%02lld:%02lld", m, sec);
    return buf;
}

std::string countdownJson(const Countdown& c, const std::string& projectName) {
    nlohmann::json j{{"active", c.active},
                     {"running", c.running},
                     {"loops", c.loops},
                     {"remaining", c.remaining},
                     {"duration", c.duration},
                     {"elapsed", c.elapsed},
                     {"text", formatCountdown(c.remaining)},
                     {"cueNumber", c.cueNumber},
                     {"label", c.cueLabel},
                     {"cue", c.cueName},
                     {"next", c.nextName},
                     {"media", c.mediaName},
                     {"mode", c.mode},
                     {"state", c.state},
                     {"project", projectName}};
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace vthost
