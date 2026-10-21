#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include "lpc/audio/effects.h"
#include "lpc/graph_builder.h"
#include "lpc/match_eq.h"
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
    REQUIRE(checkInsert(ref(kProcCompressor, {{"ratio", 999}})).has_value());    // out of range
    REQUIRE(checkInsert(ref(kProcCompressor, {{"cutoff", 1}})).has_value());     // not a parameter of the compressor
    REQUIRE_FALSE(checkInsert(ref(kProcEq, {{"p1Gain", 12}})).has_value());
    REQUIRE(checkInsert(ref(kProcEq, {{"p1Gain", 40}})).has_value());
    REQUIRE_FALSE(checkInsert(ref(kProcGain, {{"gainDb", 3}})).has_value());
}

TEST_CASE("eq: a flat EQ changes nothing and a band boosts around its frequency by its gain", "[effects][eq]") {
    ProcessorRef flat = ref(kProcEq);
    for (const double f : {50.0, 500.0, 5000.0, 15000.0}) REQUIRE(eqResponseDb(flat, kSr, f) == Catch::Approx(0.0).margin(0.01));
    ProcessorRef boost = ref(kProcEq, {{"p2Freq", 1000}, {"p2Gain", 12}, {"p2Q", 1}});
    REQUIRE(eqResponseDb(boost, kSr, 1000) == Catch::Approx(12.0).margin(0.1));
    REQUIRE(eqResponseDb(boost, kSr, 60) == Catch::Approx(0.0).margin(0.8));
    ProcessorRef shelves = ref(kProcEq, {{"lsFreq", 200}, {"lsGain", -12}, {"hsFreq", 6000}, {"hsGain", 6}});
    REQUIRE(eqResponseDb(shelves, kSr, 30) == Catch::Approx(-12.0).margin(0.8));
    REQUIRE(eqResponseDb(shelves, kSr, 18000) == Catch::Approx(6.0).margin(0.8));
    // the processor agrees with the response it reports
    auto eq = makeBuiltinEffect(boost, kSr);
    const auto out = run(*eq, sine(1000, 0.1, 9600));
    REQUIRE(20 * std::log10(peakOf(out, 2000) / 0.1) == Catch::Approx(12.0).margin(0.3));
}

TEST_CASE("eq: the cuts are Butterworth filters of the chosen slope, a band that is off does nothing, the master gain scales everything", "[effects][eq]") {
    ProcessorRef low = ref(kProcEq, {{"lcOn", 1}, {"lcFreq", 1000}, {"lcSlope", 4}});  // 4 x 6 = 24 dB per octave
    REQUIRE(eqResponseDb(low, kSr, 1000) == Catch::Approx(-3.01).margin(0.2));     // the corner of a Butterworth
    REQUIRE(eqResponseDb(low, kSr, 500) == Catch::Approx(-24.0).margin(1.5));      // one octave below: about the slope
    REQUIRE(eqResponseDb(low, kSr, 8000) == Catch::Approx(0.0).margin(0.1));
    ProcessorRef gentle = ref(kProcEq, {{"lcOn", 1}, {"lcFreq", 1000}, {"lcSlope", 1}});  // 6 dB per octave
    REQUIRE(eqResponseDb(gentle, kSr, 500) == Catch::Approx(-6.99).margin(0.3));
    ProcessorRef high = ref(kProcEq, {{"hcOn", 1}, {"hcFreq", 2000}, {"hcSlope", 8}});
    REQUIRE(eqResponseDb(high, kSr, 2000) == Catch::Approx(-3.01).margin(0.3));
    REQUIRE(eqResponseDb(high, kSr, 4000) < -40.0);
    REQUIRE(eqResponseDb(high, kSr, 200) == Catch::Approx(0.0).margin(0.1));
    ProcessorRef off = ref(kProcEq, {{"p2On", 0}, {"p2Gain", 12}, {"p2Freq", 1000}, {"masterGain", -6}});
    REQUIRE(eqResponseDb(off, kSr, 1000) == Catch::Approx(-6.0).margin(0.01));      // the band is off: only the master gain is left
    // a resonant cut (Q 4) peaks above 0 dB near its corner
    ProcessorRef res = ref(kProcEq, {{"lcOn", 1}, {"lcFreq", 1000}, {"lcSlope", 2}, {"lcQ", 4}});
    double peak = -99;
    for (double f = 800; f < 1600; f += 20) peak = std::max(peak, eqResponseDb(res, kSr, f));
    REQUIRE(peak > 6.0);
}

