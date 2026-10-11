#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include "lpc/flex_pitch.h"
#include "lpc/flex_time.h"

using namespace lpc;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kRate = 48000;

// a held tone (220 Hz with a few harmonics), plus short noise bursts at the given seconds
WavData toneWithHits(double seconds, const std::vector<double>& hits, bool tone = true, int channels = 1) {
    WavData d;
    d.sampleRate = kRate;
    d.channels = channels;
    const int n = static_cast<int>(seconds * kRate);
    std::vector<float> m(static_cast<std::size_t>(n), 0.0f);
    if (tone)
        for (int i = 0; i < n; ++i) {
            double v = 0.0;
            for (int h = 1; h <= 4; ++h) v += std::sin(2 * kPi * 220.0 * h * i / kRate) / h;
            m[static_cast<std::size_t>(i)] = static_cast<float>(0.12 * v);
        }
    unsigned s = 777;
    for (double t : hits) {
        const int a = static_cast<int>(t * kRate);
        for (int i = 0; i < kRate / 25 && a + i < n; ++i) {
            s = s * 1664525u + 1013904223u;
            const double noise = static_cast<double>(s >> 8) / 8388608.0 - 1.0;
            m[static_cast<std::size_t>(a + i)] += static_cast<float>(0.8 * noise * std::exp(-i / (kRate * 0.006)));
        }
    }
    for (float v : m)
        for (int c = 0; c < channels; ++c) d.samples.push_back(v);
    return d;
}

bool near(const std::vector<std::int64_t>& found, double seconds, double tolerance = 0.012) {
    for (std::int64_t f : found)
        if (std::abs(static_cast<double>(f) / kRate - seconds) <= tolerance) return true;
    return false;
}

std::int64_t frames(double seconds) { return static_cast<std::int64_t>(seconds * kRate); }

}  // namespace

TEST_CASE("time map: anchors are cleaned up and both directions agree", "[flex][time]") {
    TimeMap m(1000, {{500, 700}, {300, 200}, {0, 5}, {1000, 1000}, {400, 150} /* goes back in dst: dropped */});
    REQUIRE(m.points().size() == 4);  // 0, 300->200, 500->700, 1000
    REQUIRE(m.toDest(300) == Catch::Approx(200));
    REQUIRE(m.toDest(400) == Catch::Approx(450));
    REQUIRE(m.toSource(450) == Catch::Approx(400));
    REQUIRE(m.toSource(700) == Catch::Approx(500));
    REQUIRE(m.toDest(750) == Catch::Approx(850));
    REQUIRE(m.toSource(m.toDest(123.0)) == Catch::Approx(123.0));
    REQUIRE(TimeMap(1000, {}).isIdentity());
}

TEST_CASE("transients: noise bursts are found where they start, a steady tone has none", "[flex][time]") {
    const WavData d = toneWithHits(2.5, {0.3, 0.8, 1.3, 1.9});
    const auto found = findTransients(d);
    REQUIRE(found.size() == 4);
    for (double t : {0.3, 0.8, 1.3, 1.9}) REQUIRE(near(found, t));
    REQUIRE(findTransients(toneWithHits(2.0, {})).empty());
    REQUIRE(findTransients(toneWithHits(2.0, {}, false)).empty());  // silence
}

TEST_CASE("flex time: without markers the sound is untouched, and the length is always kept", "[flex][time]") {
    const WavData d = toneWithHits(1.5, {0.5});
    const WavData same = renderFlexTime(d, {}, findTransients(d), FlexTimeParams{});
    REQUIRE(same.samples == d.samples);
    for (FlexMode mode : {FlexMode::Slicing, FlexMode::Rhythmic, FlexMode::Monophonic, FlexMode::Polyphonic, FlexMode::Tempophone, FlexMode::Speed}) {
        FlexTimeParams p;
        p.mode = mode;
        const WavData out = renderFlexTime(d, {{frames(0.5), frames(0.62)}}, findTransients(d), p);
        REQUIRE(out.samples.size() == d.samples.size());
        REQUIRE(out.channels == 1);
    }
}

