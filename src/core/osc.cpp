#include "vthost/core/osc.h"

#include <cmath>
#include <cstring>

namespace vthost::osc {

namespace {

std::size_t pad4(std::size_t n) { return (n + 3) & ~static_cast<std::size_t>(3); }

std::uint32_t readU32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

void writeU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void writeString(std::vector<std::uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    const std::size_t n = pad4(s.size() + 1) - s.size();
    out.insert(out.end(), n, 0);
}

// Lê uma string OSC (terminada em 0, alinhada a 4). false se não couber.
bool readString(const std::uint8_t* data, std::size_t size, std::size_t& pos, std::string& out) {
    if (pos >= size) return false;
    const auto* start = data + pos;
    const auto* end = static_cast<const std::uint8_t*>(std::memchr(start, 0, size - pos));
    if (!end) return false;
    out.assign(reinterpret_cast<const char*>(start), static_cast<std::size_t>(end - start));
    pos += pad4(static_cast<std::size_t>(end - start) + 1);
    return pos <= size;
}

bool decodeMessage(const std::uint8_t* data, std::size_t size, Message& m) {
    std::size_t pos = 0;
    if (!readString(data, size, pos, m.address) || m.address.empty() || m.address[0] != '/') return false;
    if (pos >= size) return true; // sem type tag: mensagem sem argumentos
    std::string tags;
    if (!readString(data, size, pos, tags) || tags.empty() || tags[0] != ',') return false;
    for (std::size_t i = 1; i < tags.size(); ++i) {
        switch (tags[i]) {
        case 'i':
            if (pos + 4 > size) return false;
            m.args.emplace_back(static_cast<std::int32_t>(readU32(data + pos)));
            pos += 4;
            break;
        case 'f': {
            if (pos + 4 > size) return false;
            const std::uint32_t bits = readU32(data + pos);
            float f = 0.0f;
            std::memcpy(&f, &bits, sizeof f);
            m.args.emplace_back(f);
            pos += 4;
            break;
        }
        case 's': {
            std::string s;
            if (!readString(data, size, pos, s)) return false;
            m.args.emplace_back(std::move(s));
            break;
        }
        case 'T': m.args.emplace_back(std::int32_t{1}); break;
        case 'F': m.args.emplace_back(std::int32_t{0}); break;
        case 'N':
        case 'I': break;
        default: return false; // tipo não suportado: ignora a mensagem inteira
        }
    }
    return true;
}

void decodeInto(const std::uint8_t* data, std::size_t size, int depth, std::vector<Message>& out) {
    if (!data || size < 4 || size > kMaxPacketBytes || size % 4 != 0 || depth > 4 || out.size() > 256) return;
    if (size >= 16 && std::memcmp(data, "#bundle\0", 8) == 0) {
        std::size_t pos = 16; // "#bundle\0" + timetag (executa já)
        while (pos + 4 <= size) {
            const std::uint32_t len = readU32(data + pos);
            pos += 4;
            if (len == 0 || len > size - pos) return;
            decodeInto(data + pos, len, depth + 1, out);
            pos += len;
        }
        return;
    }
    Message m;
    if (decodeMessage(data, size, m)) out.push_back(std::move(m));
}

} // namespace

std::string Message::stringArg(std::size_t i) const {
    if (i >= args.size()) return {};
    if (const auto* s = std::get_if<std::string>(&args[i])) return *s;
    if (const auto* n = std::get_if<std::int32_t>(&args[i])) return std::to_string(*n);
    if (const auto* f = std::get_if<float>(&args[i])) {
        if (std::isfinite(*f) && *f == std::floor(*f) && std::abs(*f) < 1e9f) return std::to_string(static_cast<long long>(*f));
        return std::to_string(*f);
    }
    return {};
}

bool Message::numberArg(double& out, std::size_t i) const {
    if (i >= args.size()) return false;
    if (const auto* n = std::get_if<std::int32_t>(&args[i])) {
        out = *n;
        return true;
    }
    if (const auto* f = std::get_if<float>(&args[i])) {
        if (!std::isfinite(*f)) return false;
        out = *f;
        return true;
    }
    if (const auto* s = std::get_if<std::string>(&args[i])) {
        try {
            std::size_t used = 0;
            const double v = std::stod(*s, &used);
            if (used == 0 || !std::isfinite(v)) return false;
            out = v;
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

std::vector<Message> decodePacket(const std::uint8_t* data, std::size_t size) {
    std::vector<Message> out;
    try {
        decodeInto(data, size, 0, out);
    } catch (...) {
        out.clear();
    }
    return out;
}

std::vector<std::uint8_t> encodeMessage(const Message& m) {
    std::vector<std::uint8_t> out;
    writeString(out, m.address);
    std::string tags = ",";
    for (const auto& a : m.args) tags += std::holds_alternative<std::int32_t>(a) ? 'i' : std::holds_alternative<float>(a) ? 'f' : 's';
    writeString(out, tags);
    for (const auto& a : m.args) {
        if (const auto* n = std::get_if<std::int32_t>(&a)) {
            writeU32(out, static_cast<std::uint32_t>(*n));
        } else if (const auto* f = std::get_if<float>(&a)) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, f, sizeof bits);
            writeU32(out, bits);
        } else {
            writeString(out, std::get<std::string>(a));
        }
    }
    return out;
}

} // namespace vthost::osc