TEST_CASE("compressor: a loud sine is reduced as the ratio says, a quiet one is left alone", "[effects][compressor]") {
    ProcessorRef r = ref(kProcCompressor, {{"threshold", -20}, {"ratio", 4}, {"knee", 0}, {"attack", 1}, {"release", 50}, {"gain", 0}, {"autoGain", 0}, {"autoRelease", 0}, {"peakRms", 0}});
    auto c = makeBuiltinEffect(r, kSr);
    const double amp = std::pow(10.0, -8.0 / 20.0);               // -8 dBFS peak
    const auto loud = run(*c, sine(440, amp, 24000));
    REQUIRE(20 * std::log10(peakOf(loud, 2000)) == Catch::Approx(-17.0).margin(1.0));   // -20 + 12/4
    REQUIRE(c->reductionDb() == Catch::Approx(9.0f).margin(1.5f));     // the meter agrees: -8 dB in, -17 dB out
    auto quiet = makeBuiltinEffect(r, kSr);
    const auto soft = run(*quiet, sine(440, std::pow(10.0, -30.0 / 20.0), 24000));
    REQUIRE(20 * std::log10(peakOf(soft, 2000)) == Catch::Approx(-30.0).margin(0.3));
    ProcessorRef makeup = ref(kProcCompressor, {{"threshold", 0}, {"gain", 6}, {"autoGain", 0}, {"peakRms", 0}});           // nothing to compress: only the make-up gain
    auto m = makeBuiltinEffect(makeup, kSr);
    const auto lifted = run(*m, sine(440, 0.1, 9600));
    REQUIRE(peakOf(lifted, 2000) == Catch::Approx(0.2f).margin(0.01f));
}

TEST_CASE("compressor: the side chain filter takes the bass out of what the detector hears, Listen plays the filtered signal", "[effects][compressor]") {
    const double amp = std::pow(10.0, -8.0 / 20.0);
    const std::map<std::string, double> base{{"threshold", -20}, {"ratio", 4}, {"knee", 0}, {"attack", 1}, {"release", 50}, {"gain", 0}, {"autoGain", 0}, {"autoRelease", 0}, {"peakRms", 0}};
    auto withKey = [&](double state) {
        auto m = base;
        m["scState"] = state; m["scMode"] = 2; m["scFreq"] = 2000;   // a high-pass at 2 kHz
        return makeBuiltinEffect(ref(kProcCompressor, m), kSr);
    };
    auto off = withKey(0), on = withKey(2), listen = withKey(1);
    const auto bass = sine(100, amp, 24000);
    REQUIRE(20 * std::log10(peakOf(run(*off, bass), 2000)) < -15.0);       // reduced
    REQUIRE(20 * std::log10(peakOf(run(*on, bass), 2000)) > -9.0);         // the detector no longer hears the bass: left alone
    REQUIRE(peakOf(run(*listen, bass), 2000) < 0.02f);                     // and Listen plays what is left of it
}

TEST_CASE("compressor: auto gain makes up what the reduction takes at 0 dBFS, the mix blends, the limiter holds its threshold", "[effects][compressor]") {
    // threshold -20, ratio 4: a signal at 0 dBFS loses 15 dB, auto gain lifts the output by the same 15 dB
    ProcessorRef a = ref(kProcCompressor, {{"threshold", -20}, {"ratio", 4}, {"knee", 0}, {"attack", 1}, {"release", 50}, {"autoGain", 1}, {"autoRelease", 0}, {"peakRms", 0}});
    auto c = makeBuiltinEffect(a, kSr);
    const auto out = run(*c, sine(440, std::pow(10.0, -30.0 / 20.0), 24000));     // below the threshold: untouched but made up
    REQUIRE(20 * std::log10(peakOf(out, 2000)) == Catch::Approx(-30.0 + 15.0).margin(0.3));
    ProcessorRef half = ref(kProcCompressor, {{"threshold", 0}, {"gain", 6}, {"autoGain", 0}, {"peakRms", 0}, {"mix", 50}});
    auto h = makeBuiltinEffect(half, kSr);
    REQUIRE(peakOf(run(*h, sine(440, 0.1, 9600)), 2000) == Catch::Approx(0.15f).margin(0.01f));   // half dry (0.1), half +6 dB (0.2)
    ProcessorRef lim = ref(kProcCompressor, {{"threshold", 0}, {"gain", 12}, {"autoGain", 0}, {"peakRms", 0}, {"limiter", 1}, {"limiterThreshold", -6}});
    auto l = makeBuiltinEffect(lim, kSr);
    const auto limited = run(*l, sine(440, 0.5, 9600));                                            // +12 dB would be 2.0: the limiter stops it at -6 dBFS
    REQUIRE(peakOf(limited, 2000) <= static_cast<float>(std::pow(10.0, -6.0 / 20.0)) * 1.02f);
    ProcessorRef clip = ref(kProcCompressor, {{"threshold", 0}, {"gain", 12}, {"autoGain", 0}, {"peakRms", 0}, {"outputDistortion", 3}});
    auto k = makeBuiltinEffect(clip, kSr);
    REQUIRE(peakOf(run(*k, sine(440, 0.5, 9600)), 2000) <= 1.0f);                                  // clip: nothing above full scale
}

