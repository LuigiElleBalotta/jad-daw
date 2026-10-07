#pragma once
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace lpc {

struct Uuid {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;

    static Uuid random();                       // thread-local generator seeded from random_device
    static Uuid random(std::mt19937_64& rng);   // deterministic, for tests and demo projects
    static std::optional<Uuid> parse(std::string_view s);

    std::string toString() const;
    bool isNull() const { return hi == 0 && lo == 0; }

    auto operator<=>(const Uuid&) const = default;
};

}  // namespace lpc

template <>
struct std::hash<lpc::Uuid> {
    std::size_t operator()(const lpc::Uuid& u) const noexcept {
        return std::hash<std::uint64_t>{}(u.hi) ^ (std::hash<std::uint64_t>{}(u.lo) * 0x9e3779b97f4a7c15ULL);
    }
};
