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

Countdown computeCountdown(const Project& project, const protocol::EngineStatus& s, bool loopBoard) {
    Countdown c;
    c.mode = "idle";

    // Timeline a tocar ou em pausa.
    if (!s.timelineId.empty()) {
        const Timeline* tl = project.findTimeline(s.timelineId);
        if (!tl) return c;
        double end = 0.0;
        for (const auto& clip : tl->clips) end = std::max(end, clip.end());
        if (end <= 0.0) return c;
        c.mode = "timeline";
        c.active = true;
        c.running = s.timelinePlaying;
        c.duration = end;
        c.elapsed = std::clamp(finiteOr0(s.timelineTime), 0.0, end);
        c.remaining = end - c.elapsed;
        c.cueName = tl->name;
        return c;
    }

    const int n = static_cast<int>(project.columns.size());
    if (s.activeColumn < 0 || s.activeColumn >= n) return c;
    const Column& col = project.columns[static_cast<std::size_t>(s.activeColumn)];
    c.cueNumber = s.activeColumn + 1;
    c.cueName = col.name;
    if (const int next = project.nextColumn(s.activeColumn, loopBoard); next >= 0)
        c.nextName = project.columns[static_cast<std::size_t>(next)].name;

    if (col.follow == FollowMode::Timer) {
        c.mode = "timer";
        c.active = true;
        c.running = true;
        c.duration = col.followSeconds;
        c.elapsed = std::clamp(finiteOr0(s.activeColumnTime), 0.0, col.followSeconds);
        c.remaining = col.followSeconds - c.elapsed;
        return c;
    }

    // Vídeo com mais tempo por tocar entre as layers desta cue.
    bool found = false;
    for (const auto& ls : s.layers) {
        auto cell = col.cells.find(ls.id);
        if (cell == col.cells.end() || cell->second != ls.mediaId) continue;
        const double d = finiteOr0(ls.duration);
        if (d <= 0.0) continue; // imagem fixa
        const double pos = std::clamp(finiteOr0(ls.position), 0.0, d);
        const bool ended = ls.state == "ended";
        const double rem = ended ? 0.0 : d - pos;
        if (found && rem <= c.remaining) continue;
        found = true;
        const MediaItem* m = project.findMedia(ls.mediaId);
        c.remaining = rem;
        c.duration = d;
        c.elapsed = ended ? d : pos;
        c.running = ls.state == "playing" && !ls.paused;
        // Com "seguir no fim do vídeo" o motor toca sem loop.
        c.loops = m && m->loops() && col.follow != FollowMode::MediaEnd;
        c.mediaName = m ? baseName(m->path) : std::string();
    }
    if (found) {
        c.mode = "cue";
        c.active = true;
    }
    return c;
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
                     {"cue", c.cueName},
                     {"next", c.nextName},
                     {"media", c.mediaName},
                     {"mode", c.mode},
                     {"project", projectName}};
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace vthost
