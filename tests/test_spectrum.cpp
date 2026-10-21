#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "lpc/audio/spectrum.h"

using namespace lpc;
using namespace lpc::audio;

TEST_CASE("spectrum: a tone lights its own band, the others stay low", "[spectrum]") {
    constexpr double sr = 48000.0;
    std::vector<float> x(4096);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 1000.0 * static_cast<double>(i) / sr));
    const auto bands = spectrumBands(x, sr, 64);
    REQUIRE(bands.size() == 64);
    const double ratio = std::pow(1000.0, 1.0 / 64.0);
    const int at = static_cast<int>(std::floor(std::log(1000.0 / 20.0) / std::log(ratio)));
    float loudest = -999;
    int loudestBand = -1;
    for (int b = 0; b < 64; ++b) if (bands[static_cast<std::size_t>(b)] > loudest) { loudest = bands[static_cast<std::size_t>(b)]; loudestBand = b; }
    REQUIRE(std::abs(loudestBand - at) <= 1);
    REQUIRE(loudest > -4.0f);
    REQUIRE(loudest < 0.0f);
    REQUIRE(bands[2] < -60.0f);     // ~25 Hz
    REQUIRE(bands[60] < -60.0f);    // ~16 kHz
}

TEST_CASE("spectrum tap: only the armed insert fills the ring", "[spectrum]") {
    SpectrumTap tap;
    const Uuid a{1, 2}, b{3, 4};
    REQUIRE_FALSE(tap.wants(a, 0, true));
    tap.arm(a, 1, true);
    REQUIRE(tap.wants(a, 1, true));
    REQUIRE_FALSE(tap.wants(a, 1, false));
    REQUIRE_FALSE(tap.wants(a, 0, true));
    REQUIRE_FALSE(tap.wants(b, 1, true));
    std::vector<float> l(512, 0.5f), r(512, 0.5f), out;
    for (int i = 0; i < 7; ++i) tap.push(l.data(), r.data(), 512);
    REQUIRE_FALSE(tap.latest(out));            // 3584 samples: not a window yet
    tap.push(l.data(), r.data(), 512);
    REQUIRE(tap.latest(out));
    REQUIRE(out.size() == SpectrumTap::kWindow);
    REQUIRE(out.back() == 0.5f);
    tap.disarm();
    REQUIRE_FALSE(tap.wants(a, 1, true));
}
