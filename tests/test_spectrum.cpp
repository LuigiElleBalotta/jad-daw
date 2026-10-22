#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include <catch2/catch_approx.hpp>
#include "lpc/audio/live_meter.h"
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

TEST_CASE("live meter: a -20 dBFS 1 kHz sine reads about -20 LUFS, true peak -20 dB, and the correlation follows the channels", "[spectrum][loudness]") {
    constexpr double sr = 48000.0;
    std::vector<float> l(static_cast<std::size_t>(sr * 4)), r;
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = static_cast<float>(0.1 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * static_cast<double>(i) / sr));
    r = l;
    LiveMeter m(sr);
    m.process(l.data(), r.data(), l.size());
    // two identical channels of a 0.1 amplitude sine: 10 log10(2 * 0.1^2 / 2) - 0.691 = -20.7, and the K-weighting adds about 0.7 dB at 1 kHz
    REQUIRE(m.momentary() == Catch::Approx(-20.0).margin(0.6));
    REQUIRE(m.shortTerm() == Catch::Approx(m.momentary()).margin(0.1));
    REQUIRE(m.integrated() == Catch::Approx(m.momentary()).margin(0.1));
    REQUIRE(m.truePeakDb() == Catch::Approx(-20.0).margin(0.3));
    REQUIRE(m.correlation() == Catch::Approx(1.0).margin(0.001));
    LiveMeter opposite(sr);
    for (float& v : r) v = -v;
    opposite.process(l.data(), r.data(), l.size());
    REQUIRE(opposite.correlation() == Catch::Approx(-1.0).margin(0.001));
    LiveMeter silent(sr);
    std::vector<float> z(48000, 0.0f);
    silent.process(z.data(), z.data(), z.size());
    REQUIRE(silent.momentary() <= -199.0);
    REQUIRE(silent.correlation() == 0.0);
}

TEST_CASE("spectrum tap: take hands out the stereo samples once, in order", "[spectrum]") {
    SpectrumTap tap;
    const Uuid a{1, 2};
    tap.arm(a, 0, true);
    std::uint64_t cursor = 0;
    std::vector<float> l, r;
    REQUIRE_FALSE(tap.take(cursor, l, r));
    std::vector<float> in1(100, 0.25f), in2(100, -0.5f);
    tap.push(in1.data(), in2.data(), 100);
    REQUIRE(tap.take(cursor, l, r));
    REQUIRE(l.size() == 100);
    REQUIRE(l[10] == 0.25f);
    REQUIRE(r[10] == -0.5f);
    REQUIRE_FALSE(tap.take(cursor, l, r));
    tap.push(in2.data(), in1.data(), 40);
    REQUIRE(tap.take(cursor, l, r));
    REQUIRE(l.size() == 40);
    REQUIRE(l[0] == -0.5f);
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
