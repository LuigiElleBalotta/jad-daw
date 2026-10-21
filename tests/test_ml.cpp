#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <random>

#include "lpc/audio_decode.h"
#include "lpc/ml/beat_tracker.h"
#include "lpc/ml/onnx.h"
#include "lpc/flex_pitch.h"
#include "lpc/audio/effects.h"
#include "lpc/ml/neural_stream.h"
#include "lpc/ml/onset_cnn.h"
#include "lpc/processor_ids.h"
#include "lpc/ml/pitch_cnn.h"
#include "lpc/tempo_analysis.h"

using namespace lpc;
using Catch::Approx;

namespace {

// 4 ms noise bursts every `period` seconds on silence, 44.1 kHz stereo
WavData clicks(double seconds, double period) {
    WavData d;
    d.sampleRate = 44100;
    d.channels = 2;
    const std::size_t n = static_cast<std::size_t>(seconds * d.sampleRate);
    d.samples.assign(n * 2, 0.0f);
    std::mt19937 rng(3);
    std::normal_distribution<double> noise(0.0, 1.0);
    for (double t = 0.3; t < seconds - 0.1; t += period) {
        const std::size_t start = static_cast<std::size_t>(t * d.sampleRate);
        for (int i = 0; i < 176 && start + static_cast<std::size_t>(i) < n; ++i) {
            const float v = static_cast<float>(0.7 * noise(rng) * std::exp(-i / 40.0));
            d.samples[(start + static_cast<std::size_t>(i)) * 2] = v;
            d.samples[(start + static_cast<std::size_t>(i)) * 2 + 1] = v;
        }
    }
    return d;
}

bool haveModel() {
    ml::setModelDirectory(LPC_ONNX_MODELS_DIR);
    return ml::available() && std::filesystem::exists(ml::modelPath("OnsetDetectionCNN.espresso.onnx"));
}

}  // namespace

TEST_CASE("ml: the onset features are three 80-band log-mel spectrograms, one frame per 10 ms", "[ml]") {
    int frames = 0;
    const WavData d = clicks(5.0, 0.5);
    const std::vector<float> f = ml::onsetFeatures(d, frames);
    REQUIRE(frames == 5 * 100 + 1);
    REQUIRE(f.size() == static_cast<std::size_t>(3 * 80 * frames));
    for (const float v : f) REQUIRE(std::isfinite(v));
    // a click at 0.3 s lights up frame 30 far more than the silence at 0.4 s (frame 40), on every window size
    for (int c = 0; c < 3; ++c) {
        double on = 0, off = 0;
        for (int b = 0; b < 80; ++b) {
            on += f[(static_cast<std::size_t>(c) * 80 + static_cast<std::size_t>(b)) * static_cast<std::size_t>(frames) + 31];
            off += f[(static_cast<std::size_t>(c) * 80 + static_cast<std::size_t>(b)) * static_cast<std::size_t>(frames) + 45];
        }
        CHECK(on > off + 5.0);
    }
    WavData tiny;
    tiny.sampleRate = 44100;
    tiny.channels = 1;
    tiny.samples.assign(1000, 0.0f);
    CHECK(ml::onsetFeatures(tiny, frames).empty());
}

TEST_CASE("ml: OnsetDetectionCNN (converted to ONNX) finds the clicks", "[ml]") {
    if (!haveModel()) SKIP("onnxruntime or the converted OnsetDetectionCNN is not available");
    const ml::OnsetActivation a = ml::detectOnsetsCnn(clicks(12.0, 0.5));
    REQUIRE(a.values.size() == 12 * 100 + 1);
    double atClicks = 0, between = 0;
    int nClicks = 0, nBetween = 0;
    for (double t = 0.3; t < 11.5; t += 0.5) {
        double peak = 0;
        for (int k = -3; k <= 3; ++k) peak = std::max(peak, a.values[static_cast<std::size_t>(std::lround(t * 100) + k)]);
        atClicks += peak;
        ++nClicks;
        between += a.values[static_cast<std::size_t>(std::lround((t + 0.25) * 100))];
        ++nBetween;
    }
    CHECK(atClicks / nClicks > 0.5);                       // the network fires on the clicks
    CHECK(between / nBetween < 0.2);                       // and stays quiet between them
}

