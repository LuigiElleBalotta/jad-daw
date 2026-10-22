#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "lpc/aiff.h"
#include "lpc/audio_decode.h"
#include "lpc/audio_ops.h"
#include "temp_dir.h"

using namespace lpc;

TEST_CASE("aiff: a written file reads back with its rate, channels and samples", "[aiff]") {
    test::TempDir dir;
    const std::vector<float> in = {0.5f, -0.5f, 0.25f, -0.25f, 1.0f, -1.0f};
    for (const int bits : {16, 24}) {
        const auto path = dir.path / ("a" + std::to_string(bits) + ".aif");
        writeAiff(path, 44100, 2, in, bits);
        const WavData d = decodeAudioFile(path);
        REQUIRE(d.sampleRate == 44100);
        REQUIRE(d.channels == 2);
        REQUIRE(d.frames() == 3);
        for (std::size_t i = 0; i < in.size(); ++i) REQUIRE(d.samples[i] == Catch::Approx(in[i]).margin(bits == 16 ? 1e-4 : 1e-6));
    }
    REQUIRE_THROWS(writeAiff(dir.path / "b.aif", 48000, 2, in, 12));
    REQUIRE_THROWS(writeAiff(dir.path / "c.aif", 48000, 3, in, 16));
}

TEST_CASE("caf: a written file has the Core Audio header, a PCM description and the samples", "[caf]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "lpc_test.caf";
    const std::vector<float> in = {0.0f, 0.5f, -0.5f, 1.0f};
    for (const int bits : {16, 24, 32}) {
        writeCaf(path, 44100, 2, in, bits);
        std::ifstream f(path, std::ios::binary);
        std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const int bytes = bits / 8;
        REQUIRE(std::string(b.begin(), b.begin() + 4) == "caff");
        REQUIRE(b[5] == 1);
        REQUIRE(std::string(b.begin() + 8, b.begin() + 12) == "desc");
        auto be32 = [&](std::size_t at) { return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3]; };
        REQUIRE(std::string(b.begin() + 28, b.begin() + 32) == "lpcm");
        REQUIRE(be32(32) == (bits == 32 ? 3u : 2u));
        REQUIRE(be32(36) == static_cast<std::uint32_t>(bytes * 2));
        REQUIRE(be32(44) == 2u);
        REQUIRE(be32(48) == static_cast<std::uint32_t>(bits));
        REQUIRE(std::string(b.begin() + 52, b.begin() + 56) == "data");
        REQUIRE(b.size() == 52 + 4 + 8 + 4 + in.size() * static_cast<std::size_t>(bytes));
        // the second sample (0.5), little-endian
        const std::size_t at = 68 + static_cast<std::size_t>(bytes);
        if (bits == 16) REQUIRE(static_cast<int>(static_cast<std::int16_t>(b[at] | (b[at + 1] << 8))) == 16384);
        if (bits == 32) { float v; std::uint32_t raw = b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (std::uint32_t(b[at + 3]) << 24); std::memcpy(&v, &raw, 4); REQUIRE(v == 0.5f); }
    }
    std::filesystem::remove(path);
}

TEST_CASE("dither: TPDF noise of one least significant bit, repeatable, silence stays near silence", "[aiff][dither]") {
    WavData d;
    d.sampleRate = 48000;
    d.channels = 1;
    d.samples.assign(20000, 0.0f);
    WavData copy = d;
    ditherTpdf(d, 16);
    ditherTpdf(copy, 16);
    REQUIRE(d.samples == copy.samples);                        // the same noise every time
    float peak = 0;
    double mean = 0;
    for (const float v : d.samples) { peak = std::max(peak, std::abs(v)); mean += v; }
    REQUIRE(peak <= 2.0f / 32767.0f + 1e-9f);                  // at most two least significant bits
    REQUIRE(peak > 0.5f / 32767.0f);                           // and it is not zero
    REQUIRE(std::abs(mean / static_cast<double>(d.samples.size())) < 1e-4);
}
