#pragma once
// Versões semânticas simples ("1.2.3", "v1.2.3", "1.2"). Usado para decidir se
// uma release do GitHub é mais recente do que a versão instalada.

#include <optional>
#include <string>
#include <string_view>

namespace vthost {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    friend bool operator==(const Version&, const Version&) = default;
    friend auto operator<=>(const Version&, const Version&) = default;
    std::string toString() const;
};

// Aceita "v" inicial e ignora sufixos ("-beta", "+build"). nullopt se inválida.
std::optional<Version> parseVersion(std::string_view text);

// true se `candidate` for estritamente mais recente que `current`.
// Versões inválidas nunca são consideradas mais recentes.
bool isNewerVersion(std::string_view candidate, std::string_view current);

} // namespace vthost
