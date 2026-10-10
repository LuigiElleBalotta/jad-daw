#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
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
