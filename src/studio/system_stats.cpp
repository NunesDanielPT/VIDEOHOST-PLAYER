#include "system_stats.h"

#include <algorithm>

#ifdef _WIN32
#include <windows.h>

namespace {
unsigned long long toU64(const FILETIME& f) {
    return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}
} // namespace

bool SystemStats::cpuPercent(double& percent) {
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user)) return false;
    const unsigned long long i = toU64(idle);
    const unsigned long long t = toU64(kernel) + toU64(user); // o tempo de kernel já inclui o idle
    const bool ok = hasLast_ && t > lastTotal_;
    if (ok) percent = std::clamp(100.0 * (1.0 - static_cast<double>(i - lastIdle_) / static_cast<double>(t - lastTotal_)), 0.0, 100.0);
    lastIdle_ = i;
    lastTotal_ = t;
    hasLast_ = true;
    return ok;
}

bool SystemStats::ram(double& percent, double& usedGb, double& totalGb) const {
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof(m);
    if (!GlobalMemoryStatusEx(&m) || m.ullTotalPhys == 0) return false;
    const double gb = 1024.0 * 1024.0 * 1024.0;
    totalGb = static_cast<double>(m.ullTotalPhys) / gb;
    usedGb = static_cast<double>(m.ullTotalPhys - m.ullAvailPhys) / gb;
    percent = 100.0 * usedGb / totalGb;
    return true;
}
#else
bool SystemStats::cpuPercent(double&) { return false; }
bool SystemStats::ram(double&, double&, double&) const { return false; }
#endif