TEST_CASE("compressor: the circuit types behave differently", "[effects][compressor]") {
    // the FET circuit reacts to a burst faster than the Platinum one with the same attack time
    auto reductionAfter = [](int circuit, int frames) {
        ProcessorRef r = ref(kProcCompressor, {{"circuit", static_cast<double>(circuit)}, {"threshold", -30}, {"ratio", 8}, {"knee", 0}, {"attack", 8}, {"release", 100},
                                                {"autoGain", 0}, {"autoRelease", 0}, {"peakRms", 0}});
        auto c = makeBuiltinEffect(r, kSr);
        std::vector<float> burst = sine(440, 0.5, frames);
        run(*c, burst);
        return c->reductionDb();
    };
    REQUIRE(reductionAfter(5, 240) > reductionAfter(0, 240) + 1.0f);       // 5 ms into the burst
    REQUIRE(reductionAfter(6, 240) < reductionAfter(0, 240));              // the opto cell is slower to grip
    // the dual-release SSL circuit lets go more slowly than the single-release Platinum one after a long loud passage
    auto afterRelease = [](int circuit) {
        ProcessorRef r = ref(kProcCompressor, {{"circuit", static_cast<double>(circuit)}, {"threshold", -30}, {"ratio", 8}, {"knee", 0}, {"attack", 1}, {"release", 40},
                                                {"autoGain", 0}, {"autoRelease", 0}, {"peakRms", 0}});
        auto c = makeBuiltinEffect(r, kSr);
        run(*c, sine(440, 0.5, 48000));
        run(*c, std::vector<float>(2400, 0.0f));                           // 50 ms of silence
        return c->reductionDb();
    };
    REQUIRE(afterRelease(4) > afterRelease(0));
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

TEST_CASE("limiter: the lookahead is the latency, the adaptive limiter holds its ceiling too", "[effects][limiter]") {
    ProcessorRef r = ref(kProcLimiter, {{"lookahead", 2}});
    REQUIRE(makeBuiltinEffect(r, kSr)->latencySamples() == 96);      // 2 ms at 48 kHz
    ProcessorRef none = ref(kProcLimiter, {{"lookahead", 0}});
    REQUIRE(makeBuiltinEffect(none, kSr)->latencySamples() == 1);    // never less than a sample
    ProcessorRef a = ref(kProcAdaptiveLimiter, {{"gain", 12}, {"ceiling", -2}, {"lookahead", 5}});
    auto lim = makeBuiltinEffect(a, kSr);
    REQUIRE(lim->latencySamples() == 240);
    const float ceiling = static_cast<float>(std::pow(10.0, -2.0 / 20.0));  // the Out Ceiling goes from -2 to 0 dB
    for (const float v : run(*lim, sine(200, 0.9, 24000))) REQUIRE(std::abs(v) <= ceiling + 1e-6f);
}

TEST_CASE("limiter: the meter reads the reduction (bit-pattern log, as Logic's)", "[effects][limiter]") {
    ProcessorRef r = ref(kProcLimiter, {{"ceiling", -6}, {"gain", 12}, {"lookahead", 1}, {"release", 500}});
    auto lim = makeBuiltinEffect(r, kSr);
    run(*lim, sine(200, 0.9, 24000));
    // the signal asks for 20 log10(0.9 * 3.98 / 0.501) = 17 dB at its peaks; the meter shows what the gain is at the end of the block
    REQUIRE(lim->reductionDb() > 10.0f);
    REQUIRE(lim->reductionDb() < 18.5f);
    auto idle = makeBuiltinEffect(ref(kProcLimiter), kSr);
    run(*idle, sine(200, 0.05, 4800));
    REQUIRE(idle->reductionDb() == Catch::Approx(0.0f).margin(0.05f));
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

TEST_CASE("gate: hysteresis keeps it open, the side-chain filter and the lookahead", "[effects][gate]") {
    // opens at -30 dB; a signal at -33 dB keeps an open gate open with 6 dB of hysteresis but not with 1 dB
    auto level = [](double hysteresis) {
        ProcessorRef r = ref(kProcGate, {{"threshold", -30}, {"hysteresis", hysteresis}, {"range", -80}, {"attack", 0.5}, {"hold", 0}, {"release", 5}});
        auto g = makeBuiltinEffect(r, kSr);
        run(*g, sine(300, std::pow(10.0, -20.0 / 20.0), 9600));                                  // loud: opens
        const auto tail = run(*g, sine(300, std::pow(10.0, -33.0 / 20.0), 12000));
        return 20 * std::log10(peakOf(tail, 2000));
    };
    REQUIRE(level(-6) == Catch::Approx(-33.0).margin(0.5));
    REQUIRE(level(-1) < -60.0);
    // a low rumble does not open a gate whose side chain is high-passed at 2 kHz; the same rumble opens the plain gate
    ProcessorRef keyed = ref(kProcGate, {{"threshold", -30}, {"range", -80}, {"attack", 0.5}, {"hold", 0}, {"release", 5}, {"keyFilter", 1}, {"keyLowCut", 2000}});
    auto g = makeBuiltinEffect(keyed, kSr);
    REQUIRE(20 * std::log10(peakOf(run(*g, sine(80, 0.5, 24000)), 2000)) < -60.0);
    ProcessorRef plain = ref(kProcGate, {{"threshold", -30}, {"range", -80}, {"attack", 0.5}, {"hold", 0}, {"release", 5}});
    auto p = makeBuiltinEffect(plain, kSr);
    REQUIRE(peakOf(run(*p, sine(80, 0.5, 24000)), 2000) == Catch::Approx(0.5f).margin(0.02f));
    ProcessorRef ahead = ref(kProcGate, {{"lookahead", 5}});
    REQUIRE(makeBuiltinEffect(ahead, kSr)->latencySamples() == 240);
}

TEST_CASE("match eq: bands, the correction and its fade", "[effects][matcheq]") {
    REQUIRE(matchEqBandFreq(17) == Catch::Approx(1000.0));
    REQUIRE(matchEqBandFreq(0) == Catch::Approx(19.95).margin(0.1));
    REQUIRE(matchEqBandFreq(30) == Catch::Approx(19952.0).margin(5.0));
    // a flat source against a tilted target: the correction follows the tilt, the means are taken out
    std::vector<double> source(kMatchEqBands, -40.0), target(kMatchEqBands);
    for (int i = 0; i < kMatchEqBands; ++i) target[static_cast<std::size_t>(i)] = -40.0 - 0.3 * i;     // darker towards the top
    const auto c = matchEqCorrection(source, target, 1.0, 0.0, 1e6, 99.0);
    double mean = 0;
    for (const double v : c) mean += v;
    REQUIRE(mean / kMatchEqBands == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(c[0] > c[30]);
    REQUIRE(c[0] - c[30] == Catch::Approx(0.3 * 30).margin(1e-6));
    const auto half = matchEqCorrection(source, target, 0.5, 0.0, 1e6, 99.0);
    REQUIRE(half[0] == Catch::Approx(c[0] * 0.5));
    const auto faded = matchEqCorrection(source, target, 1.0, 400.0, 1e6, 99.0);                      // nothing below 200 Hz
    REQUIRE(faded[0] == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(faded[matchEqBandFreq(10) > 200 ? 9 : 10] == Catch::Approx(0.0).margin(1e-9));
    const auto limited = matchEqCorrection(source, target, 1.0, 0.0, 1e6, 3.0);
    for (const double v : limited) REQUIRE(std::abs(v) <= 3.0 + 1e-9);
    // a curve on a sparse log axis is averaged into the bands
    std::vector<double> f{20, 200, 2000, 20000}, m{-30, -40, -50, -60};
    const auto bands = matchEqBands(f, m);
    REQUIRE(bands[17] == Catch::Approx(-50.0).margin(5.0));
    // the insert applies the corrections: +12 dB on band 17 (1 kHz)
    ProcessorRef insert = ref(kProcMatchEq, {{"b17", 12}});
    REQUIRE(makeBuiltinEffect(insert, kSr) != nullptr);
    auto mq = makeBuiltinEffect(insert, kSr);
    REQUIRE(20 * std::log10(peakOf(run(*mq, sine(1000, 0.1, 9600)), 2000) / 0.1) == Catch::Approx(12.0).margin(0.5));
    ProcessorRef amount = ref(kProcMatchEq, {{"b17", 12}, {"amount", 50}});
    auto ma = makeBuiltinEffect(amount, kSr);
    REQUIRE(20 * std::log10(peakOf(run(*ma, sine(1000, 0.1, 9600)), 2000) / 0.1) == Catch::Approx(6.0).margin(0.5));
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

TEST_CASE("synth: the waveforms play the right pitch, the envelope reaches the sustain and releases to silence", "[synth]") {
    Synth s(kSr);
    SynthParams p;
    p.wave = 2;                 // saw
    p.attackMs = 1;
    p.decayMs = 20;
    p.sustain = 0.5f;
    p.releaseMs = 20;
    p.level = 0.5f;
    s.setParams(p);
    s.noteOn(69, 127);          // A4 = 440 Hz
    std::vector<float> l(24000, 0.0f), r(24000, 0.0f);
    s.render(l.data(), r.data(), 480);                      // a few blocks are rendered in slices of at most 512
    s.render(l.data() + 480, r.data() + 480, 23520 > 512 ? 512 : 23520);
    std::vector<float> body(9600, 0.0f), unused(9600, 0.0f);
    for (int at = 0; at < 9600; at += 480) s.render(body.data() + at, unused.data() + at, 480);
    int crossings = 0;                                       // a saw with a rising edge crosses zero once per period going up
    for (std::size_t i = 1; i < body.size(); ++i)
        if (body[i - 1] < 0 && body[i] >= 0) ++crossings;
    REQUIRE(crossings >= 17);                                // 0.2 s of 440 Hz: about 88 periods... counted on the rising edges of a band-limited saw
    float peak = 0;
    for (const float v : body) peak = std::max(peak, std::abs(v));
    REQUIRE(peak == Catch::Approx(0.5f * 0.5f).margin(0.08f));   // level 0.5 * sustain 0.5 (the saw swings to about +-1)
    REQUIRE(s.active());
    s.noteOff(69);
    std::vector<float> tail(4800, 0.0f), tail2(4800, 0.0f);
    for (int at = 0; at < 4800; at += 480) s.render(tail.data() + at, tail2.data() + at, 480);
    REQUIRE_FALSE(s.active());                               // the 20 ms release is over
    REQUIRE(std::abs(tail[4700]) < 1e-6f);
}

TEST_CASE("synth: the instrument specs and the parameters of a synth track reach the engine", "[synth]") {
    REQUIRE(isKnownInstrument("builtin.sine"));
    REQUIRE(isKnownInstrument("builtin.synth"));
    REQUIRE_FALSE(isKnownInstrument("builtin.nope"));
    ProcessorRef sine{kProcSine, {}, "", "", false};
    REQUIRE(synthParamsOf(sine).releaseMs == Catch::Approx(5.0f));        // the old sine: a 2 ms attack, a 5 ms release
    ProcessorRef synth{kProcSynth, {{"wave", 3}, {"attack", 50}, {"sustain", 25}, {"cutoff", 2000}, {"level", -6}}, "", "", false};
    const SynthParams sp = synthParamsOf(synth);
    REQUIRE(sp.wave == 3);
    REQUIRE(sp.attackMs == Catch::Approx(50.0f));
    REQUIRE(sp.sustain == Catch::Approx(0.25f));
    REQUIRE(sp.cutoffHz == Catch::Approx(2000.0f));
    REQUIRE(sp.level == Catch::Approx(0.501f).margin(0.01f));
    synth.params["attack"] = 99999;                                        // out of range: clamped
    REQUIRE(synthParamsOf(synth).attackMs == Catch::Approx(2000.0f));
}
