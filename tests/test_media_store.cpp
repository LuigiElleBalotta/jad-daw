#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include "lpc/audio/frame_source.h"
#include "lpc/media_store.h"
#include "lpc/wav.h"
#include "temp_dir.h"

using namespace lpc;

namespace {
MediaItem item(const char* path, int rate = 48000, int channels = 2) {
    return MediaItem{Uuid::random(), path, "h", rate, channels, 100};
}
}  // namespace

TEST_CASE("media store: opens files from the project folder (memory and streaming)", "[media]") {
    test::TempDir tmp;
    std::filesystem::create_directories(tmp.path / "audio");
    std::vector<float> samples(200);
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = float(i) / 400.0f;
    writeWav(tmp.path / "audio" / "a.wav", 48000, 2, samples);
    const MediaItem m = item("audio/a.wav");

    for (const bool streaming : {false, true}) {
        MediaStore store(tmp.path, streaming);
        auto src = store.open(m);
        REQUIRE(src != nullptr);
        REQUIRE(store.open(m) == src);  // cached by id
        std::vector<float> l(4), r(4);
        REQUIRE(src->read(10, l.data(), r.data(), 4));
        REQUIRE(l[0] == samples[20]);
        REQUIRE(store.missingCount() == 0);
    }
}

TEST_CASE("media store: a missing file is reported once and does not throw", "[media]") {
    test::TempDir tmp;
    MediaStore store(tmp.path, false);
    const MediaItem m = item("audio/gone.wav");
    REQUIRE(store.open(m) == nullptr);
    REQUIRE(store.open(m) == nullptr);
    REQUIRE(store.missingCount() == 1);
    REQUIRE(store.warnings().size() == 1);
    REQUIRE(store.warnings()[0].find("gone.wav") != std::string::npos);
}

TEST_CASE("media store: unreadable or mismatching files are reported, not thrown", "[media]") {
    test::TempDir tmp;
    std::filesystem::create_directories(tmp.path / "audio");
    { std::ofstream(tmp.path / "audio" / "bad.wav", std::ios::binary) << "this is not a wav"; }
    writeWav(tmp.path / "audio" / "rate.wav", 44100, 2, std::vector<float>(20, 0.1f));
    MediaStore store(tmp.path, false);
    REQUIRE(store.open(item("audio/bad.wav")) == nullptr);
    REQUIRE(store.open(item("audio/rate.wav", 48000)) == nullptr);  // item says 48 kHz, file is 44.1 kHz
    REQUIRE(store.missingCount() == 2);
}

TEST_CASE("media store: registered sources win and need no file", "[media]") {
    MediaStore store;
    const MediaItem m = item("audio/virtual.wav");
    auto mem = std::make_shared<audio::MemorySource>(48000, 2, std::vector<float>(20, 0.5f));
    store.registerSource(m.id, mem);
    REQUIRE(store.open(m) == mem);
    REQUIRE(store.missingCount() == 0);
}
