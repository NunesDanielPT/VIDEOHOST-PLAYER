#include "vthost/core/paths.h"

#include <cstdlib>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace vthost::paths {
namespace {
fs::path ensure(fs::path p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

fs::path envPath(const char* name) {
#ifdef _WIN32
    std::wstring wname(name, name + std::char_traits<char>::length(name)); // nomes ASCII
    wchar_t* value = nullptr;
    std::size_t len = 0;
    if (_wdupenv_s(&value, &len, wname.c_str()) == 0 && value) {
        fs::path p(value);
        std::free(value);
        return p;
    }
    return {};
#else
    const char* v = std::getenv(name);
    return v ? fs::path(v) : fs::path{};
#endif
}
} // namespace

fs::path dataDir() {
#ifdef _WIN32
    fs::path base = envPath("LOCALAPPDATA");
#else
    fs::path base = envPath("XDG_DATA_HOME");
    if (base.empty()) {
        fs::path home = envPath("HOME");
        if (!home.empty()) base = home / ".local" / "share";
    }
#endif
    if (base.empty()) base = fs::temp_directory_path();
    return ensure(base / "VTHOST");
}

fs::path logsDir() { return ensure(dataDir() / "logs"); }
fs::path crashDir() { return ensure(dataDir() / "crashes"); }
fs::path showStateFile() { return dataDir() / "show_state.json"; }

} // namespace vthost::paths
