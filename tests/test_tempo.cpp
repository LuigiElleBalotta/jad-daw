#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <random>
#include "lpc/flex_model.h"
#include "lpc/model_json.h"
#include "lpc/tempo_analysis.h"

using namespace lpc;

namespace {

constexpr int kRate = 44100;
std::mt19937_64 gRng(7);

// a pulse of noise at each of `at` seconds, an accent on every fourth one, over a quiet tone
WavData pulses(double seconds, const std::vector<double>& at, int accentEvery = 4, int accentFrom = 0) {
    WavData d;
    d.sampleRate = kRate;
    d.channels = 1;
    const int n = static_cast<int>(seconds * kRate);
    d.samples.assign(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) d.samples[static_cast<std::size_t>(i)] = static_cast<float>(0.02 * std::sin(0.05 * i));
    unsigned s = 99;
    int k = 0;
    for (double t : at) {
        const int a = static_cast<int>(t * kRate);
        const double level = ((k - accentFrom) % accentEvery == 0) ? 0.9 : 0.5;
        for (int i = 0; i < kRate / 20 && a + i < n; ++i) {
            s = s * 1664525u + 1013904223u;
            const double noise = static_cast<double>(s >> 8) / 8388608.0 - 1.0;
            d.samples[static_cast<std::size_t>(a + i)] += static_cast<float>(level * noise * std::exp(-i / (kRate * 0.008)));
        }
        ++k;
    }
    return d;
}

std::vector<double> grid(double bpm, double first, double until) {
    std::vector<double> v;
    for (double t = first; t < until; t += 60.0 / bpm) v.push_back(t);
    return v;
}

}  // namespace

TEST_CASE("tempo: a steady pulse gives its tempo and the beats where the hits are", "[tempo]") {
    const std::vector<double> hits = grid(100.0, 0.3, 19.5);
    const TempoAnalysis a = analyseTempo(pulses(20.0, hits));
    REQUIRE(a.bpm > 0);
    CHECK(std::abs(a.bpm - 100.0) < 1.5);
    REQUIRE(a.beats.size() >= 25);
    // every hit has a beat within 25 ms
    int matched = 0;
    for (double t : hits) {
        const std::int64_t frame = static_cast<std::int64_t>(t * kRate);
        for (std::int64_t b : a.beats)
            if (std::abs(b - frame) < kRate * 25 / 1000) { ++matched; break; }
    }
    CHECK(matched >= static_cast<int>(hits.size()) - 3);
}

TEST_CASE("tempo: the accented hit opens the bars", "[tempo]") {
    const std::vector<double> hits = grid(120.0, 0.2, 29.5);
    const TempoAnalysis a = analyseTempo(pulses(30.0, hits, 4, 2));  // the accent is on hits 2, 6, 10...
    REQUIRE(a.bpm > 0);
    REQUIRE(a.beats.size() > 8);
    // the beat found nearest to hit 2 is a downbeat: its index modulo four is firstDownbeat
    const std::int64_t frame = static_cast<std::int64_t>(hits[2] * kRate);
    std::size_t nearest = 0;
    for (std::size_t i = 0; i < a.beats.size(); ++i)
        if (std::abs(a.beats[i] - frame) < std::abs(a.beats[nearest] - frame)) nearest = i;
    CHECK(static_cast<int>(nearest % 4) == a.firstDownbeat);
}

TEST_CASE("tempo: a waltz (accent every third beat) reads as three beats a bar", "[tempo]") {
    const std::vector<double> hits = grid(110.0, 0.2, 29.5);
    const TempoAnalysis a = analyseTempo(pulses(30.0, hits, 3, 1));   // the accent on hits 1, 4, 7...
    REQUIRE(a.bpm > 0);
    CHECK(a.beatsPerBar == 3);
    const std::int64_t frame = static_cast<std::int64_t>(hits[1] * kRate);
    std::size_t nearest = 0;
    for (std::size_t i = 0; i < a.beats.size(); ++i)
        if (std::abs(a.beats[i] - frame) < std::abs(a.beats[nearest] - frame)) nearest = i;
    CHECK(static_cast<int>(nearest % 3) == a.firstDownbeat);
    // and four stays four
    CHECK(analyseTempo(pulses(30.0, grid(120.0, 0.2, 29.5), 4, 2)).beatsPerBar == 4);
}

TEST_CASE("tempo: silence and a very short sound have no tempo", "[tempo]") {
    WavData quiet;
    quiet.sampleRate = kRate;
    quiet.channels = 2;
    quiet.samples.assign(static_cast<std::size_t>(kRate * 10 * 2), 0.0f);
    CHECK(analyseTempo(quiet).bpm == 0.0);
    WavData tiny = pulses(0.2, {0.05});
    CHECK(analyseTempo(tiny).beats.empty());
}

