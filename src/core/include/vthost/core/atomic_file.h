#pragma once
// Escrita atómica de ficheiros: escreve num temporário e só depois substitui o
// original. Um crash ou corte de energia a meio nunca deixa um ficheiro meio escrito.

#include <filesystem>
#include <optional>
#include <string>

namespace vthost {

// Conversões seguras UTF-8 <-> path (no Windows, std::string seria lido como ANSI
// e caminhos com acentos, ex. "Ação.mp4", partiam-se).
inline std::filesystem::path pathFromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
inline std::string pathToUtf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// Escreve `content` em `path` de forma atómica. Se `keepBackup`, o ficheiro
// anterior fica em `<path>.bak`. Nunca lança excepções; devolve false em erro.
bool writeFileAtomic(const std::filesystem::path& path, const std::string& content,
                     bool keepBackup = true, std::string* error = nullptr);

// Lê um ficheiro inteiro. Limite de tamanho para nunca rebentar a memória com
// um ficheiro errado (ex.: escolher um vídeo em vez de um projecto).
std::optional<std::string> readFileLimited(const std::filesystem::path& path,
                                           std::size_t maxBytes = 64u * 1024u * 1024u,
                                           std::string* error = nullptr);

} // namespace vthost
