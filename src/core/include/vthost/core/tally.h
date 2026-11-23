#pragma once
// Tally de misturadores de vídeo (saber quando o input do player está no ar):
//  - vMix: API TCP (porta 8099), "SUBSCRIBE TALLY" -> linhas "TALLY OK 0121..."
//    (um carácter por input: 0 = nada, 1 = programa, 2 = preview).
//  - Blackmagic ATEM: protocolo UDP (porta 9910). Aqui só a parte pura (pacotes,
//    comandos e o tally por fonte "TlSr"); a rede fica no Studio.
// Tudo tolerante a dados maus: nunca lança, devolve "sem informação".

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vthost::tally {

// vMix: true/false se a linha for de tally e tiver o input; nullopt caso contrário.
std::optional<bool> vmixProgram(const std::string& line, int input);

// --- ATEM
inline constexpr std::uint16_t kAtemPort = 9910;
inline constexpr std::uint8_t kFlagAckRequest = 0x01;
inline constexpr std::uint8_t kFlagNewSession = 0x02;
inline constexpr std::uint8_t kFlagRetransmit = 0x04;
inline constexpr std::uint8_t kFlagRetransmitRequest = 0x08;
inline constexpr std::uint8_t kFlagAckReply = 0x10;
inline constexpr std::uint16_t kAtemMaxPacketId = 1u << 15;

struct AtemHeader {
    std::uint8_t flags = 0;
    std::uint16_t length = 0;
    std::uint16_t session = 0;
    std::uint16_t ackId = 0;     // pacote nosso que o ATEM confirma
    std::uint16_t packetId = 0;  // id deste pacote (para o confirmarmos)
};

bool parseAtemHeader(const std::uint8_t* data, std::size_t size, AtemHeader& out);
std::vector<std::uint8_t> atemHello();
std::vector<std::uint8_t> atemAck(std::uint16_t session, std::uint16_t packetId);

struct AtemCommand {
    std::string name; // 4 letras, ex. "TlSr", "PrgI", "InCm"
    std::vector<std::uint8_t> data;
};
std::vector<AtemCommand> parseAtemCommands(const std::uint8_t* payload, std::size_t size);

// "TlSr" (tally por fonte): true/false se a fonte estiver na lista; nullopt se não estiver.
std::optional<bool> atemTallyProgram(const AtemCommand& tlsr, int source);

} // namespace vthost::tally
