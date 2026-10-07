#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <fstream>
#include "lpc/wav.h"
#include "temp_dir.h"

using namespace lpc;
namespace fs = std::filesystem;

namespace {

void put16(std::vector<unsigned char>& b, unsigned v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff); }
void put32(std::vector<unsigned char>& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xff); }
void putTag(std::vector<unsigned char>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// Builds a RIFF/WAVE file in memory. `fmtBody` is the content of the "fmt " chunk (>= 16 bytes).
std::vector<unsigned char> riff(const std::vector<unsigned char>& fmtBody, const std::vector<unsigned char>& data,
                                std::uint32_t declaredDataSize, const std::vector<unsigned char>& extraBeforeData = {}) {
    std::vector<unsigned char> b;
    putTag(b, "RIFF");
    put32(b, 0);  // size is not used by the reader
    putTag(b, "WAVE");
    putTag(b, "fmt ");
    put32(b, static_cast<std::uint32_t>(fmtBody.size()));
    b.insert(b.end(), fmtBody.begin(), fmtBody.end());
    b.insert(b.end(), extraBeforeData.begin(), extraBeforeData.end());
    putTag(b, "data");
    put32(b, declaredDataSize);
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

std::vector<unsigned char> fmt(unsigned tag, unsigned channels, unsigned rate, unsigned bits) {
    std::vector<unsigned char> f;
    put16(f, tag);
    put16(f, channels);
    put32(f, rate);
    put32(f, rate * channels * bits / 8);
    put16(f, channels * bits / 8);
    put16(f, bits);
    return f;
}

fs::path writeBytes(const fs::path& dir, const char* name, const std::vector<unsigned char>& bytes) {
    const fs::path p = dir / name;
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return p;
}

std::vector<float> ramp(int frames, int channels) {
    std::vector<float> s(static_cast<std::size_t>(frames) * channels);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c) s[static_cast<std::size_t>(i) * channels + c] = (c == 0 ? 1.0f : -1.0f) * (float(i % 200) / 250.0f);
    return s;
}

}  // namespace

TEST_CASE("wav: write then read round trip for every output format", "[wav]") {
    test::TempDir tmp;
    const auto samples = ramp(1000, 2);
    struct Case { WavFormat f; double tol; };
    for (const Case c : {Case{WavFormat::Float32, 0.0}, Case{WavFormat::Pcm24, 5e-7}, Case{WavFormat::Pcm16, 6e-5}}) {
        const fs::path p = tmp.path / "r.wav";
        writeWav(p, 44100, 2, samples, c.f);
        const WavData d = readWav(p);
        REQUIRE(d.sampleRate == 44100);
        REQUIRE(d.channels == 2);
        REQUIRE(d.frames() == 1000);
        for (std::size_t i = 0; i < samples.size(); ++i) REQUIRE(std::abs(d.samples[i] - samples[i]) <= c.tol);
    }
}

TEST_CASE("wav: mono files and random access reads", "[wav]") {
    test::TempDir tmp;
    const auto samples = ramp(500, 1);
    writeWav(tmp.path / "m.wav", 48000, 1, samples);
    WavFile f(tmp.path / "m.wav");
    REQUIRE(f.channels() == 1);
    REQUIRE(f.frames() == 500);
    std::vector<float> out(20);
    f.readFrames(100, 20, out.data());
    for (int i = 0; i < 20; ++i) REQUIRE(out[i] == samples[100 + i]);
    // before the start and past the end: zeros, no out-of-bounds access
    std::vector<float> edge(30, 9.0f);
    f.readFrames(-10, 30, edge.data());
    for (int i = 0; i < 10; ++i) REQUIRE(edge[i] == 0.0f);
    REQUIRE(edge[10] == samples[0]);
    std::vector<float> tail(30, 9.0f);
    f.readFrames(490, 30, tail.data());
    REQUIRE(tail[9] == samples[499]);
    for (int i = 10; i < 30; ++i) REQUIRE(tail[i] == 0.0f);
    std::vector<float> far(5, 9.0f);
    f.readFrames(100000, 5, far.data());
    for (float v : far) REQUIRE(v == 0.0f);
}

TEST_CASE("wav: 8-bit PCM is unsigned and centred on 128", "[wav][formats]") {
    test::TempDir tmp;
    const auto p = writeBytes(tmp.path, "u8.wav", riff(fmt(1, 1, 8000, 8), {128, 255, 0, 192}, 4));
    const WavData d = readWav(p);
    REQUIRE(d.frames() == 4);
    REQUIRE(d.samples[0] == Catch::Approx(0.0f));
    REQUIRE(d.samples[1] == Catch::Approx(127.0f / 128.0f));
    REQUIRE(d.samples[2] == Catch::Approx(-1.0f));
    REQUIRE(d.samples[3] == Catch::Approx(0.5f));
}

