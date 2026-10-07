#include <catch2/catch_test_macros.hpp>
#include "lpc/commands.h"
#include "lpc/demo_project.h"
#include "lpc/offline_render.h"
#include "temp_dir.h"

using namespace lpc;

namespace {
float peak(const std::vector<float>& v) {
    float p = 0.0f;
    for (float x : v) p = std::max(p, std::abs(x));
    return p;
}
}  // namespace

TEST_CASE("render: an empty project renders silence of the requested length", "[render]") {
    Project p;
    MediaStore media;
    RenderOptions o;
    o.frames = 1000;
    const RenderResult r = renderOffline(p, media, o);
    REQUIRE(r.frames == 1000);
    REQUIRE(r.interleaved.size() == 2000);
    REQUIRE(peak(r.interleaved) == 0.0f);

    const RenderResult tail = renderOffline(p, media);  // default: no regions, so only the tail
    REQUIRE(tail.frames == 24000);                      // 0.5 s at 48 kHz
}

TEST_CASE("render: default length is the end of the last region plus the tail", "[render]") {
    test::TempDir tmp;
    const Project p = makeDemoProject(tmp.path);
    REQUIRE(projectEndFrame(p) == 8 * 24000);  // keys region: 8 beats at 120 bpm
    MediaStore media(tmp.path, false);
    const RenderResult r = renderOffline(p, media);
    REQUIRE(r.frames == 8 * 24000 + 24000);
    REQUIRE(peak(r.interleaved) > 0.05f);
}

TEST_CASE("render: start frame and explicit length", "[render]") {
    test::TempDir tmp;
    const Project p = makeDemoProject(tmp.path);
    MediaStore media(tmp.path, false);
    RenderOptions all;
    all.frames = 96000;
    const RenderResult full = renderOffline(p, media, all);

    // Start exactly on the second chord (frame 48000): notes that began earlier do not sound when playback
    // starts in the middle of them, and the previous chord's 5 ms release tail (240 frames) is only in `full`.
    RenderOptions part;
    part.startFrame = 48000;
    part.frames = 6000;
    const RenderResult cut = renderOffline(p, media, part);
    REQUIRE(cut.frames == 6000);
    for (std::size_t i = 300 * 2; i < 6000 * 2; ++i) REQUIRE(std::abs(cut.interleaved[i] - full.interleaved[48000 * 2 + i]) <= 1e-6f);
}

TEST_CASE("render: identical for any block size and on repeated runs", "[render]") {
    test::TempDir tmp;
    const Project p = makeDemoProject(tmp.path);
    MediaStore media(tmp.path, false);
    RenderOptions base;
    base.frames = 60000;
    const RenderResult expected = renderOffline(p, media, base);
    REQUIRE(renderOffline(p, media, base).interleaved == expected.interleaved);
    for (const int block : {1, 64, 4096, 100000}) {
        CAPTURE(block);
        RenderOptions o = base;
        o.blockSize = block;
        REQUIRE(renderOffline(p, media, o).interleaved == expected.interleaved);
    }
}

TEST_CASE("render: missing media silences only the affected track and is reported", "[render]") {
    test::TempDir tmp;
    const Project p = makeDemoProject(tmp.path);
    std::filesystem::remove(tmp.path / "audio" / "tone.wav");
    MediaStore media(tmp.path, false);
    RenderOptions o;
    o.frames = 48000;
    const RenderResult r = renderOffline(p, media, o);
    REQUIRE(peak(r.interleaved) > 0.05f);  // the keys still play
    REQUIRE(media.missingCount() == 1);
}

TEST_CASE("render: regions entirely past the requested range and absurd options are safe", "[render]") {
    test::TempDir tmp;
    const Project p = makeDemoProject(tmp.path);
    MediaStore media(tmp.path, false);
    RenderOptions late;
    late.startFrame = 10'000'000;
    late.frames = 500;
    REQUIRE(peak(renderOffline(p, media, late).interleaved) == 0.0f);
    RenderOptions none;
    none.frames = 0;
    REQUIRE(renderOffline(p, media, none).interleaved.empty());
    RenderOptions negative;
    negative.frames = -1;
    negative.startFrame = 10'000'000;  // start beyond the project end: only the tail remains, never a negative length
    REQUIRE(renderOffline(p, media, negative).frames >= 0);
    RenderOptions zeroBlock;
    zeroBlock.frames = 100;
    zeroBlock.blockSize = 0;  // clamped to 1
    REQUIRE(renderOffline(p, media, zeroBlock).frames == 100);
}
