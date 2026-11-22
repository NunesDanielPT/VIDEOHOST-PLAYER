// vthost-watchdog: mantém o motor vivo.
//
//   vthost-watchdog.exe [--project <ficheiro.vthost>]
//
// - Lança o vthost-engine.exe (que está na mesma pasta).
// - Se o motor sair com erro ou crashar, relança-o com --recover (o show
//   retoma no mesmo cue e posição).
// - Se o motor ficar bloqueado (o heartbeat deixa de mexer), mata-o e relança.
// - Se o motor sair a pedido (shutdown), o watchdog também termina.
// - Se o motor falhar muitas vezes seguidas, espera um pouco entre tentativas,
//   mas nunca desiste: num show, tentar sempre é melhor que ficar preto.

#include "crash_handler.h"
#include "vthost/core/heartbeat.h"
#include "vthost/core/log.h"
#include "vthost/core/paths.h"

#include <windows.h>

#include <shellapi.h>

#include <chrono>
#include <cstdio>
#include <iterator>
#include <deque>
#include <string>

namespace {

using namespace vthost;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr auto kStartupGrace = 20s; // abrir projecto/media pode demorar
constexpr auto kHangTimeout = 4s;   // sem heartbeat durante isto = bloqueado
constexpr int kExitRequested = 0;
constexpr int kExitAlreadyRunning = 10;
constexpr int kExitRestart = 6; // reinício pedido pelo Studio (ex. outra placa gráfica)

std::wstring exeDir() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    std::wstring p(buf, n);
    const auto slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring quote(const std::wstring& s) {
    std::wstring q = L"\"";
    for (wchar_t c : s) {
        if (c == L'"') q += L'\\';
        q += c;
    }
    q += L'"';
    return q;
}

struct Heartbeat {
    HANDLE mapping = nullptr;
    HeartbeatBlock* block = nullptr;

    bool open() {
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(HeartbeatBlock),
                                     paths::kHeartbeatName);
        if (!mapping) return false;
        block = static_cast<HeartbeatBlock*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(HeartbeatBlock)));
        return block != nullptr;
    }
    std::uint64_t counter() const {
        return block ? static_cast<std::uint64_t>(
                           InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(&block->counter), 0, 0))
                     : 0;
    }
    EngineRunState state() const {
        return block ? static_cast<EngineRunState>(
                           InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&block->state), 0, 0))
                     : EngineRunState::Starting;
    }
    void reset(std::uint32_t restarts) {
        if (!block) return;
        block->magic = kHeartbeatMagic;
        block->enginePid = 0;
        block->counter = 0;
        block->state = static_cast<std::uint32_t>(EngineRunState::Starting);
        block->restarts = restarts;
    }
    ~Heartbeat() {
        if (block) UnmapViewOfFile(block);
        if (mapping) CloseHandle(mapping);
    }
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    log::init(paths::logsDir() / "watchdog.log");
    installCrashHandler(paths::crashDir(), L"watchdog");
    log::info(std::string("=== vthost-watchdog ") + VTHOST_VERSION + " ===");

    HANDLE mutex = CreateMutexW(nullptr, TRUE, paths::kWatchdogMutex);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        log::info("watchdog já está a correr");
        return 0;
    }

    std::wstring project;
    int argc = 0;
    if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i)
            if (std::wstring(argv[i]) == L"--project" && i + 1 < argc) project = argv[++i];
        LocalFree(argv);
    }

    Heartbeat hb;
    if (!hb.open()) log::error("não foi possível criar o heartbeat: só detecto crashes, não bloqueios");

    const std::wstring enginePath = exeDir() + L"\\vthost-engine.exe";
    std::uint32_t restarts = 0;
    bool recover = false;
    std::deque<Clock::time_point> recentStarts;

    for (;;) {
        // Backoff: mais de 5 arranques no último minuto => espera antes de tentar outra vez.
        const auto now = Clock::now();
        while (!recentStarts.empty() && now - recentStarts.front() > 60s) recentStarts.pop_front();
        if (recentStarts.size() >= 5) {
            log::warn("motor a falhar repetidamente: a aguardar 5 s antes de nova tentativa");
            Sleep(5000);
        }
        recentStarts.push_back(Clock::now());

        hb.reset(restarts);
        std::wstring cmd = quote(enginePath);
        if (recover) cmd += L" --recover";
        else if (!project.empty()) cmd += L" --project " + quote(project);

        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        std::wstring mutableCmd = cmd;
        if (!CreateProcessW(enginePath.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            exeDir().c_str(), &si, &pi)) {
            log::error("não foi possível lançar o motor (" + std::to_string(GetLastError()) + "): " +
                       narrow(enginePath));
            Sleep(2000);
            continue;
        }
        CloseHandle(pi.hThread);
        log::info(std::string("motor lançado, pid ") + std::to_string(pi.dwProcessId) +
                  (recover ? " (recuperação)" : ""));

        const auto launched = Clock::now();
        std::uint64_t lastCounter = hb.counter();
        auto lastProgress = Clock::now();
        bool hung = false;

        for (;;) {
            if (WaitForSingleObject(pi.hProcess, 250) == WAIT_OBJECT_0) break;
            const std::uint64_t c = hb.counter();
            const auto t = Clock::now();
            if (c != lastCounter) {
                lastCounter = c;
                lastProgress = t;
                continue;
            }
            const bool inGrace = (t - launched) < kStartupGrace && c == 0;
            if (!inGrace && hb.state() == EngineRunState::Running && t - lastProgress > kHangTimeout) {
                log::error("motor bloqueado (sem heartbeat há " +
                           std::to_string(std::chrono::duration_cast<std::chrono::seconds>(t - lastProgress).count()) +
                           " s): a terminar e relançar");
                TerminateProcess(pi.hProcess, 0xDEAD);
                WaitForSingleObject(pi.hProcess, 5000);
                hung = true;
                break;
            }
        }

        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);

        if (!hung && code == kExitRequested && hb.state() == EngineRunState::StoppingOnPurpose) {
            log::info("motor parado a pedido: watchdog termina");
            break;
        }
        if (!hung && code == kExitRestart) {
            log::info("reinício do motor pedido: a relançar e retomar o show");
            recover = true;
            recentStarts.clear(); // não conta para o backoff
            continue;
        }
        if (!hung && code == kExitAlreadyRunning) {
            log::warn("outro motor já está a correr: nova tentativa em 3 s");
            Sleep(3000);
            continue;
        }
        char hex[16];
        std::snprintf(hex, sizeof(hex), "0x%08lX", code);
        log::error(std::string("motor terminou inesperadamente (código ") + hex + "): a recuperar");
        recover = true;
        ++restarts;
    }

    log::shutdown();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
