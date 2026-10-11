#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include "lpc/audio_analysis.h"

using namespace lpc;

namespace {
constexpr int kRate = 48000;
constexpr double kPi = 3.14159265358979323846;

WavData tone(double freq, double db, double seconds, bool leftOnly = false, bool inverted = false) {
    WavData d;
    d.sampleRate = kRate;
    d.channels = 2;
    const double a = std::pow(10.0, db / 20.0);
    for (int i = 0; i < static_cast<int>(seconds * kRate); ++i) {
        const float v = static_cast<float>(a * std::sin(2 * kPi * freq * i / kRate));
        d.samples.push_back(v);
        d.samples.push_back(leftOnly ? 0.0f : (inverted ? -v : v));
    }
    return d;
}
}  // namespace

TEST_CASE("analysis: a stereo sine reads its level, loudness, spectrum and stereo image", "[analysis]") {
    AnalysisOptions o;
    o.tempo = false;
    const AudioReport r = analyseAudio(tone(1000.0, -20.0, 6.0), o);
    CHECK(std::abs(r.peakDb - (-20.0)) < 0.1);
    CHECK(std::abs(r.rmsDb - (-23.0)) < 0.1);                 // a sine's RMS is 3 dB under its peak
    CHECK(std::abs(r.crestDb - 3.0) < 0.2);
    CHECK(std::abs(r.loudness.integrated - (-20.0)) < 0.5);
    CHECK(r.correlation > 0.99);
    CHECK(r.widthPercent < 0.1);
    CHECK(std::abs(r.balanceDb) < 0.1);
    CHECK(r.clippedSamples == 0);
    CHECK(std::abs(r.dcOffset) < 1e-3);
    // the 1 kHz band holds the energy
    std::size_t top = 0;
    for (std::size_t i = 0; i < r.bandDb.size(); ++i)
        if (r.bandDb[i] > r.bandDb[top]) top = i;
    CHECK(std::abs(r.bandCentres[top] - 1000.0) < 1.0);
    CHECK(std::abs(r.bandDb[top] - (-23.0)) < 1.0);
    CHECK(std::abs(r.centroidHz - 1000.0) < 60.0);
    CHECK(r.midPercent > 95.0);
    CHECK(!r.shortTermTimeline.empty());
    const auto j = toJson(r);
    CHECK(j["spectrum"]["thirdOctaveBands"].size() == 31);
    CHECK(j["loudness"].contains("truePeakDbtp"));
}

TEST_CASE("analysis: clipping, out of phase channels, one-sided image and silence are found", "[analysis]") {
    AnalysisOptions o;
    o.tempo = false;
    WavData loud = tone(200.0, 6.0, 3.0);                     // a sine at +6 dBFS written as floats, then clipped at full scale
    for (float& v : loud.samples) v = std::clamp(v, -1.0f, 1.0f);
    const AudioReport clipped = analyseAudio(loud, o);
    CHECK(clipped.clippedPercent > 10.0);
    const auto notes = describeReport(clipped);
    CHECK(std::any_of(notes.begin(), notes.end(), [](const std::string& s) { return s.find("Clipping") != std::string::npos; }));
    CHECK(clipped.lowPercent + clipped.lowMidPercent > 90.0);

    const AudioReport phase = analyseAudio(tone(500.0, -12.0, 3.0, false, true), o);
    CHECK(phase.correlation < -0.99);
    CHECK(phase.widthPercent > 99.0);
    const auto phaseNotes = describeReport(phase);
    CHECK(std::any_of(phaseNotes.begin(), phaseNotes.end(), [](const std::string& s) { return s.find("out of phase") != std::string::npos; }));

    const AudioReport left = analyseAudio(tone(500.0, -12.0, 3.0, true), o);
    CHECK(left.balanceDb > 20.0);

    WavData padded = tone(1000.0, -18.0, 2.0);
    WavData silence;
    silence.sampleRate = kRate;
    silence.channels = 2;
    silence.samples.assign(static_cast<std::size_t>(kRate * 3 * 2), 0.0f);
    padded.samples.insert(padded.samples.begin(), silence.samples.begin(), silence.samples.end());
    const AudioReport p = analyseAudio(padded, o);
    CHECK(std::abs(p.leadingSilenceSeconds - 3.0) < 0.3);
    CHECK(p.silencePercent > 50.0);
}

TEST_CASE("analysis: two sounds in the same band mask each other, two apart do not", "[analysis]") {
    AnalysisOptions o;
    o.tempo = false;
    o.timeline = false;
    const AudioReport bassA = analyseAudio(tone(100.0, -20.0, 3.0), o), bassB = analyseAudio(tone(100.0, -22.0, 3.0), o), air = analyseAudio(tone(6000.0, -20.0, 3.0), o);
    const MaskingReport same = compareMasking(bassA, bassB);
    const MaskingReport apart = compareMasking(bassA, air);
    CHECK(same.score > 0.1);
    CHECK(!same.worstHz.empty());
    CHECK(std::abs(same.worstHz.front() - 100.0) < 15.0);
    CHECK(apart.score < 0.05);
}

TEST_CASE("analysis: the tempo of a pulse train is part of the report", "[analysis]") {
    WavData d;
    d.sampleRate = kRate;
    d.channels = 1;
    d.samples.assign(static_cast<std::size_t>(kRate * 15), 0.0f);
    unsigned s = 5;
    for (double t = 0.3; t < 14.0; t += 0.5)  // 120 bpm
        for (int i = 0; i < 1500; ++i) {
            s = s * 1664525u + 1013904223u;
            d.samples[static_cast<std::size_t>(t * kRate) + static_cast<std::size_t>(i)] += static_cast<float>(0.7 * (static_cast<double>(s >> 8) / 8388608.0 - 1.0) * std::exp(-i / 300.0));
        }
    const AudioReport r = analyseAudio(d);
    CHECK(std::abs(r.bpm - 120.0) < 2.0);
    CHECK(r.channels == 1);
}

// Optional: LPC_REAL_AUDIO=<an audio file> prints the report of its first minute (not run when unset).
#include <cstdlib>
#include <iostream>
#include "lpc/audio_decode.h"
TEST_CASE("analysis: a real recording (LPC_REAL_AUDIO)", "[real][analysis]") {
    const char* path = std::getenv("LPC_REAL_AUDIO");
    if (!path) return;
    WavData d = decodeAudioFile(path);
    d.samples.resize(static_cast<std::size_t>(std::min<std::int64_t>(d.frames(), static_cast<std::int64_t>(d.sampleRate) * 60) * d.channels));
    const AudioReport r = analyseAudio(d);
    std::cout << toJson(r).dump(1).substr(0, 900) << "\n...\n";
    for (const std::string& s : describeReport(r)) std::cout << "- " << s << std::endl;
    CHECK(r.loudness.integrated > -60.0);
    CHECK(r.bpm > 70.0);
}
