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
