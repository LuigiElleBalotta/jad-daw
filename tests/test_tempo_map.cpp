#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include "lpc/tempo_map.h"

using lpc::kPPQ;
using lpc::TempoMap;
using lpc::Ticks;

TEST_CASE("tempo map: default is 120 bpm", "[tempo]") {
    const TempoMap m;
    REQUIRE(m.bpmAt(0) == 120.0);
    // one beat at 120 bpm is 0.5 s = 24000 samples at 48 kHz
    REQUIRE(m.ticksToSamples(kPPQ, 48000.0) == Catch::Approx(24000.0));
    REQUIRE(m.samplesToTicks(24000.0, 48000.0) == kPPQ);
}

TEST_CASE("tempo map: tempo change", "[tempo]") {
    TempoMap m;
    REQUIRE(m.setTempo(kPPQ, 60.0));
    // beat 1 -> 24000 samples (120 bpm), beat 2 -> +48000 samples (60 bpm)
    REQUIRE(m.ticksToSamples(2 * kPPQ, 48000.0) == Catch::Approx(72000.0));
    REQUIRE(m.samplesToTicks(72000.0, 48000.0) == 2 * kPPQ);
    REQUIRE(m.samplesToTicks(24000.0, 48000.0) == kPPQ);
    REQUIRE(m.bpmAt(kPPQ - 1) == 120.0);
    REQUIRE(m.bpmAt(kPPQ) == 60.0);
}

TEST_CASE("tempo map: round trip across many tempo changes", "[tempo]") {
    TempoMap m;
    for (int i = 1; i <= 20; ++i) REQUIRE(m.setTempo(i * kPPQ * 4, 60.0 + 7.0 * i));
    for (Ticks t = 0; t < 90 * kPPQ; t += 137) {
        const double s = m.ticksToSamples(t, 44100.0);
        // floor() of a value that is mathematically t may land one tick low because of rounding
        const Ticks back = m.samplesToTicks(s + 1e-6, 44100.0);
        REQUIRE(back == t);
    }
}

TEST_CASE("tempo map: validation", "[tempo]") {
    TempoMap m;
    REQUIRE_FALSE(m.setTempo(0, 19.9));
    REQUIRE_FALSE(m.setTempo(0, 1000.0));
    REQUIRE_FALSE(m.setTempo(0, std::numeric_limits<double>::quiet_NaN()));
    REQUIRE_FALSE(m.setTempo(-1, 120.0));
    REQUIRE_FALSE(m.removeTempo(0));  // the tick-0 event can never be removed
    REQUIRE(m.setTempo(kPPQ, 90.0));
    REQUIRE(m.tempoEventAt(kPPQ).value() == 90.0);
    REQUIRE_FALSE(m.tempoEventAt(kPPQ + 1).has_value());
    REQUIRE(m.removeTempo(kPPQ));
    REQUIRE(m.tempos().size() == 1);
}

TEST_CASE("tempo map: extreme values stay finite", "[tempo]") {
    TempoMap m;
    REQUIRE(m.setTempo(0, 999.0));
    REQUIRE(std::isfinite(m.ticksToSamples(1'000'000'000, 192000.0)));
    REQUIRE(m.ticksToSamples(-kPPQ, 48000.0) < 0.0);  // negative ticks use the first tempo
}

TEST_CASE("tempo map: signatures and fromEvents", "[tempo]") {
    TempoMap m;
    REQUIRE(m.setSignature(4 * kPPQ, 3, 4));
    REQUIRE_FALSE(m.setSignature(0, 0, 4));
    REQUIRE_FALSE(m.setSignature(0, 4, 3));  // denominator must be a power of two
    REQUIRE(m.signatures().size() == 2);

    const auto built = TempoMap::fromEvents({{kPPQ, 100.0}, {0, 120.0}}, {});
    REQUIRE(built.tempos().front().tick == 0);  // sorted
    REQUIRE(built.signatures().size() == 1);
    REQUIRE_THROWS_AS(TempoMap::fromEvents({{kPPQ, 100.0}}, {}), std::invalid_argument);  // no tick-0 event
}
