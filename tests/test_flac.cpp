#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>

#include "lpc/audio_decode.h"
#include "lpc/flac.h"
#include "temp_dir.h"

using namespace lpc;

namespace {
// What a 16 or 24 bit file holds of x: the nearest step of the format.
float quantised(float x, int bits) {
    const double scale = std::pow(2.0, bits - 1) - 1.0;
    return static_cast<float>(std::lround(std::clamp(static_cast<double>(x), -1.0, 1.0) * scale) / std::pow(2.0, bits - 1));  // the decoder divides by 2^(bits-1)
}
}  // namespace

TEST_CASE("flac: a written file decodes to exactly the quantised samples", "[flac]") {
    test::TempDir dir;
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
    std::vector<float> in;
    for (int i = 0; i < 10000; ++i) {  // a tone with a little noise: stereo, not a multiple of the block size
        const float tone = 0.4f * std::sin(static_cast<float>(i) * 0.05f);
        in.push_back(tone + 0.01f * noise(rng));
        in.push_back(0.3f * std::sin(static_cast<float>(i) * 0.031f));
    }
    for (const int bits : {16, 24}) {
        const auto path = dir.path / ("t" + std::to_string(bits) + ".flac");
        writeFlac(path, 44100, 2, in, bits);
        const WavData d = decodeAudioFile(path);
        REQUIRE(d.sampleRate == 44100);
        REQUIRE(d.channels == 2);
        REQUIRE(d.frames() == 10000);
        for (std::size_t i = 0; i < in.size(); ++i) REQUIRE(d.samples[i] == Catch::Approx(quantised(in[i], bits)).margin(1e-7));
        REQUIRE(std::filesystem::file_size(path) < in.size() * static_cast<std::size_t>(bits / 8));  // it is smaller than the raw PCM
    }
}

TEST_CASE("flac: silence, full scale, mono, a single block and an empty file", "[flac]") {
    test::TempDir dir;
    std::vector<float> silence(9000, 0.0f);
    writeFlac(dir.path / "s.flac", 48000, 1, silence, 16);
    const WavData s = decodeAudioFile(dir.path / "s.flac");
    REQUIRE(s.channels == 1);
    REQUIRE(s.frames() == 9000);
    for (const float v : s.samples) REQUIRE(v == 0.0f);
    REQUIRE(std::filesystem::file_size(dir.path / "s.flac") < 200);          // constant subframes are tiny

    std::vector<float> square;
    for (int i = 0; i < 600; ++i) square.push_back((i / 50) % 2 ? 1.0f : -1.0f);   // full scale both ways: large residuals
    writeFlac(dir.path / "q.flac", 48000, 1, square, 24);
    const WavData q = decodeAudioFile(dir.path / "q.flac");
    REQUIRE(q.frames() == 600);
    for (std::size_t i = 0; i < square.size(); ++i) REQUIRE(q.samples[i] == Catch::Approx(square[i]).margin(1e-6));

    writeFlac(dir.path / "e.flac", 48000, 2, {}, 16);
    REQUIRE(std::filesystem::exists(dir.path / "e.flac"));
    REQUIRE_THROWS(writeFlac(dir.path / "x.flac", 48000, 2, silence, 12));
    REQUIRE_THROWS(writeFlac(dir.path / "y.flac", 48000, 0, silence, 16));
    REQUIRE_THROWS(writeFlac(dir.path / "z.flac", 48000, 2, std::vector<float>(3, 0.0f), 16));   // odd sample count for stereo
}
