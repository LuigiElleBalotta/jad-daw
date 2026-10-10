#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "lpc/audio_ops.h"

using namespace lpc;

namespace {

constexpr double kPi = 3.14159265358979323846;

WavData sine(double freq, double seconds, double amp = 0.5, int rate = 48000, int channels = 1) {
    WavData d;
    d.sampleRate = rate;
    d.channels = channels;
    const int frames = static_cast<int>(seconds * rate);
    d.samples.resize(static_cast<std::size_t>(frames * channels));
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c) d.samples[static_cast<std::size_t>(i * channels + c)] = static_cast<float>(amp * std::sin(2 * kPi * freq * i / rate));
    return d;
}

// the frequency of a clean tone, from its upward zero crossings
double frequencyOf(const WavData& d) {
    int crossings = 0;
    for (std::size_t i = static_cast<std::size_t>(d.channels); i < d.samples.size(); i += static_cast<std::size_t>(d.channels))
        if (d.samples[i - static_cast<std::size_t>(d.channels)] < 0 && d.samples[i] >= 0) ++crossings;
    return crossings / (static_cast<double>(d.frames()) / d.sampleRate);
}

}  // namespace

TEST_CASE("audio ops: normalize reaches the target peak, silence stays silent, gain and reverse are exact", "[audioops]") {
    WavData d = sine(440, 0.2, 0.25);
    const double applied = normalize(d, -6.0);
    REQUIRE(peakOf(d) == Catch::Approx(0.5012f).margin(0.002f));      // -6 dBFS
    REQUIRE(applied == Catch::Approx(20 * std::log10(0.5012 / 0.25)).margin(0.05));
    WavData quiet;
    quiet.sampleRate = 48000;
    quiet.channels = 1;
    quiet.samples.assign(100, 0.0f);
    REQUIRE(normalize(quiet) == 0.0);
    WavData r;
    r.sampleRate = 48000;
    r.channels = 2;
    r.samples = {1, 10, 2, 20, 3, 30};
    reverse(r);
    REQUIRE(r.samples == std::vector<float>({3, 30, 2, 20, 1, 10}));
    applyGain(r, 6.0206);
    REQUIRE(r.samples[0] == Catch::Approx(6.0f).margin(0.01f));
}

TEST_CASE("audio ops: fades ramp the ends and silence blanks a range", "[audioops]") {
    WavData d;
    d.sampleRate = 48000;
    d.channels = 1;
    d.samples.assign(1000, 1.0f);
    fade(d, true, 200);
    fade(d, false, 100);
    REQUIRE(d.samples[0] < 0.02f);
    REQUIRE(d.samples[100] == Catch::Approx(std::sin(0.5 * kPi * 100.5 / 200.0)).margin(0.01));
    REQUIRE(d.samples[500] == 1.0f);
    REQUIRE(d.samples[999] < 0.02f);
    silence(d, 400, 600);
    REQUIRE(d.samples[399] == 1.0f);
    REQUIRE(d.samples[400] == 0.0f);
    REQUIRE(d.samples[599] == 0.0f);
    REQUIRE(d.samples[600] == 1.0f);
}

TEST_CASE("audio ops: time stretch changes the length and keeps the pitch", "[audioops][stretch]") {
    const WavData d = sine(440, 1.0, 0.5, 48000, 2);
    for (const double ratio : {0.5, 1.5, 2.0}) {
        const WavData s = timeStretch(d, ratio);
        REQUIRE(std::abs(s.frames() - static_cast<std::int64_t>(48000 * ratio)) <= 1);
        REQUIRE(s.channels == 2);
        REQUIRE(frequencyOf(s) == Catch::Approx(440.0).margin(8.0));   // the pitch did not move
        REQUIRE(peakOf(s) > 0.4f);                                       // the level is kept
        REQUIRE(peakOf(s) < 0.6f);
    }
}

TEST_CASE("audio ops: pitch shift keeps the length and moves the pitch by the interval", "[audioops][pitch]") {
    const WavData d = sine(440, 1.0, 0.5);
    const WavData up = pitchShift(d, 12.0);
    REQUIRE(up.frames() == d.frames());
    REQUIRE(frequencyOf(up) == Catch::Approx(880.0).margin(15.0));
    const WavData down = pitchShift(d, -12.0);
    REQUIRE(down.frames() == d.frames());
    REQUIRE(frequencyOf(down) == Catch::Approx(220.0).margin(8.0));
    REQUIRE(pitchShift(d, 0.0).samples == d.samples);                  // nothing to do
}

TEST_CASE("audio ops: findSounds returns the loud stretches, joins short gaps and drops short blips", "[audioops][strip]") {
    WavData d;
    d.sampleRate = 48000;
    d.channels = 1;
    d.samples.assign(48000 * 2, 0.0f);
    auto burst = [&](double from, double to) {
        for (int i = static_cast<int>(from * 48000); i < static_cast<int>(to * 48000); ++i) d.samples[static_cast<std::size_t>(i)] = 0.4f * std::sin(0.3f * static_cast<float>(i));
    };
    burst(0.2, 0.5);
    burst(0.52, 0.7);      // 20 ms later: the same sound
    burst(1.2, 1.205);     // a 5 ms click: too short
    burst(1.5, 1.8);
    const auto sounds = findSounds(d, -30.0, 100.0, 20.0, 0.0);
    REQUIRE(sounds.size() == 2);
    REQUIRE(sounds[0].first == Catch::Approx(0.2 * 48000).margin(500));
    REQUIRE(sounds[0].second == Catch::Approx(0.7 * 48000).margin(500));
    REQUIRE(sounds[1].first == Catch::Approx(1.5 * 48000).margin(500));
    REQUIRE(findSounds(d, -3.0).empty());                              // a threshold above everything
}
