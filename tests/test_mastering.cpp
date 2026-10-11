#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <random>
#include "lpc/audio/effects.h"
#include "lpc/loudness.h"
#include "lpc/mastering.h"
#include "lpc/processor_ids.h"

using namespace lpc;

namespace {
constexpr int kRate = 48000;
constexpr double kPi = 3.14159265358979323846;

WavData sine(double freq, double db, double seconds) {
    WavData d;
    d.sampleRate = kRate;
    d.channels = 2;
    const int n = static_cast<int>(seconds * kRate);
    const double a = std::pow(10.0, db / 20.0);
    for (int i = 0; i < n; ++i) {
        const float v = static_cast<float>(a * std::sin(2 * kPi * freq * i / kRate));
        d.samples.push_back(v);
        d.samples.push_back(v);
    }
    return d;
}
}  // namespace

TEST_CASE("loudness: a stereo sine at -20 dBFS reads about -20 LUFS, and silence reads nothing", "[mastering][loudness]") {
    const LoudnessStats s = measureLoudness(sine(997.0, -20.0, 10.0));
    CHECK(std::abs(s.integrated - (-20.0)) < 0.4);
    CHECK(std::abs(s.momentaryMax - s.integrated) < 0.3);
    CHECK(std::abs(s.truePeakDb - (-20.0)) < 0.3);
    CHECK(s.range < 0.5);
    WavData quiet;
    quiet.sampleRate = kRate;
    quiet.channels = 2;
    quiet.samples.assign(static_cast<std::size_t>(kRate * 4 * 2), 0.0f);
    CHECK(measureLoudness(quiet).integrated < -100.0);
    // 10 dB louder reads 10 LU louder
    CHECK(std::abs(measureLoudness(sine(997.0, -10.0, 10.0)).integrated - s.integrated - 10.0) < 0.1);
}

TEST_CASE("loudness: a quiet passage under the gate does not pull the integrated loudness down", "[mastering][loudness]") {
    WavData a = sine(997.0, -20.0, 10.0);
    const WavData b = sine(997.0, -60.0, 10.0);
    a.samples.insert(a.samples.end(), b.samples.begin(), b.samples.end());
    CHECK(std::abs(measureLoudness(a).integrated - (-20.0)) < 0.5);
}

TEST_CASE("mastering: a dark, quiet mix gets a lift in the highs and a gain towards -14 LUFS", "[mastering]") {
    // low-passed noise: lots of bass, little top
    WavData d;
    d.sampleRate = kRate;
    d.channels = 2;
    std::mt19937 rng(5);
    std::normal_distribution<double> n(0.0, 1.0);
    double lp1 = 0, lp2 = 0;
    for (int i = 0; i < kRate * 12; ++i) {
        lp1 += 0.05 * (n(rng) - lp1);
        lp2 += 0.05 * (lp1 - lp2);
        const float v = static_cast<float>(0.8 * lp2);
        d.samples.push_back(v);
        d.samples.push_back(v);
    }
    const MasteringAnalysis a = analyseMastering(d);
    CHECK(a.before.integrated < -14.0);
    CHECK(std::abs(a.gainDb - (kMasteringTargetLufs - a.before.integrated)) < 0.01);
    CHECK(a.eqDb[kMasteringBands - 1] > 1.0);   // the top is lifted
    CHECK(a.eqDb[0] < a.eqDb[kMasteringBands - 1]);
    for (double g : a.eqDb) CHECK(std::abs(g) <= 6.0);
}

TEST_CASE("mastering processor: nothing leaves above -1 dB, bypass keeps the sound, and the loudness knob moves the level", "[mastering]") {
    ProcessorRef ref;
    ref.processorId = kProcMastering;
    ref.params = {{"gain", 18.0}, {"autoEq", 0.0}};
    auto run = [&](const ProcessorRef& r, double db) {
        auto p = audio::makeBuiltinEffect(r, kRate);
        REQUIRE(p);
        std::vector<float> l(kRate), rr(kRate);
        for (int i = 0; i < kRate; ++i) l[static_cast<std::size_t>(i)] = rr[static_cast<std::size_t>(i)] = static_cast<float>(std::pow(10.0, db / 20.0) * std::sin(2 * kPi * 440.0 * i / kRate));
        p->process(l.data(), rr.data(), kRate);
        float peak = 0, rms = 0;
        for (int i = kRate / 2; i < kRate; ++i) { peak = std::max(peak, std::abs(l[static_cast<std::size_t>(i)])); rms += l[static_cast<std::size_t>(i)] * l[static_cast<std::size_t>(i)]; }
        return std::pair<float, float>{peak, std::sqrt(rms / (kRate / 2))};
    };
    const auto loud = run(ref, -10.0);
    CHECK(loud.first <= std::pow(10.0, -1.0 / 20.0) + 1e-3);
    ProcessorRef lower = ref;
    lower.params["gain"] = 0.0;
    const auto quiet = run(lower, -30.0);
    ProcessorRef lowerKnob = lower;
    lowerKnob.params["loudness"] = 100.0;
    const auto knob = run(lowerKnob, -30.0);
    CHECK(knob.second > quiet.second * 1.8);  // +6 dB is a factor of 2
    ProcessorRef bypass = ref;
    bypass.params["bypass"] = 1.0;
    const auto passed = run(bypass, -10.0);
    CHECK(std::abs(passed.first - std::pow(10.0, -10.0 / 20.0)) < 0.01);
}
