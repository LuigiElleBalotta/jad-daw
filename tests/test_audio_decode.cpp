#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fstream>
#include "lpc/audio_decode.h"
#include "temp_dir.h"

using namespace lpc;

namespace {

void put32(std::ofstream& f, std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) f.put(static_cast<char>((v >> s) & 0xff)); }
void put16(std::ofstream& f, std::uint16_t v) { f.put(static_cast<char>(v >> 8)); f.put(static_cast<char>(v & 0xff)); }

// a minimal AIFF: 16-bit big-endian, 44.1 kHz (80-bit extended 0x400E AC44 0000...)
void writeAiff(const std::filesystem::path& path, int channels, const std::vector<std::int16_t>& samples) {
    std::ofstream f(path, std::ios::binary);
    const std::uint32_t frames = static_cast<std::uint32_t>(samples.size()) / static_cast<std::uint32_t>(channels);
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size()) * 2;
    f.write("FORM", 4);
    put32(f, 4 + 8 + 18 + 8 + 8 + dataBytes);
    f.write("AIFF", 4);
    f.write("COMM", 4);
    put32(f, 18);
    put16(f, static_cast<std::uint16_t>(channels));
    put32(f, frames);
    put16(f, 16);
    const unsigned char rate[10] = {0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0};
    f.write(reinterpret_cast<const char*>(rate), 10);
    f.write("SSND", 4);
    put32(f, 8 + dataBytes);
    put32(f, 0);
    put32(f, 0);
    for (const std::int16_t s : samples) put16(f, static_cast<std::uint16_t>(s));
}

}  // namespace

TEST_CASE("decode: an AIFF file is read with its rate, channels and samples", "[decode]") {
    test::TempDir dir;
    const auto path = dir.path / "a.aif";
    writeAiff(path, 2, {16384, -16384, 8192, -8192});
    const WavData d = decodeAudioFile(path);
    REQUIRE(d.sampleRate == 44100);
    REQUIRE(d.channels == 2);
    REQUIRE(d.frames() == 2);
    REQUIRE(d.samples[0] == Catch::Approx(0.5f));
    REQUIRE(d.samples[1] == Catch::Approx(-0.5f));
    REQUIRE(d.samples[2] == Catch::Approx(0.25f));
}

TEST_CASE("decode: unknown and damaged files are refused with a message", "[decode]") {
    test::TempDir dir;
    REQUIRE_THROWS_AS(decodeAudioFile(dir.path / "x.m4a"), std::runtime_error);
    const auto bad = dir.path / "bad.mp3";
    { std::ofstream f(bad, std::ios::binary); f << "not an mp3 at all"; }
    REQUIRE_THROWS_AS(decodeAudioFile(bad), std::runtime_error);
    REQUIRE(isImportableAudioExtension(".MP3"));
    REQUIRE(isImportableAudioExtension(".flac"));
    REQUIRE_FALSE(isImportableAudioExtension(".txt"));
}

TEST_CASE("decode: converting keeps the length in seconds, the first two channels and a constant level", "[decode]") {
    WavData in;
    in.sampleRate = 44100;
    in.channels = 3;
    in.samples.assign(3 * 4410, 0.25f);
    const WavData out = convertForProject(in, 48000);
    REQUIRE(out.channels == 2);
    REQUIRE(out.sampleRate == 48000);
    REQUIRE(std::abs(out.frames() - 4800) <= 1);
    for (std::size_t i = 10; i < out.samples.size() - 10; ++i) REQUIRE(out.samples[i] == Catch::Approx(0.25f).margin(1e-4f));
    REQUIRE(convertForProject(in, 44100).frames() == 4410);   // same rate: only the channels change
}
