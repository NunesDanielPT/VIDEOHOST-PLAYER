// vthost-engine: motor de playback sem interface.
//
//   vthost-engine.exe [--project <ficheiro.vthost>] [--recover]
//
// Normalmente é lançado pelo vthost-watchdog, nunca directamente pelo Studio.

#include "crash_handler.h"
#include "engine_app.h"
#include "vthost/core/log.h"
#include "vthost/core/paths.h"

#include <windows.h>

#include <shellapi.h>

#include <string>
#include <vector>

namespace {

std::string toUtf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using namespace vthost;

    log::init(paths::logsDir() / "engine.log");
    installCrashHandler(paths::crashDir(), L"engine");
    log::info(std::string("=== vthost-engine ") + VTHOST_VERSION + " ===");

    // Uma só instância: dois motores a disputar os mesmos monitores seria pior que nenhum.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, paths::kEngineMutex);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        log::warn("já existe um motor a correr");
        return kExitAlreadyRunning;
    }

    EngineApp::Options opts;
    int argc = 0;
    if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            const std::wstring a = argv[i];
            if (a == L"--recover") opts.recover = true;
            else if (a == L"--project" && i + 1 < argc) opts.projectPath = toUtf8(argv[++i]);
        }
        LocalFree(argv);
    }

    // Janelas com DPI real: um output 4K tem de ser 3840x2160 píxeis, não escalado.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int code = 0;
    {
        EngineApp app;
        code = app.run(opts);
    }
    log::shutdown();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return code;
}