TEST_CASE("ml: Smart Tempo with the neural onsets finds the pulse of a click train", "[ml][tempo]") {
    if (!haveModel()) SKIP("onnxruntime or the converted OnsetDetectionCNN is not available");
    const TempoAnalysis t = analyseTempo(clicks(20.0, 0.5), 60.0, 200.0, 0, true);
    REQUIRE(t.bpm > 0.0);
    CHECK(t.bpm == Approx(120.0).margin(2.0));
    CHECK(t.beats.size() >= 30);
}

TEST_CASE("ml: a real recording, when LPC_REAL_AUDIO is set", "[ml][real]") {
    const char* file = std::getenv("LPC_REAL_AUDIO");  // NOLINT(concurrency-mt-unsafe)
    if (!file || !haveModel()) SKIP("no LPC_REAL_AUDIO or no model");
    WavData d = decodeAudioFile(file);
    REQUIRE(d.frames() > 0);
    const std::int64_t keep = std::min<std::int64_t>(d.frames(), static_cast<std::int64_t>(d.sampleRate) * 40);
    d.samples.resize(static_cast<std::size_t>(keep * d.channels));
    const TempoAnalysis t = analyseTempo(d, 60.0, 200.0, 0, true);
    INFO("bpm " << t.bpm);
    CHECK(t.bpm > 60.0);
}

TEST_CASE("ml: the neural pitch tracker reads sine tones in tune and silence as no pitch", "[ml][pitch]") {
    if (!haveModel() || !std::filesystem::exists(ml::modelPath("RTPTM.model.onnx"))) SKIP("the converted pitch tracker is not available");
    for (const double hz : {110.0, 220.0, 440.0, 880.0}) {
        std::vector<float> x(16000);
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / 16000.0));
        const auto peaks = ml::neuralPitchPeaks(x, 16000, 160, 100);
        REQUIRE(peaks.size() == 100);
        REQUIRE(!peaks[50].empty());
        CHECK(peaks[50][0].midi == Approx(hzToMidi(hz)).margin(0.15));     // within 15 cents
        CHECK(peaks[50][0].prob > 0.8);
    }
    const auto quiet = ml::neuralPitchPeaks(std::vector<float>(16000, 0.0f), 16000, 160, 100);
    CHECK(quiet[50].empty());
}

TEST_CASE("ml: Flex Pitch with the neural tracker finds the notes of a two-note melody", "[ml][pitch]") {
    if (!haveModel() || !std::filesystem::exists(ml::modelPath("RTPTM.model.onnx"))) SKIP("the converted pitch tracker is not available");
    WavData d;
    d.sampleRate = 48000;
    d.channels = 1;
    for (int note = 0; note < 2; ++note) {
        const double hz = note == 0 ? 261.63 : 329.63;
        for (int i = 0; i < 48000; ++i) {
            const double t = static_cast<double>(i) / 48000.0;
            d.samples.push_back(static_cast<float>(0.4 * (std::sin(2 * 3.14159265358979323846 * hz * t) + 0.3 * std::sin(4 * 3.14159265358979323846 * hz * t)) * std::min(1.0, std::min(t * 50.0, (1.0 - t) * 50.0))));
        }
    }
    PitchOptions o;
    o.neural = true;
    const PitchAnalysis a = analysePitch(d, o);
    REQUIRE(a.notes.size() == 2);
    CHECK(a.notes[0].pitch == Approx(60.0).margin(0.3));
    CHECK(a.notes[1].pitch == Approx(64.0).margin(0.3));
}

