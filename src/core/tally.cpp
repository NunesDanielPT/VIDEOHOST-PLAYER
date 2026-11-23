#include "vthost/core/tally.h"

namespace vthost::tally {

std::optional<bool> vmixProgram(const std::string& raw, int input) {
    std::string line = raw;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
    static const std::string prefix = "TALLY OK ";
    if (input < 1 || line.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
    const std::string states = line.substr(prefix.size());
    if (static_cast<std::size_t>(input) > states.size()) return std::nullopt;
    const char c = states[static_cast<std::size_t>(input) - 1];
    if (c < '0' || c > '9') return std::nullopt;
    return c == '1';
}

bool parseAtemHeader(const std::uint8_t* d, std::size_t size, AtemHeader& h) {
    if (!d || size < 12) return false;
    h.flags = static_cast<std::uint8_t>(d[0] >> 3);
    h.length = static_cast<std::uint16_t>(((d[0] & 0x07) << 8) | d[1]);
    if (h.length != size) return false;
    h.session = static_cast<std::uint16_t>((d[2] << 8) | d[3]);
    h.ackId = static_cast<std::uint16_t>((d[4] << 8) | d[5]);
    h.packetId = static_cast<std::uint16_t>((d[10] << 8) | d[11]);
    return true;
}

std::vector<std::uint8_t> atemHello() {
    return {0x10, 0x14, 0x53, 0xab, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3a,
            0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

std::vector<std::uint8_t> atemAck(std::uint16_t session, std::uint16_t packetId) {
    std::vector<std::uint8_t> b(12, 0);
    const std::uint16_t head = static_cast<std::uint16_t>((kFlagAckReply << 11) | 12);
    b[0] = static_cast<std::uint8_t>(head >> 8);
    b[1] = static_cast<std::uint8_t>(head);
    b[2] = static_cast<std::uint8_t>(session >> 8);
    b[3] = static_cast<std::uint8_t>(session);
    b[4] = static_cast<std::uint8_t>(packetId >> 8);
    b[5] = static_cast<std::uint8_t>(packetId);
    return b;
}

std::vector<AtemCommand> parseAtemCommands(const std::uint8_t* p, std::size_t size) {
    std::vector<AtemCommand> out;
    if (!p) return out;
    std::size_t pos = 0;
    while (pos + 8 <= size && out.size() < 512) {
        const std::size_t len = static_cast<std::size_t>((p[pos] << 8) | p[pos + 1]);
        if (len < 8 || pos + len > size) break;
        AtemCommand c;
        c.name.assign(reinterpret_cast<const char*>(p + pos + 4), 4);
        c.data.assign(p + pos + 8, p + pos + len);
        out.push_back(std::move(c));
        pos += len;
    }
    return out;
}

std::optional<bool> atemTallyProgram(const AtemCommand& c, int source) {
    if (c.name != "TlSr" || c.data.size() < 2 || source < 0) return std::nullopt;
    const std::size_t count = static_cast<std::size_t>((c.data[0] << 8) | c.data[1]);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = 2 + i * 3;
        if (at + 3 > c.data.size()) break;
        const int s = (c.data[at] << 8) | c.data[at + 1];
        if (s == source) return (c.data[at + 2] & 0x01) != 0;
    }
    return std::nullopt;
}

} // namespace vthost::tally
