#include "lpc/uuid.h"

#include <cstdio>

namespace lpc {

Uuid Uuid::random(std::mt19937_64& rng) {
    Uuid u{rng(), rng()};
    u.hi = (u.hi & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;  // version 4
    u.lo = (u.lo & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;  // RFC 4122 variant
    return u;
}

Uuid Uuid::random() {
    static thread_local std::mt19937_64 rng{[] {
        std::random_device rd;
        return (std::uint64_t(rd()) << 32) ^ std::uint64_t(rd());
    }()};
    return random(rng);
}

std::string Uuid::toString() const {
    char buf[37];
    std::snprintf(buf, sizeof buf, "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(hi >> 32),
                  static_cast<unsigned>((hi >> 16) & 0xffff), static_cast<unsigned>(hi & 0xffff),
                  static_cast<unsigned>(lo >> 48), static_cast<unsigned long long>(lo & 0xffffffffffffULL));
    return buf;
}

std::optional<Uuid> Uuid::parse(std::string_view s) {
    if (s.size() != 36) return std::nullopt;
    Uuid u;
    int digits = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return std::nullopt;
            continue;
        }
        const char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return std::nullopt;
        std::uint64_t& part = digits < 16 ? u.hi : u.lo;
        part = (part << 4) | std::uint64_t(d);
        ++digits;
    }
    return u;
}

}  // namespace lpc
