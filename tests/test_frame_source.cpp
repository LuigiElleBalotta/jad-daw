#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include "lpc/audio/frame_source.h"
#include "lpc/wav.h"
#include "temp_dir.h"

using namespace lpc;
using namespace lpc::audio;

namespace {

// L = (i % 1000) / 1000, R = -L
std::vector<float> stereoRamp(int frames) {
    std::vector<float> s(static_cast<std::size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        s[static_cast<std::size_t>(i) * 2] = float(i % 1000) / 1000.0f;
        s[static_cast<std::size_t>(i) * 2 + 1] = -float(i % 1000) / 1000.0f;
    }
    return s;
}

}  // namespace

TEST_CASE("memory source: reads, zero outside the file, mono is duplicated", "[source]") {
    MemorySource stereo(48000, 2, stereoRamp(100));
    REQUIRE(stereo.frames() == 100);
    std::vector<float> l(10, 7.0f), r(10, 7.0f);
    REQUIRE(stereo.read(5, l.data(), r.data(), 10));
    REQUIRE(l[0] == 5.0f / 1000.0f);
    REQUIRE(r[0] == -5.0f / 1000.0f);

    REQUIRE(stereo.read(-3, l.data(), r.data(), 10));  // before the start
    for (int i = 0; i < 3; ++i) REQUIRE((l[i] == 0.0f && r[i] == 0.0f));
    REQUIRE(l[3] == 0.0f);
    REQUIRE(l[4] == 1.0f / 1000.0f);

    REQUIRE(stereo.read(95, l.data(), r.data(), 10));  // past the end
    REQUIRE(l[4] == 99.0f / 1000.0f);
    for (int i = 5; i < 10; ++i) REQUIRE(l[i] == 0.0f);

    MemorySource mono(48000, 1, {0.1f, 0.2f, 0.3f});
    mono.read(0, l.data(), r.data(), 3);
    REQUIRE(l[1] == 0.2f);
    REQUIRE(r[1] == 0.2f);
}

TEST_CASE("streaming source: paused reader reports an underrun until the chunk is loaded", "[source][streaming]") {
    test::TempDir tmp;
    constexpr int kFrames = 400000;  // larger than the 15-chunk read-ahead window
    const auto samples = stereoRamp(kFrames);
    writeWav(tmp.path / "long.wav", 48000, 2, samples);

    StreamingSource src(tmp.path / "long.wav", /*startReaderThread=*/false);
    REQUIRE(src.frames() == kFrames);
    std::vector<float> l(256), r(256);

    REQUIRE(src.read(1000, l.data(), r.data(), 256));  // inside the initial window: ready
    REQUIRE(l[0] == samples[2000]);
    REQUIRE(src.takeUnderruns() == 0);

    REQUIRE_FALSE(src.read(350000, l.data(), r.data(), 256));  // far away, not loaded yet
    for (float v : l) REQUIRE(v == 0.0f);                       // silence, never garbage
    REQUIRE(src.takeUnderruns() == 1);
    REQUIRE(src.takeUnderruns() == 0);  // counter resets

    src.pumpOnce();  // the reader notices the new position and loads around it
    REQUIRE(src.read(350000, l.data(), r.data(), 256));
    REQUIRE(l[10] == samples[(350000 + 10) * 2]);
    REQUIRE(r[10] == samples[(350000 + 10) * 2 + 1]);
}

TEST_CASE("streaming source: a read spanning two chunks is continuous", "[source][streaming]") {
    test::TempDir tmp;
    const auto samples = stereoRamp(100000);
    writeWav(tmp.path / "c.wav", 48000, 2, samples);
    StreamingSource src(tmp.path / "c.wav", false);
    std::vector<float> l(64), r(64);
    const int start = StreamingSource::kChunkFrames - 32;  // straddles the chunk 0/1 boundary
    REQUIRE(src.read(start, l.data(), r.data(), 64));
    for (int i = 0; i < 64; ++i) REQUIRE(l[i] == samples[static_cast<std::size_t>(start + i) * 2]);
}

TEST_CASE("streaming source: end of file is zero-filled without an underrun", "[source][streaming]") {
    test::TempDir tmp;
    writeWav(tmp.path / "s.wav", 48000, 2, stereoRamp(1000));
    StreamingSource src(tmp.path / "s.wav", false);
    std::vector<float> l(64, 3.0f), r(64, 3.0f);
    REQUIRE(src.read(980, l.data(), r.data(), 64));
    REQUIRE(l[19] == 999.0f / 1000.0f);
    for (int i = 20; i < 64; ++i) REQUIRE(l[i] == 0.0f);
    REQUIRE(src.takeUnderruns() == 0);
}

TEST_CASE("streaming source: empty file and mono file", "[source][streaming]") {
    test::TempDir tmp;
    writeWav(tmp.path / "e.wav", 48000, 2, {});
    StreamingSource empty(tmp.path / "e.wav", false);
    std::vector<float> l(16, 2.0f), r(16, 2.0f);
    REQUIRE(empty.read(0, l.data(), r.data(), 16));
    for (float v : l) REQUIRE(v == 0.0f);

    writeWav(tmp.path / "m.wav", 48000, 1, {0.5f, 0.25f});
    StreamingSource mono(tmp.path / "m.wav", false);
    REQUIRE(mono.read(0, l.data(), r.data(), 2));
    REQUIRE(r[1] == 0.25f);
}

TEST_CASE("streaming source: the background reader keeps up with a moving playhead", "[source][streaming][threads]") {
    test::TempDir tmp;
    constexpr int kFrames = 600000;
    const auto samples = stereoRamp(kFrames);
    writeWav(tmp.path / "t.wav", 48000, 2, samples);
    StreamingSource src(tmp.path / "t.wav", /*startReaderThread=*/true);
    std::vector<float> l(512), r(512);
    int mismatches = 0;
    for (int pos = 0; pos + 512 <= kFrames; pos += 512 * 40) {
        // after a jump, allow the reader up to two seconds to catch up
        bool ok = false;
        for (int attempt = 0; attempt < 400 && !ok; ++attempt) {
            ok = src.read(pos, l.data(), r.data(), 512);
            if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(ok);
        for (int i = 0; i < 512; ++i)
            if (l[i] != samples[static_cast<std::size_t>(pos + i) * 2]) ++mismatches;
    }
    REQUIRE(mismatches == 0);
}
