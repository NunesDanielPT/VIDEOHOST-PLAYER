#pragma once
#include <filesystem>

namespace vthost {

// Instala handlers para excepções não tratadas, pure virtual calls, parâmetros
// inválidos do CRT e std::terminate. Em todos os casos escreve um minidump
// (crashes/<nome>-<data>.dmp) e termina o processo de imediato, sem caixas de
// diálogo do Windows que deixariam o processo pendurado. O watchdog relança-o.
void installCrashHandler(const std::filesystem::path& dumpDir, const wchar_t* processName);

} // namespace vthost