TEST_CASE("flex time: a marker moves its transient, the ones before and after stay", "[flex][time]") {
    const WavData d = toneWithHits(2.5, {0.5, 1.0, 1.5, 2.0});
    const auto hits = findTransients(d);
    REQUIRE(hits.size() == 4);
    for (FlexMode mode : {FlexMode::Monophonic, FlexMode::Polyphonic, FlexMode::Slicing, FlexMode::Rhythmic}) {
        FlexTimeParams p;
        p.mode = mode;
        // the hit at 1.0 s is pulled to 1.15 s; its neighbours are pinned (a click in the lower half of a region sets three markers)
        const WavData out = renderFlexTime(d, {{hits[0], hits[0]}, {hits[1], hits[1] + frames(0.15)}, {hits[2], hits[2]}}, hits, p);
        const auto moved = findTransients(out);
        INFO(flexModeName(mode));
        REQUIRE(near(moved, 0.5));
        REQUIRE(near(moved, 1.15, 0.02));
        REQUIRE(near(moved, 1.5));
        REQUIRE(near(moved, 2.0));
        REQUIRE_FALSE(near(moved, 1.0, 0.03));
    }
}

TEST_CASE("flex time: the stretching modes keep the pitch, speed does not", "[flex][time]") {
    const WavData d = toneWithHits(3.0, {0.5, 2.5});
    const auto hits = findTransients(d);
    // the middle of the sound is stretched to 1.4 times its length, the rest squeezed to fit
    std::vector<FlexAnchor> anchors{{frames(0.75), frames(0.5)}, {frames(2.25), frames(2.6)}};
    for (FlexMode mode : {FlexMode::Monophonic, FlexMode::Polyphonic}) {
        FlexTimeParams p;
        p.mode = mode;
        const WavData out = renderFlexTime(d, anchors, hits, p);
        // the stretched part, 1.0 s .. 2.0 s of the output, is the tone at 220 Hz (A3 = MIDI 57)
        WavData part;
        part.sampleRate = kRate;
        part.channels = 1;
        part.samples.assign(out.samples.begin() + frames(1.0), out.samples.begin() + frames(2.0));
        const PitchAnalysis a = analysePitch(part);
        INFO(flexModeName(mode));
        REQUIRE(a.notes.size() == 1);
        REQUIRE(a.notes[0].pitch == Catch::Approx(57.0).margin(0.2));
    }
    FlexTimeParams speed;
    speed.mode = FlexMode::Speed;
    const WavData fast = renderFlexTime(d, anchors, hits, speed);
    WavData part;
    part.sampleRate = kRate;
    part.channels = 1;
    part.samples.assign(fast.samples.begin() + frames(1.0), fast.samples.begin() + frames(2.0));
    const PitchAnalysis a = analysePitch(part);
    REQUIRE(a.notes.size() == 1);
    REQUIRE(std::abs(a.notes[0].pitch - 57.0) > 2.0);  // the middle plays at 0.71 times the speed: five semitones lower
}

TEST_CASE("flex time: stereo stays stereo and both channels are treated alike", "[flex][time]") {
    const WavData d = toneWithHits(1.5, {0.5}, true, 2);
    const auto hits = findTransients(d);
    FlexTimeParams p;
    const WavData out = renderFlexTime(d, {{hits[0], hits[0] + frames(0.1)}}, hits, p);
    REQUIRE(out.channels == 2);
    REQUIRE(out.samples.size() == d.samples.size());
    for (std::size_t i = 0; i + 1 < out.samples.size(); i += 2) REQUIRE(out.samples[i] == out.samples[i + 1]);
}

TEST_CASE("flex time: Automatic picks Slicing for hits, Monophonic for a tone", "[flex][time]") {
    std::vector<double> drums;
    for (double t = 0.1; t < 3.0; t += 0.2) drums.push_back(t);
    REQUIRE(chooseFlexMode(toneWithHits(3.2, drums, false)) == FlexMode::Slicing);
    REQUIRE(chooseFlexMode(toneWithHits(2.0, {})) == FlexMode::Monophonic);
    REQUIRE(flexModeFromName(flexModeName(FlexMode::Rhythmic)) == FlexMode::Rhythmic);
    REQUIRE(flexModeFromName("nonsense") == FlexMode::Off);
}
