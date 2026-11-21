#include "crash_handler.h"

#include "vthost/core/log.h"

#include <windows.h>

#include <dbghelp.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

namespace vthost {
namespace {

std::wstring g_dumpDir;
std::wstring g_name = L"vthost";

void writeDump(EXCEPTION_POINTERS* info) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t file[MAX_PATH];
    swprintf_s(file, L"%s\\%s-%04d%02d%02d-%02d%02d%02d.dmp", g_dumpDir.c_str(), g_name.c_str(),
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = info;
    mei.ClientPointers = FALSE;
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, type, info ? &mei : nullptr,
                      nullptr, nullptr);
    CloseHandle(h);
}

LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* info) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "CRASH: excepção 0x%08lX - a escrever minidump",
                  info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0ul);
    log::error(msg);
    writeDump(info);
    log::shutdown();
    TerminateProcess(GetCurrentProcess(), 0xC0DE0001);
    return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void fatal(const char* why) {
    log::error(std::string("FATAL: ") + why);
    // Força uma excepção SEH para passar pelo mesmo caminho (minidump + saída).
    RaiseException(0xE0000001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    TerminateProcess(GetCurrentProcess(), 0xC0DE0002);
    std::abort();
}

void purecallHandler() { fatal("pure virtual call"); }

void invalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
    fatal("parâmetro inválido no CRT");
}

void terminateHandler() { fatal("std::terminate (excepção C++ não apanhada)"); }

} // namespace

void installCrashHandler(const std::filesystem::path& dumpDir, const wchar_t* processName) {
    g_dumpDir = dumpDir.wstring();
    g_name = processName;

    // Sem caixas "O programa deixou de funcionar" nem "inserir disco": num show
    // ninguém as vai fechar e o processo ficaria pendurado.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    SetUnhandledExceptionFilter(unhandledFilter);
    _set_purecall_handler(purecallHandler);
    _set_invalid_parameter_handler(invalidParameterHandler);
    std::set_terminate(terminateHandler);
}

} // namespace vthost
