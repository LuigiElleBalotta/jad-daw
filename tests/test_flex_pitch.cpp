#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "lpc/fft.h"
#include "lpc/flex_pitch.h"

using namespace lpc;

namespace {

constexpr double kPi = 3.14159265358979323846;

// a voice-like tone: the first harmonics with falling amplitude; `hz(t)` gives the frequency at second t
template <class F>
void addTone(std::vector<float>& out, int rate, double from, double to, F hz, double amp = 0.3) {
    double phase = 0.0;
    const int a = static_cast<int>(from * rate), b = static_cast<int>(to * rate);
    for (int i = a; i < b; ++i) {
        phase += 2 * kPi * hz(static_cast<double>(i) / rate - from) / rate;
        double v = 0.0;
        for (int h = 1; h <= 5; ++h) v += std::sin(h * phase) / h;
        out[static_cast<std::size_t>(i)] += static_cast<float>(amp * v);
    }
}

WavData toWav(const std::vector<float>& mono, int rate, int channels = 1) {
    WavData d;
    d.sampleRate = rate;
    d.channels = channels;
    for (float v : mono)
        for (int c = 0; c < channels; ++c) d.samples.push_back(v);
    return d;
}

}  // namespace

TEST_CASE("fft: forward then inverse gives the signal back, and a sine lands in its bin", "[flex][fft]") {
    Fft fft(64);
    std::vector<std::complex<double>> x(64);
    for (int i = 0; i < 64; ++i) x[static_cast<std::size_t>(i)] = std::sin(2 * kPi * 5 * i / 64.0) + 0.25 * std::cos(2 * kPi * 11 * i / 64.0);
    const auto original = x;
    fft.forward(x.data());
    REQUIRE(std::abs(x[5]) == Catch::Approx(32.0).margin(1e-9));
    REQUIRE(std::abs(x[11]) == Catch::Approx(8.0).margin(1e-9));
    REQUIRE(std::abs(x[7]) < 1e-9);
    fft.inverse(x.data());
    for (int i = 0; i < 64; ++i) REQUIRE(std::abs(x[static_cast<std::size_t>(i)] - original[static_cast<std::size_t>(i)]) < 1e-12);
}

TEST_CASE("flex pitch: a steady tone is one note at the right pitch", "[flex][pitch]") {
    const int rate = 48000;
    std::vector<float> v(static_cast<std::size_t>(1.5 * rate), 0.0f);
    addTone(v, rate, 0.1, 1.4, [](double) { return 220.0; });
    const PitchAnalysis a = analysePitch(toWav(v, rate));
    REQUIRE(a.notes.size() == 1);
    REQUIRE(a.notes[0].pitch == Catch::Approx(57.0).margin(0.15));
    REQUIRE(a.notes[0].start == Catch::Approx(0.1 * rate).margin(0.05 * rate));
    REQUIRE(a.notes[0].length == Catch::Approx(1.3 * rate).margin(0.1 * rate));
    REQUIRE(a.midiAt(rate / 2) == Catch::Approx(57.0).margin(0.15));
    REQUIRE(a.midiAt(static_cast<std::int64_t>(0.02 * rate)) == 0.0);  // before the note: not pitched
}

TEST_CASE("flex pitch: two notes with a gap are two notes; the sample rate and channel count do not matter", "[flex][pitch]") {
    for (int rate : {44100, 48000}) {
        std::vector<float> v(static_cast<std::size_t>(2.0 * rate), 0.0f);
        addTone(v, rate, 0.1, 0.9, [](double) { return 220.0; });
        addTone(v, rate, 1.0, 1.9, [](double) { return 261.6256; });
        const PitchAnalysis a = analysePitch(toWav(v, rate, 2));
        REQUIRE(a.notes.size() == 2);
        REQUIRE(a.notes[0].pitch == Catch::Approx(57.0).margin(0.15));
        REQUIRE(a.notes[1].pitch == Catch::Approx(60.0).margin(0.15));
        REQUIRE(a.notes[1].start > a.notes[0].start + a.notes[0].length - rate / 20);
    }
}

TEST_CASE("flex pitch: vibrato stays inside one note, whose pitch is its centre", "[flex][pitch]") {
    const int rate = 48000;
    std::vector<float> v(static_cast<std::size_t>(2.0 * rate), 0.0f);
    // 330 Hz (E4 = 64) with a vibrato of +-40 cents at 5.5 Hz
    addTone(v, rate, 0.1, 1.9, [](double t) { return 329.6276 * std::pow(2.0, 0.4 / 12.0 * std::sin(2 * kPi * 5.5 * t)); });
    const PitchAnalysis a = analysePitch(toWav(v, rate));
    REQUIRE(a.notes.size() == 1);
    REQUIRE(a.notes[0].pitch == Catch::Approx(64.0).margin(0.2));
    // the pitch track follows the vibrato
    double lo = 1e9, hi = -1e9;
    for (std::int64_t p = rate / 4; p < rate * 3 / 2; p += 200) {
        const double m = a.midiAt(p);
        REQUIRE(m > 0);
        lo = std::min(lo, m);
        hi = std::max(hi, m);
    }
    REQUIRE(hi - lo > 0.4);
    REQUIRE(hi - lo < 1.1);
}

TEST_CASE("flex pitch: silence and noise hold no notes", "[flex][pitch]") {
    const int rate = 44100;
    std::vector<float> silence(static_cast<std::size_t>(rate), 0.0f);
    REQUIRE(analysePitch(toWav(silence, rate)).notes.empty());
    std::vector<float> noise(static_cast<std::size_t>(rate), 0.0f);
    unsigned s = 12345;
    for (float& x : noise) {
        s = s * 1664525u + 1013904223u;
        x = (static_cast<float>(s >> 8) / 8388608.0f - 1.0f) * 0.3f;
    }
    REQUIRE(analysePitch(toWav(noise, rate)).notes.empty());
    REQUIRE(analysePitch(toWav({0.1f, 0.2f}, rate)).notes.empty());  // too short to say anything
}
