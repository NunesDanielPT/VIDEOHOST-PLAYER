#pragma once
// OSC 1.0 (Open Sound Control) mínimo para controlo remoto (Bitfocus Companion).
// Só o necessário: mensagens e bundles com argumentos int32 (i), float32 (f) e
// string (s). Pacotes mal formados nunca lançam excepções: são ignorados.

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace vthost::osc {

using Arg = std::variant<std::int32_t, float, std::string>;

struct Message {
    std::string address;
    std::vector<Arg> args;

    // Primeiro argumento como texto / número (strings numéricas também contam).
    std::string stringArg(std::size_t i = 0) const;
    bool numberArg(double& out, std::size_t i = 0) const;
};

inline constexpr std::size_t kMaxPacketBytes = 64 * 1024;

// Descodifica um pacote UDP (mensagem ou bundle, bundles dentro de bundles até 4
// níveis). Devolve as mensagens válidas encontradas.
std::vector<Message> decodePacket(const std::uint8_t* data, std::size_t size);

// Codifica uma mensagem.
std::vector<std::uint8_t> encodeMessage(const Message& m);

} // namespace vthost::osc
