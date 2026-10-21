#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>

#include "lpc/audio_decode.h"
#include "lpc/compressed_audio.h"
#include "temp_dir.h"

using namespace lpc;

TEST_CASE("compressed audio: the target rate is 44.1 or 48 kHz", "[compressed]") {
    REQUIRE(compressedTargetRate(44100) == 44100);
    REQUIRE(compressedTargetRate(48000) == 48000);
    REQUIRE(compressedTargetRate(88200) == 44100);
    REQUIRE(compressedTargetRate(96000) == 48000);
    REQUIRE(compressedTargetRate(192000) == 48000);
    REQUIRE(compressedTargetRate(176400) == 44100);
}

TEST_CASE("compressed audio: an MP3 of a tone decodes to about the same length and level", "[compressed]") {
    if (!compressedEncoderAvailable(CompressedFormat::Mp3)) SKIP("no MP3 encoder on this system");
    test::TempDir dir;
    const int rate = 44100, frames = rate * 2;
    std::vector<float> in(static_cast<std::size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const float v = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / static_cast<float>(rate));
        in[static_cast<std::size_t>(i) * 2] = in[static_cast<std::size_t>(i) * 2 + 1] = v;
    }
    const auto file = dir.path / "tone.mp3";
    writeCompressed(file, CompressedFormat::Mp3, rate, 2, in);
    REQUIRE(std::filesystem::file_size(file) > 20000);  // 2 s at 256 kbit/s is about 64 kB
    const WavData out = decodeAudioFile(file);
    REQUIRE(out.sampleRate == rate);
    REQUIRE(out.channels == 2);
    const double seconds = static_cast<double>(out.samples.size()) / 2.0 / out.sampleRate;
    REQUIRE(seconds > 1.9);
    REQUIRE(seconds < 2.3);
    float peak = 0;
    for (float s : out.samples) peak = std::max(peak, std::abs(s));
    REQUIRE(peak > 0.4f);
    REQUIRE(peak < 0.65f);
}

TEST_CASE("compressed audio: an M4A with AAC is written", "[compressed]") {
    if (!compressedEncoderAvailable(CompressedFormat::M4aAac)) SKIP("no AAC encoder on this system");
    test::TempDir dir;
    std::vector<float> in(48000 * 2, 0.1f);
    const auto file = dir.path / "x.m4a";
    writeCompressed(file, CompressedFormat::M4aAac, 48000, 2, in);
    REQUIRE(std::filesystem::file_size(file) > 1000);
    REQUIRE_THROWS(writeCompressed(dir.path / "y.m4a", CompressedFormat::M4aAac, 96000, 2, in));  // the encoder takes 44.1 and 48 kHz
}
