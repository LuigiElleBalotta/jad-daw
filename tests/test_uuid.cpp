#include <catch2/catch_test_macros.hpp>
#include <random>
#include <unordered_set>
#include "lpc/uuid.h"

using lpc::Uuid;

TEST_CASE("uuid: random ids are unique and non-null", "[uuid]") {
    std::unordered_set<Uuid> seen;
    for (int i = 0; i < 1000; ++i) {
        const Uuid u = Uuid::random();
        REQUIRE_FALSE(u.isNull());
        REQUIRE(seen.insert(u).second);
    }
}

TEST_CASE("uuid: string round trip", "[uuid]") {
    std::mt19937_64 rng(7);
    for (int i = 0; i < 100; ++i) {
        const Uuid u = Uuid::random(rng);
        const std::string s = u.toString();
        REQUIRE(s.size() == 36);
        REQUIRE(s[8] == '-');
        REQUIRE(s[14] == '4');  // version nibble
        const auto back = Uuid::parse(s);
        REQUIRE(back.has_value());
        REQUIRE(*back == u);
    }
}

TEST_CASE("uuid: seeded generator is deterministic", "[uuid]") {
    std::mt19937_64 a(42), b(42);
    REQUIRE(Uuid::random(a) == Uuid::random(b));
}

TEST_CASE("uuid: parse rejects malformed input", "[uuid]") {
    REQUIRE_FALSE(Uuid::parse("").has_value());
    REQUIRE_FALSE(Uuid::parse("not-a-uuid").has_value());
    REQUIRE_FALSE(Uuid::parse("zzzzzzzz-zzzz-zzzz-zzzz-zzzzzzzzzzzz").has_value());
    REQUIRE_FALSE(Uuid::parse("0123456789abcdef0123456789abcdef0123").has_value());  // no dashes
    REQUIRE_FALSE(Uuid::parse("00000000-0000-0000-0000-00000000000").has_value());   // 35 chars
}

TEST_CASE("uuid: null uuid", "[uuid]") {
    const Uuid n;
    REQUIRE(n.isNull());
    REQUIRE(n.toString() == "00000000-0000-0000-0000-000000000000");
    REQUIRE(Uuid::parse(n.toString()) == n);
}
