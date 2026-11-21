#pragma once
// Logger simples, thread-safe, com rotação por tamanho. Nunca lança excepções:
// um erro de disco no log não pode derrubar o show.

#include <filesystem>
#include <string>
#include <string_view>

namespace vthost::log {

enum class Level { Debug, Info, Warn, Error };

// Abre o ficheiro de log (ex.: %LOCALAPPDATA%/VTHOST/logs/engine.log).
// Sem init, as mensagens vão só para stderr.
void init(const std::filesystem::path& file, std::size_t maxBytes = 10u * 1024u * 1024u);
void setMinLevel(Level level);
void write(Level level, std::string_view message);
void shutdown();

inline void debug(std::string_view m) { write(Level::Debug, m); }
inline void info(std::string_view m) { write(Level::Info, m); }
inline void warn(std::string_view m) { write(Level::Warn, m); }
inline void error(std::string_view m) { write(Level::Error, m); }

} // namespace vthost::log