TEST_CASE("wav: 32-bit integer PCM", "[wav][formats]") {
    test::TempDir tmp;
    std::vector<unsigned char> data;
    put32(data, 0x40000000u);  // +0.5
    put32(data, 0xC0000000u);  // -0.5
    const WavData d = readWav(writeBytes(tmp.path, "i32.wav", riff(fmt(1, 1, 48000, 32), data, 8)));
    REQUIRE(d.samples[0] == Catch::Approx(0.5f));
    REQUIRE(d.samples[1] == Catch::Approx(-0.5f));
}

TEST_CASE("wav: WAVE_FORMAT_EXTENSIBLE float32", "[wav][formats]") {
    test::TempDir tmp;
    std::vector<unsigned char> f = fmt(0xFFFE, 2, 48000, 32);
    put16(f, 22);  // cbSize
    put16(f, 32);  // valid bits
    put32(f, 3);   // channel mask
    put16(f, 3);   // sub-format tag: IEEE float (first two bytes of the GUID)
    f.resize(40, 0);
    std::vector<unsigned char> data;
    const float v[4] = {0.25f, -0.25f, 0.5f, -0.5f};
    for (float x : v) { std::uint32_t u; std::memcpy(&u, &x, 4); put32(data, u); }
    const WavData d = readWav(writeBytes(tmp.path, "ext.wav", riff(f, data, 16)));
    REQUIRE(d.channels == 2);
    REQUIRE(d.frames() == 2);
    REQUIRE(d.samples[2] == 0.5f);
}

TEST_CASE("wav: odd-sized chunks before the data chunk are skipped with padding", "[wav][formats]") {
    test::TempDir tmp;
    std::vector<unsigned char> list;  // "LIST" chunk of 3 bytes + 1 pad byte
    putTag(list, "LIST");
    put32(list, 3);
    list.insert(list.end(), {'a', 'b', 'c', 0});
    std::vector<unsigned char> data;
    put16(data, 16384);  // 0.5 in 16-bit
    const WavData d = readWav(writeBytes(tmp.path, "odd.wav", riff(fmt(1, 1, 8000, 16), data, 2, list)));
    REQUIRE(d.frames() == 1);
    REQUIRE(d.samples[0] == Catch::Approx(0.5f));
}

TEST_CASE("wav: truncated data chunk uses what is really in the file", "[wav][damaged]") {
    test::TempDir tmp;
    std::vector<unsigned char> data(100, 0);  // header claims 1000 bytes, only 100 are present
    const WavData d = readWav(writeBytes(tmp.path, "trunc.wav", riff(fmt(1, 1, 8000, 16), data, 1000)));
    REQUIRE(d.frames() == 50);
}

TEST_CASE("wav: streaming-style data size 0xFFFFFFFF is clamped to the file size", "[wav][damaged]") {
    test::TempDir tmp;
    std::vector<unsigned char> data(40, 0);
    const WavData d = readWav(writeBytes(tmp.path, "stream.wav", riff(fmt(1, 2, 8000, 16), data, 0xFFFFFFFFu)));
    REQUIRE(d.frames() == 10);
}

TEST_CASE("wav: zero-length audio is valid", "[wav][damaged]") {
    test::TempDir tmp;
    const WavData d = readWav(writeBytes(tmp.path, "empty.wav", riff(fmt(1, 2, 48000, 16), {}, 0)));
    REQUIRE(d.frames() == 0);
    WavFile f(tmp.path / "empty.wav");
    std::vector<float> out(8, 5.0f);
    f.readFrames(0, 4, out.data());
    for (float v : out) REQUIRE(v == 0.0f);
}

TEST_CASE("wav: unsupported and damaged files are rejected with an error", "[wav][damaged]") {
    test::TempDir tmp;
    REQUIRE_THROWS_AS(WavFile(tmp.path / "missing.wav"), std::runtime_error);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "garbage.wav", {'n', 'o', 't', ' ', 'a', ' ', 'w', 'a', 'v'})), std::runtime_error);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "empty.bin", {})), std::runtime_error);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "6ch.wav", riff(fmt(1, 6, 48000, 16), std::vector<unsigned char>(24), 24))), std::runtime_error);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "12bit.wav", riff(fmt(1, 1, 48000, 12), std::vector<unsigned char>(8), 8))), std::runtime_error);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "adpcm.wav", riff(fmt(2, 1, 48000, 4), std::vector<unsigned char>(8), 8))), std::runtime_error);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "zero_rate.wav", riff(fmt(1, 1, 0, 16), std::vector<unsigned char>(8), 8))), std::runtime_error);
    // header only, no data chunk at all
    std::vector<unsigned char> noData;
    putTag(noData, "RIFF"); put32(noData, 0); putTag(noData, "WAVE"); putTag(noData, "fmt "); put32(noData, 16);
    for (unsigned char c : fmt(1, 1, 48000, 16)) noData.push_back(c);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "nodata.wav", noData)), std::runtime_error);
}
