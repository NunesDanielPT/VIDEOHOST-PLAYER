#include "vthost/core/log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

namespace fs = std::filesystem;

namespace vthost::log {
namespace {

struct State {
    std::mutex mutex;
    std::ofstream file;
    fs::path path;
    std::size_t maxBytes = 0;
    std::size_t written = 0;
    Level minLevel = Level::Info;
};

State& state() {
    static State s;
    return s;
}

const char* levelName(Level l) {
    switch (l) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO ";
    case Level::Warn: return "WARN ";
    case Level::Error: return "ERROR";
    }
    return "?????";
}

std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buf;
}

void rotateLocked(State& s) {
    std::error_code ec;
    s.file.close();
    fs::path old = s.path;
    old += ".1";
    fs::remove(old, ec);
    fs::rename(s.path, old, ec);
    s.file.open(s.path, std::ios::app | std::ios::binary);
    s.written = 0;
}

} // namespace

void init(const fs::path& file, std::size_t maxBytes) {
    try {
        auto& s = state();
        std::lock_guard lock(s.mutex);
        std::error_code ec;
        if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
        s.path = file;
        s.maxBytes = maxBytes;
        s.file.open(file, std::ios::app | std::ios::binary);
        s.written = fs::exists(file, ec) ? static_cast<std::size_t>(fs::file_size(file, ec)) : 0;
    } catch (...) {
    }
}

void setMinLevel(Level level) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    s.minLevel = level;
}

void write(Level level, std::string_view message) {
    try {
        auto& s = state();
        std::lock_guard lock(s.mutex);
        if (level < s.minLevel) return;
        std::string line = timestamp();
        line += " [";
        line += levelName(level);
        line += "] ";
        line += message;
        line += '\n';
        std::fwrite(line.data(), 1, line.size(), stderr);
        if (s.file.is_open()) {
            s.file.write(line.data(), static_cast<std::streamsize>(line.size()));
            s.file.flush();
            s.written += line.size();
            if (s.maxBytes > 0 && s.written > s.maxBytes) rotateLocked(s);
        }
    } catch (...) {
        // Nunca propagar erros de log.
    }
}

void shutdown() {
    try {
        auto& s = state();
        std::lock_guard lock(s.mutex);
        s.file.close();
    } catch (...) {
    }
}

} // namespace vthost::log
