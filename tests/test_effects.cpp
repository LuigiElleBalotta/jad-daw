#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include "lpc/audio/effects.h"
#include "lpc/effect_specs.h"
#include "lpc/processor_ids.h"
#include "lpc/validation.h"

using namespace lpc;
using namespace lpc::audio;

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

ProcessorRef ref(const char* id, std::map<std::string, double> params = {}) { return ProcessorRef{id, std::move(params), "", "", false}; }

std::vector<float> sine(double freq, double amp, int frames) {
    std::vector<float> v(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) v[static_cast<std::size_t>(i)] = static_cast<float>(amp * std::sin(2 * kPi * freq * i / kSr));
    return v;
}

// the peak of the last `tail` frames
float peakOf(const std::vector<float>& v, int tail) {
    float p = 0;
    for (std::size_t i = v.size() - static_cast<std::size_t>(tail); i < v.size(); ++i) p = std::max(p, std::abs(v[i]));
    return p;
}

std::vector<float> run(IProcessor& p, std::vector<float> in) {
    std::vector<float> r = in;
    for (std::size_t at = 0; at < in.size(); at += 512) {
        const int n = static_cast<int>(std::min<std::size_t>(512, in.size() - at));
        p.process(in.data() + at, r.data() + at, n);
    }
    return in;
}

}  // namespace

TEST_CASE("effects: every spec has a default inside its range and the factory builds it", "[effects]") {
    REQUIRE(effectSpecs().size() >= 7);
    for (const EffectSpec& s : effectSpecs()) {
        REQUIRE(isKnownEffect(s.id));
        for (const EffectParam& p : s.params) {
            REQUIRE(p.min < p.max);
            REQUIRE(p.def >= p.min);
            REQUIRE(p.def <= p.max);
        }
        if (s.id != kProcGain) REQUIRE(makeBuiltinEffect(ref(s.id.c_str()), kSr) != nullptr);
    }
    REQUIRE_FALSE(isKnownEffect("builtin.nope"));
}

TEST_CASE("effects: validation checks every parameter against its spec", "[effects][validation]") {
    REQUIRE_FALSE(checkInsert(ref(kProcCompressor, {{"threshold", -30}, {"ratio", 4}})).has_value());
    REQUIRE(checkInsert(ref(kProcCompressor, {{"ratio", 99}})).has_value());     // out of range
    REQUIRE(checkInsert(ref(kProcCompressor, {{"cutoff", 1}})).has_value());     // not a parameter of the compressor
    REQUIRE_FALSE(checkInsert(ref(kProcEq, {{"m1Gain", 12}})).has_value());
    REQUIRE(checkInsert(ref(kProcEq, {{"m1Gain", 40}})).has_value());
    REQUIRE_FALSE(checkInsert(ref(kProcGain, {{"gainDb", 3}})).has_value());
}

TEST_CASE("eq: a flat EQ changes nothing and a band boosts around its frequency by its gain", "[effects][eq]") {
    ProcessorRef flat = ref(kProcEq);
    for (const double f : {50.0, 500.0, 5000.0, 15000.0}) REQUIRE(eqResponseDb(flat, kSr, f) == Catch::Approx(0.0).margin(0.01));
    ProcessorRef boost = ref(kProcEq, {{"m2Freq", 1000}, {"m2Gain", 12}, {"m2Q", 1}});
    REQUIRE(eqResponseDb(boost, kSr, 1000) == Catch::Approx(12.0).margin(0.1));
    REQUIRE(eqResponseDb(boost, kSr, 60) == Catch::Approx(0.0).margin(0.8));
    ProcessorRef shelves = ref(kProcEq, {{"lowFreq", 200}, {"lowGain", -12}, {"highFreq", 6000}, {"highGain", 6}});
    REQUIRE(eqResponseDb(shelves, kSr, 30) == Catch::Approx(-12.0).margin(0.5));
    REQUIRE(eqResponseDb(shelves, kSr, 18000) == Catch::Approx(6.0).margin(0.5));
    // the processor agrees with the response it reports
    auto eq = makeBuiltinEffect(boost, kSr);
    const auto out = run(*eq, sine(1000, 0.1, 9600));
    REQUIRE(20 * std::log10(peakOf(out, 2000) / 0.1) == Catch::Approx(12.0).margin(0.3));
}

