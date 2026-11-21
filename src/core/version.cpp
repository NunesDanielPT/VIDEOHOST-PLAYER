#include "vthost/core/version.h"

#include <cctype>

namespace vthost {

std::string Version::toString() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

std::optional<Version> parseVersion(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);
    // Corta sufixos de pré-release/build.
    if (const auto cut = text.find_first_of("-+ "); cut != std::string_view::npos) text = text.substr(0, cut);
    if (text.empty()) return std::nullopt;

    int parts[3] = {0, 0, 0};
    int count = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        if (count == 3) return std::nullopt;
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return std::nullopt;
        long value = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            value = value * 10 + (text[i] - '0');
            if (value > 1000000) return std::nullopt;
            ++i;
        }
        parts[count++] = static_cast<int>(value);
        if (i < text.size()) {
            if (text[i] != '.') return std::nullopt;
            ++i;
            if (i == text.size()) return std::nullopt; // "1.2."
        }
    }
    if (count == 0) return std::nullopt;
    return Version{parts[0], parts[1], parts[2]};
}

bool isNewerVersion(std::string_view candidate, std::string_view current) {
    const auto c = parseVersion(candidate);
    const auto cur = parseVersion(current);
    if (!c) return false;
    if (!cur) return true; // versão local desconhecida: qualquer release válida serve
    return *c > *cur;
}

} // namespace vthost
