#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "lpc/drum_replace.h"

using namespace lpc;

namespace {
constexpr int kRate = 48000;

// noise bursts at the given seconds, with the given gains, over silence
WavData bursts(double seconds, const std::vector<double>& at, const std::vector<double>& gains) {
    WavData d;
    d.sampleRate = kRate;
    d.channels = 1;
    d.samples.assign(static_cast<std::size_t>(seconds * kRate), 0.0f);
    unsigned s = 4;
    for (std::size_t k = 0; k < at.size(); ++k)
        for (int i = 0; i < 2400; ++i) {
            s = s * 1664525u + 1013904223u;
            d.samples[static_cast<std::size_t>(at[k] * kRate) + static_cast<std::size_t>(i)] +=
                static_cast<float>(gains[k] * (static_cast<double>(s >> 8) / 8388608.0 - 1.0) * std::exp(-i / 500.0));
        }
    return d;
}
double energy(const WavData& d, double from, double to) {
    double e = 0;
    for (std::int64_t i = static_cast<std::int64_t>(from * kRate); i < static_cast<std::int64_t>(to * kRate); ++i) e += d.samples[static_cast<std::size_t>(i)] * d.samples[static_cast<std::size_t>(i)];
    return e;
}
}  // namespace

TEST_CASE("drum replacement: the hits of a drum track are found with their relative level", "[drums]") {
    const WavData d = bursts(4.0, {0.5, 1.0, 1.5, 2.0, 2.5}, {0.8, 0.4, 0.8, 0.4, 0.8});
    const std::vector<DrumHit> hits = findDrumHits(d, 0.5, 60.0);
    REQUIRE(hits.size() == 5);
    for (std::size_t i = 0; i < hits.size(); ++i) CHECK(std::abs(static_cast<double>(hits[i].frame) / kRate - (0.5 + 0.5 * static_cast<double>(i))) < 0.02);
    CHECK(hits[1].level < 0.7 * hits[0].level);  // the soft hits read softer
}

TEST_CASE("drum replacement: Replace takes the original away and puts the sound there, Double keeps both", "[drums]") {
    const WavData d = bursts(3.0, {0.5, 1.5}, {0.8, 0.8});
    const std::vector<DrumHit> hits = findDrumHits(d, 0.5, 60.0);
    REQUIRE(hits.size() == 2);
    const WavData kick = synthDrum(DrumSound::Kick, kRate);
    DrumReplaceParams p;
    p.velocity = 0.0;
    const WavData replaced = replaceDrums(d, hits, kick, p);
    REQUIRE(replaced.frames() == d.frames());
    // the burst is gone (the original lasts about 50 ms, the kick is far lower in frequency but long): compare the first 20 ms after the hit
    WavData nothing = kick;
    std::fill(nothing.samples.begin(), nothing.samples.end(), 0.0f);
    const WavData gone = replaceDrums(d, hits, nothing, p);       // a silent sound shows what was taken away
    CHECK(energy(gone, 0.502, 0.56) < 0.001 * energy(d, 0.502, 0.56));
    CHECK(energy(replaced, 0.52, 0.54) > energy(d, 0.52, 0.54));   // and the kick is there instead
    // between the hits nothing changes
    CHECK(energy(replaced, 0.9, 1.4) == energy(d, 0.9, 1.4));
    // Double adds to the original
    p.replace = false;
    const WavData doubled = replaceDrums(d, hits, kick, p);
    double diff = 0;
    for (std::int64_t i = static_cast<std::int64_t>(0.5 * kRate); i < static_cast<std::int64_t>(0.6 * kRate); ++i)
        diff += std::abs(doubled.samples[static_cast<std::size_t>(i)] - d.samples[static_cast<std::size_t>(i)] - kick.samples[static_cast<std::size_t>(i - hits[0].frame)]);
    CHECK(diff < 1.0);   // the original plus the sound (up to the 3 ms fade at the end of a cut sound)
    // the sound of every kind exists and is not silent
    for (const char* name : {"kick", "snare", "tom", "hihat", "clap"}) {
        const WavData s = synthDrum(drumSoundFromName(name), kRate);
        CHECK(s.frames() > kRate / 20);
        CHECK(energy(s, 0.0, 0.03) > 0.0);
    }
}

TEST_CASE("drum replacement: velocity follows the level of the original hits", "[drums]") {
    const WavData d = bursts(3.0, {0.5, 1.5}, {0.8, 0.2});
    const std::vector<DrumHit> hits = findDrumHits(d, 0.5, 60.0);
    REQUIRE(hits.size() == 2);
    DrumReplaceParams p;
    p.velocity = 1.0;
    p.replace = false;
    const WavData silent = [&] { WavData z = d; std::fill(z.samples.begin(), z.samples.end(), 0.0f); return z; }();
    const WavData out = replaceDrums(silent, hits, synthDrum(DrumSound::Snare, kRate), p);
    CHECK(energy(out, 1.5, 1.6) < 0.5 * energy(out, 0.5, 0.6));
}
