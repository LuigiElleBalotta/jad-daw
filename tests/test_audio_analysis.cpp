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

#include "lpc/input_stats.h"

TEST_CASE("analysis: the noise floor is what lies between the loud parts", "[analysis]") {
    // 1 s of tone, 2 s of -60 dB noise-like hiss (a low sine stands in), 1 s of tone
    WavData d = tone(1000.0, -12.0, 1.0);
    const WavData hiss = tone(7000.0, -60.0, 2.0);
    d.samples.insert(d.samples.end(), hiss.samples.begin(), hiss.samples.end());
    const WavData again = tone(1000.0, -12.0, 1.0);
    d.samples.insert(d.samples.end(), again.samples.begin(), again.samples.end());
    AnalysisOptions o;
    o.tempo = false;
    const AudioReport r = analyseAudio(d, o);
    CHECK(std::abs(r.noiseFloorDb - (-63.0)) < 1.5);  // -60 dB peak of a sine is -63 dB RMS
    // digital silence has no floor
    const AudioReport silent = analyseAudio(tone(1000.0, -200.0, 2.0), o);
    CHECK(silent.noiseFloorDb < -199.0);
    // a continuous tone: the floor equals its level, so nothing is said about noise
    const AudioReport steady = analyseAudio(tone(1000.0, -20.0, 4.0), o);
    CHECK(std::abs(steady.noiseFloorDb - steady.rmsDb) < 1.0);
    for (const std::string& s : describeReport(steady)) CHECK(s.find("noise floor") == std::string::npos);
    // loud parts and a floor that is not quiet: it is said
    WavData noisy = tone(1000.0, -12.0, 1.0);
    const WavData floorNoise = tone(7000.0, -35.0, 2.0);
    noisy.samples.insert(noisy.samples.end(), floorNoise.samples.begin(), floorNoise.samples.end());
    const AudioReport nr = analyseAudio(noisy, o);
    bool said = false;
    for (const std::string& s : describeReport(nr)) said = said || s.find("noise floor") != std::string::npos;
    CHECK(said);
}

TEST_CASE("input stats: peak, usual level, floor, clipping, and the gain to change", "[analysis][input]") {
    InputLevelStats stats;
    CHECK(adviseInput(stats.summary()).messages.front().find("No readings") == 0);
    for (int i = 0; i < 200; ++i) stats.push(0.0f, 0.02);  // silence
    CHECK(!stats.summary().signal);
    CHECK(adviseInput(stats.summary()).messages.front().find("No signal") == 0);

    stats.reset();  // room noise only: no gain advice
    for (int i = 0; i < 200; ++i) stats.push(0.002f, 0.02);
    CHECK(adviseInput(stats.summary()).gainChangeDb == 0.0);
    CHECK(adviseInput(stats.summary()).messages.front().find("Only background") == 0);

    stats.reset();
    const float quiet = static_cast<float>(std::pow(10.0, -34.0 / 20.0)), loud = static_cast<float>(std::pow(10.0, -30.0 / 20.0));
    for (int i = 0; i < 500; ++i) stats.push(i % 5 == 0 ? loud : quiet, 0.02);
    InputSummary s = stats.summary();
    CHECK(std::abs(s.seconds - 10.0) < 0.01);
    CHECK(std::abs(s.peakDb + 30.0) < 0.2);
    CHECK(std::abs(s.noiseFloorDb + 34.0) < 0.2);
    InputAdvice a = adviseInput(s);
    CHECK(a.gainChangeDb == 20.0);  // peaks -30 -> -10
    CHECK(a.messages.front().find("low") != std::string::npos);

    stats.reset();
    for (int i = 0; i < 100; ++i) stats.push(i == 50 ? 1.0f : 0.2f, 0.02);
    s = stats.summary();
    CHECK(s.clippedReadings == 1);
    a = adviseInput(s);
    CHECK(a.gainChangeDb <= -6.0);
    CHECK(a.messages.front().find("clipped") != std::string::npos);

    stats.reset();
    for (int i = 0; i < 100; ++i) stats.push(i % 10 == 0 ? 0.3f : 0.1f, 0.02);  // peak -10.5 dBFS
    a = adviseInput(stats.summary());
    CHECK(a.gainChangeDb == 0.0);
    CHECK(a.messages.front().find("good") != std::string::npos);

    // the window is bounded
    InputLevelStats small(10);
    for (int i = 0; i < 100; ++i) small.push(0.5f, 0.02);
    CHECK(small.summary().seconds < 0.25);
}

#include <filesystem>
#include <fstream>
#include "lpc/audio_image.h"

TEST_CASE("images: a PNG with the right signature and size; the spectrogram puts a tone at its frequency", "[analysis][image]") {
    WavData d = tone(1000.0, -10.0, 4.0);
    const Image s = spectrogramImage(d, 200, 120);
    REQUIRE(s.rgb.size() == 200u * 120u * 3u);
    // brightest row of the middle column: 1 kHz on a log axis 20 Hz..20 kHz is at 57 % of the height from the bottom
    int best = 0, bestY = 0;
    for (int y = 0; y < 120; ++y) {
        const int v = s.rgb[(static_cast<std::size_t>(y) * 200 + 100) * 3] + s.rgb[(static_cast<std::size_t>(y) * 200 + 100) * 3 + 1];
        if (v > best) { best = v; bestY = y; }
    }
    CHECK(std::abs((119 - bestY) / 120.0 - 0.567) < 0.05);
    const Image w = waveformImage(d, 100, 80);
    CHECK(w.rgb.size() == 100u * 80u * 3u);
    CHECK(spectrumImage(d, 310, 100).rgb.size() == 310u * 100u * 3u);
    const std::vector<std::uint8_t> png = encodePng(w);
    CHECK(png.size() > 100);
    CHECK(png[1] == 'P');
    CHECK(png[2] == 'N');
    CHECK(png[3] == 'G');
    CHECK_THROWS(encodePng(Image{}));
    // clipping shows red in the waveform
    WavData loud = tone(1000.0, 0.0, 1.0);
    for (float& v : loud.samples) v = std::clamp(v * 2.0f, -1.0f, 1.0f);
    const Image lw = waveformImage(loud, 50, 60);
    bool red = false;
    for (std::size_t i = 0; i + 2 < lw.rgb.size(); i += 3) red = red || (lw.rgb[i] == 255 && lw.rgb[i + 1] == 60);
    CHECK(red);
}