TEST_CASE("ml: the beat tracker network reports the pulse of a click train and Smart Tempo uses it", "[ml][tempo]") {
    ml::setModelDirectory(LPC_ONNX_MODELS_DIR);
    if (!ml::available() || !std::filesystem::exists(ml::modelPath("DownbeatTrackerCondMPS.model.onnx"))) SKIP("the converted beat tracker is not available");
    const WavData d = clicks(30.0, 0.5);
    const ml::BeatActivation a = ml::detectBeatsCnn(d);
    REQUIRE(a.beats.size() == a.downbeats.size());
    REQUIRE(a.beats.size() > 1400);
    REQUIRE(a.meter.size() == 13);
    REQUIRE(a.tempo.size() == 251);
    // the strongest tempo bin is the click rate (120 bpm) or one of its octaves
    std::size_t best = 0;
    for (std::size_t i = 1; i < a.tempo.size(); ++i)
        if (a.tempo[i] > a.tempo[best]) best = i;
    INFO("tempo bin " << best);
    CHECK((std::abs(static_cast<int>(best) - 120) <= 3 || std::abs(static_cast<int>(best) - 60) <= 2 || std::abs(static_cast<int>(best) - 240) <= 4));
    const TempoAnalysis t = analyseTempo(d, 60.0, 200.0, 0, true);
    REQUIRE(t.bpm > 0.0);
    CHECK(t.bpm == Approx(120.0).margin(3.0));
    CHECK(t.beats.size() >= 40);
}

namespace {
// THD-like ratio: the energy outside the fundamental over the energy of the fundamental, from the last 8192 samples of a mono block
double harmonicRatio(const std::vector<float>& y, double hz, double sr) {
    const std::size_t n = 8192, from = y.size() - n;
    double fundC = 0, fundS = 0, total = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = 2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / sr, v = y[from + i];
        fundC += v * std::cos(ph);
        fundS += v * std::sin(ph);
        total += v * v;
    }
    const double fund = 2.0 * (fundC * fundC + fundS * fundS) / (static_cast<double>(n) * n);  // power of the fundamental (a * a / 2 form)
    const double all = total / static_cast<double>(n);
    return std::sqrt(std::max(0.0, all - fund) / std::max(fund, 1e-12));
}
}  // namespace

TEST_CASE("ml: Neural Saturation (Logic's model through onnxruntime) adds harmonics with the drive and reports its latency", "[ml][saturation]") {
    ml::setModelDirectory(LPC_ONNX_MODELS_DIR);
    if (!ml::available() || ml::findFamilyModel("RTRST", 44100.0, 256).empty()) SKIP("the converted saturation models are not available");
    auto render = [](double drive, double mix) {
        ProcessorRef ref{kProcMlSaturation, {{"type", 0}, {"drive", drive}, {"mix", mix}}, "", "", false};
        auto fx = audio::makeBuiltinEffect(ref, 44100.0);
        REQUIRE(fx != nullptr);
        CHECK(fx->latencySamples() == 512);
        std::vector<float> all;
        for (int b = 0; b < 40; ++b) {
            std::vector<float> l(512), r(512);
            for (int i = 0; i < 512; ++i) l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * (b * 512 + i) / 44100.0));
            fx->process(l.data(), r.data(), 512);
            all.insert(all.end(), l.begin(), l.end());
        }
        return all;
    };
    const double low = harmonicRatio(render(0.0, 100.0), 440.0, 44100.0), high = harmonicRatio(render(100.0, 100.0), 440.0, 44100.0);
    INFO("harmonic ratio at drive 0: " << low << ", at drive 100: " << high);
    CHECK(low < 0.05);
    CHECK(high > 0.15);
    CHECK(high > low * 4.0);
    // mix 0 is the dry sound, as late as the wet one
    const auto dry = render(100.0, 0.0);
    CHECK(std::abs(dry[512 + 100] - static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * 100 / 44100.0))) < 1e-4f);
}

TEST_CASE("ml: Neural Saturation without its models passes the sound through", "[ml][saturation]") {
    ml::setModelDirectory("this/folder/does/not/exist");
    ProcessorRef ref{kProcMlSaturation, {{"drive", 100}}, "", "", false};
    auto fx = audio::makeBuiltinEffect(ref, 48000.0);
    REQUIRE(fx != nullptr);
    CHECK(fx->latencySamples() == 0);
    std::vector<float> l(256, 0.3f), r(256, -0.2f);
    fx->process(l.data(), r.data(), 256);
    CHECK(l[100] == 0.3f);
    CHECK(r[100] == -0.2f);
    ml::setModelDirectory(LPC_ONNX_MODELS_DIR);
}