TEST_CASE("flex beats: a window keeps the beats inside it and the bar phase, and two windows join back", "[tempo][flex][model]") {
    FlexData f;
    f.sourceMediaId = Uuid::random(gRng);
    f.sourceOffset = 0;
    f.sourceLength = 100000;
    FlexBeats b;
    b.follow = true;
    b.bpm = 110;
    b.firstDownbeat = 1;
    for (int i = 0; i < 10; ++i) b.beats.push_back(5000 + i * 10000);  // 5000, 15000, ..., 95000
    f.beats = b;
    // the first 40000 frames go: beats 5000 15000 25000 35000 are removed (four beats = one bar), the phase stays
    const auto w = flexWindow(f, 100000, 40000, 60000, 200000);
    REQUIRE(w.has_value());
    REQUIRE(w->beats.has_value());
    CHECK(w->beats->beats.size() == 6);
    CHECK(w->beats->beats.front() == 5000);
    CHECK(w->beats->firstDownbeat == 1);
    // and one beat fewer at the front moves the phase on
    const auto w2 = flexWindow(f, 100000, 10000, 90000, 200000);
    REQUIRE(w2.has_value());
    CHECK(w2->beats->beats.size() == 9);
    CHECK(w2->beats->firstDownbeat == 0);
    // the two halves of a cut join back into the beats they came from
    const auto left = flexWindow(f, 100000, 0, 50000, 200000);
    const auto right = flexWindow(f, 100000, 50000, 50000, 200000);
    REQUIRE(left.has_value());
    REQUIRE(right.has_value());
    const auto joined = flexJoin(*left, 50000, *right);
    REQUIRE(joined.has_value());
    REQUIRE(joined->beats.has_value());
    CHECK(joined->beats->beats == f.beats->beats);
    CHECK(flexProblem(*joined, 200000).empty());
    // json round trip
    nlohmann::json j = *joined;
    CHECK(j.get<FlexData>() == *joined);
    // beats out of order are refused
    FlexData bad = f;
    bad.beats->beats = {9000, 8000};
    CHECK(!flexProblem(bad, 200000).empty());
}

// Optional: LPC_REAL_AUDIO=<an audio file> prints what the tempo, mastering and drum analyses find in a real recording (not run when unset).
#include <cstdlib>
#include <iostream>
#include "lpc/audio_decode.h"
#include "lpc/mastering.h"
#include "lpc/audio/effects.h"
#include "lpc/processor_ids.h"
#include "lpc/drum_replace.h"
TEST_CASE("analyses on a real recording (LPC_REAL_AUDIO)", "[real]") {
    const char* path = std::getenv("LPC_REAL_AUDIO");
    if (!path) return;
    WavData d = decodeAudioFile(path);
    const std::int64_t keep = std::min<std::int64_t>(d.frames(), static_cast<std::int64_t>(d.sampleRate) * 60);
    d.samples.resize(static_cast<std::size_t>(keep * d.channels));   // the first minute
    const TempoAnalysis t = analyseTempo(d);
    const MasteringAnalysis m = analyseMastering(d);
    const auto hits = findDrumHits(d, 0.5, 80.0);
    std::cout << "tempo " << t.bpm << " bpm, confidence " << t.confidence << ", beats " << t.beats.size() << ", downbeat " << t.firstDownbeat << "\n"
              << "loudness " << m.before.integrated << " LUFS, range " << m.before.range << " LU, true peak " << m.before.truePeakDb << " dBTP, gain to -14: " << m.gainDb << " dB\n"
              << "eq:";
    for (double g : m.eqDb) std::cout << " " << g;
    std::cout << "\nhits found: " << hits.size() << "\n";
    CHECK(t.bpm > 0);
    // the mastering insert with what the analysis found, run over the recording
    ProcessorRef ref;
    ref.processorId = kProcMastering;
    ref.params["gain"] = m.gainDb;
    for (int b = 0; b < kMasteringBands; ++b) ref.params["eq" + std::to_string(b + 1)] = m.eqDb[static_cast<std::size_t>(b)];
    auto proc = audio::makeBuiltinEffect(ref, d.sampleRate);
    REQUIRE(proc);
    std::vector<float> l(static_cast<std::size_t>(d.frames())), r(l.size());
    for (std::int64_t i = 0; i < d.frames(); ++i) {
        l[static_cast<std::size_t>(i)] = d.samples[static_cast<std::size_t>(i * d.channels)];
        r[static_cast<std::size_t>(i)] = d.samples[static_cast<std::size_t>(i * d.channels + (d.channels > 1 ? 1 : 0))];
    }
    for (std::size_t at = 0; at < l.size(); at += 512) proc->process(l.data() + at, r.data() + at, static_cast<int>(std::min<std::size_t>(512, l.size() - at)));
    WavData out;
    out.sampleRate = d.sampleRate;
    out.channels = 2;
    for (std::size_t i = 0; i < l.size(); ++i) { out.samples.push_back(l[i]); out.samples.push_back(r[i]); }
    const LoudnessStats after = measureLoudness(out);
    std::cout << "mastered: " << after.integrated << " LUFS, range " << after.range << " LU, true peak " << after.truePeakDb << " dBTP" << std::endl;
    CHECK(after.truePeakDb <= -0.7);
    CHECK(after.integrated > m.before.integrated + 3.0);   // louder; -14 itself needs a peak-to-loudness ratio of about 13 dB, this recording has 22
}

TEST_CASE("tempo: a recording that speeds up from 90 to 130 bpm is followed by its beats", "[tempo]") {
    std::vector<double> hits;
    for (double t = 0.3; t < 40.5;) {
        hits.push_back(t);
        t += 60.0 / (90.0 + 40.0 * t / 40.0);
    }
    const TempoAnalysis a = analyseTempo(pulses(42.0, hits, 4, 0));
    REQUIRE(a.beats.size() > 40);
    // the tempo between beats in the first quarter and in the last quarter of the recording
    auto local = [&](double fromSec, double toSec) {
        std::vector<double> v;
        for (std::size_t i = 1; i < a.beats.size(); ++i) {
            const double at = static_cast<double>(a.beats[i]) / kRate;
            if (at >= fromSec && at < toSec) v.push_back(60.0 * kRate / static_cast<double>(a.beats[i] - a.beats[i - 1]));
        }
        std::sort(v.begin(), v.end());
        return v.empty() ? 0.0 : v[v.size() / 2];
    };
    const double early = local(0.0, 10.0), late = local(30.0, 41.0);
    WARN("early " << early << " bpm, late " << late << " bpm");
    CHECK(std::abs(early - 95.0) < 8.0);   // 90 at the start, 100 at ten seconds
    CHECK(std::abs(late - 125.0) < 8.0);   // 120 to 130
}
