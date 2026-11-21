#pragma once
#include <filesystem>

namespace vthost::paths {

// Pasta de dados do utilizador: %LOCALAPPDATA%\VTHOST (Windows) ou ~/.local/share/VTHOST.
std::filesystem::path dataDir();
std::filesystem::path logsDir();
std::filesystem::path crashDir();
// Estado do show para recuperação após crash do motor.
std::filesystem::path showStateFile();

// Nomes partilhados entre processos.
inline constexpr const char* kPipeName = "vthost-engine";              // \\.\pipe\vthost-engine
inline constexpr const wchar_t* kHeartbeatName = L"Local\\VTHOST_Heartbeat";
inline constexpr const wchar_t* kEngineMutex = L"Local\\VTHOST_Engine";
inline constexpr const wchar_t* kWatchdogMutex = L"Local\\VTHOST_Watchdog";

} // namespace vthost::paths