TEST_CASE("ml: the Mastering Analog Model (Valve, Punch, Transparent) runs Logic's networks, Clean does not", "[ml][mastering][analog]") {
    ml::setModelDirectory(LPC_ONNX_MODELS_DIR);
    if (!ml::available() || ml::findAnalogModel("TSO", 48000.0).empty()) SKIP("the converted Analog Model networks are not available");
    CHECK(ml::findAnalogModel("G", 48000.0).filename().string().rfind("G_48_", 0) == 0);        // not G_SO
    CHECK(ml::findAnalogModel("G_SO", 176400.0).filename().string().rfind("G_SO_96_", 0) == 0);  // a rate class that exists
    auto latency = [](int character) {
        ProcessorRef ref{kProcMastering, {{"character", character}}, "", "", false};
        auto fx = audio::makeBuiltinEffect(ref, 48000.0);
        REQUIRE(fx != nullptr);
        return fx->latencySamples();
    };
    const int clean = latency(0);
    for (int character = 1; character <= 3; ++character) CHECK(latency(character) == clean + 2048);   // two blocks of 1024 for the answer of the network
    // the sound goes through: after the latency a sine comes out, finite and not silent
    for (int character = 1; character <= 3; ++character) {
        ProcessorRef ref{kProcMastering, {{"character", character}}, "", "", false};
        auto fx = audio::makeBuiltinEffect(ref, 48000.0);
        const int total = fx->latencySamples();
        double energy = 0;
        bool finite = true;
        for (int b = 0; b < 40; ++b) {
            std::vector<float> l(512), r(512);
            for (int i = 0; i < 512; ++i) l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] = static_cast<float>(0.2 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * (b * 512 + i) / 48000.0));
            fx->process(l.data(), r.data(), 512);
            if (b * 512 > total + 4096)
                for (const float v : l) { finite = finite && std::isfinite(v); energy += static_cast<double>(v) * v; }
        }
        INFO("character " << character << " latency " << total);
        CHECK(finite);
        CHECK(energy > 1.0);
    }
}

TEST_CASE("ml: the Mastering Analog Model without its files keeps the approximation and the old latency", "[ml][mastering][analog]") {
    ml::setModelDirectory("this/folder/does/not/exist");
    ProcessorRef clean{kProcMastering, {{"character", 0}}, "", "", false}, valve{kProcMastering, {{"character", 1}}, "", "", false};
    CHECK(audio::makeBuiltinEffect(valve, 48000.0)->latencySamples() == audio::makeBuiltinEffect(clean, 48000.0)->latencySamples());
    ml::setModelDirectory(LPC_ONNX_MODELS_DIR);
}

// Not run by default (`lpc_tests "[.bench]"`): how much of the real time the neural effects take on this CPU (blocks of 512 frames of a noisy sine, 48 kHz stereo)
TEST_CASE("bench: the neural effects against real time", "[.bench][ml]") {
    ml::setModelDirectory(LPC_ONNX_MODELS_DIR);
    if (!ml::available()) SKIP("onnxruntime is not available");
    auto run = [](const ProcessorRef& ref) {
        auto fx = audio::makeBuiltinEffect(ref, 48000.0);
        REQUIRE(fx != nullptr);
        std::mt19937 rng(1);
        std::normal_distribution<float> noise(0.0f, 0.05f);
        const int blocks = 48000 * 8 / 512;
        std::vector<float> l(512), r(512);
        double worst = 0.0, total = 0.0;
        for (int b = 0; b < blocks; ++b) {
            for (int i = 0; i < 512; ++i) l[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] = 0.3f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 220.0 * (b * 512 + i) / 48000.0)) + noise(rng);
            const auto t0 = std::chrono::steady_clock::now();
            fx->process(l.data(), r.data(), 512);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            worst = std::max(worst, ms);
            total += ms;
        }
        const double budget = 512.0 / 48000.0 * 1000.0;
        std::cout << ref.processorId << " character " << (ref.params.count("character") ? ref.params.at("character") : -1) << ": mean " << total / blocks / budget * 100.0 << " %\n";
        };
    for (int character = 0; character <= 3; ++character) run(ProcessorRef{kProcMastering, {{"character", character}}, "", "", false});
    for (int type = 0; type <= 9; type += 3) run(ProcessorRef{kProcMlSaturation, {{"type", type}, {"drive", 70}}, "", "", false});
}