TEST_CASE("compressor: a loud sine is reduced as the ratio says, a quiet one is left alone", "[effects][compressor]") {
    ProcessorRef r = ref(kProcCompressor, {{"threshold", -20}, {"ratio", 4}, {"knee", 0}, {"attack", 1}, {"release", 50}, {"makeup", 0}});
    auto c = makeBuiltinEffect(r, kSr);
    const double amp = std::pow(10.0, -8.0 / 20.0);               // -8 dBFS peak
    const auto loud = run(*c, sine(440, amp, 24000));
    REQUIRE(20 * std::log10(peakOf(loud, 2000)) == Catch::Approx(-17.0).margin(1.0));   // -20 + 12/4
    REQUIRE(c->reductionDb() == Catch::Approx(9.0f).margin(1.5f));     // the meter agrees: -8 dB in, -17 dB out
    auto quiet = makeBuiltinEffect(r, kSr);
    const auto soft = run(*quiet, sine(440, std::pow(10.0, -30.0 / 20.0), 24000));
    REQUIRE(20 * std::log10(peakOf(soft, 2000)) == Catch::Approx(-30.0).margin(0.3));
    ProcessorRef makeup = ref(kProcCompressor, {{"threshold", 0}, {"makeup", 6}});           // nothing to compress: only the make-up gain
    auto m = makeBuiltinEffect(makeup, kSr);
    const auto lifted = run(*m, sine(440, 0.1, 9600));
    REQUIRE(peakOf(lifted, 2000) == Catch::Approx(0.2f).margin(0.01f));
}

TEST_CASE("limiter: nothing leaves above the ceiling and the latency is reported", "[effects][limiter]") {
    ProcessorRef r = ref(kProcLimiter, {{"ceiling", -6}, {"gain", 12}});
    auto lim = makeBuiltinEffect(r, kSr);
    REQUIRE(lim->latencySamples() > 0);
    const float ceiling = static_cast<float>(std::pow(10.0, -6.0 / 20.0));
    const auto out = run(*lim, sine(200, 0.9, 24000));
    for (const float v : out) REQUIRE(std::abs(v) <= ceiling + 1e-6f);
    REQUIRE(peakOf(out, 2000) > ceiling * 0.8f);                    // it limits, it does not silence
}

TEST_CASE("gate: a quiet signal is cut by the reduction, a loud one passes", "[effects][gate]") {
    ProcessorRef r = ref(kProcGate, {{"threshold", -30}, {"range", -60}, {"attack", 0.5}, {"hold", 0}, {"release", 20}});
    auto g = makeBuiltinEffect(r, kSr);
    const auto quiet = run(*g, sine(300, std::pow(10.0, -50.0 / 20.0), 24000));
    REQUIRE(20 * std::log10(peakOf(quiet, 2000)) < -90.0);          // -50 dB reduced by 60 dB more
    auto g2 = makeBuiltinEffect(r, kSr);
    const auto loud = run(*g2, sine(300, 0.5, 24000));
    REQUIRE(peakOf(loud, 2000) == Catch::Approx(0.5f).margin(0.02f));
}

TEST_CASE("delay: an impulse comes back after the time with the feedback and the mix", "[effects][delay]") {
    ProcessorRef r = ref(kProcDelay, {{"time", 100}, {"feedback", 50}, {"mix", 100}});
    auto d = makeBuiltinEffect(r, kSr);
    std::vector<float> in(24000, 0.0f);
    in[0] = 1.0f;
    const auto out = run(*d, in);
    REQUIRE(out[0] == Catch::Approx(0.0f).margin(1e-6f));           // mix 100 %: no dry signal
    REQUIRE(out[4800] == Catch::Approx(1.0f).margin(1e-6f));        // 100 ms at 48 kHz
    REQUIRE(out[9600] == Catch::Approx(0.5f).margin(1e-6f));        // the first repeat, halved by the feedback
    ProcessorRef dry = ref(kProcDelay, {{"time", 100}, {"mix", 0}});
    auto d0 = makeBuiltinEffect(dry, kSr);
    const auto same = run(*d0, in);
    REQUIRE(same[0] == 1.0f);
    REQUIRE(same[4800] == 0.0f);
}

TEST_CASE("reverb: a tail follows an impulse, and mix 0 leaves the signal alone", "[effects][reverb]") {
    ProcessorRef r = ref(kProcReverb, {{"mix", 50}, {"size", 80}});
    auto rv = makeBuiltinEffect(r, kSr);
    std::vector<float> in(48000, 0.0f);
    in[0] = 1.0f;
    const auto out = run(*rv, in);
    float tail = 0;
    for (std::size_t i = 4000; i < 20000; ++i) tail += std::abs(out[i]);
    REQUIRE(tail > 0.01f);                                           // there is a tail
    for (const float v : out) REQUIRE(std::isfinite(v));
    ProcessorRef off = ref(kProcReverb, {{"mix", 0}});
    auto r0 = makeBuiltinEffect(off, kSr);
    const auto same = run(*r0, sine(300, 0.3, 4800));
    REQUIRE(peakOf(same, 1000) == Catch::Approx(0.3f).margin(0.01f));
}
