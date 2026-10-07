#define _CRT_SECURE_NO_WARNINGS
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <string>
#include "lpc/demo_project.h"
#include "lpc/offline_render.h"
#include "lpc/wav.h"
#include "temp_dir.h"

using namespace lpc;
namespace fs = std::filesystem;

namespace {

// Compares a render with tests/golden/<name>.wav. Run with LPC_UPDATE_GOLDEN=1 to (re)write the reference.
void checkGolden(const std::string& name, const RenderResult& actual) {
    const fs::path file = fs::path(LPC_TEST_DIR) / "golden" / (name + ".wav");
    const char* update = std::getenv("LPC_UPDATE_GOLDEN");
    if (update && std::string(update) == "1") {
        fs::create_directories(file.parent_path());
        writeWav(file, actual.sampleRate, 2, actual.interleaved, WavFormat::Pcm16);
        WARN("golden file written: " << file.string());
        return;
    }
    REQUIRE(fs::exists(file));
    const WavData golden = readWav(file);
    REQUIRE(golden.sampleRate == actual.sampleRate);
    REQUIRE(golden.samples.size() == actual.interleaved.size());
    float goldenPeak = 0.0f;
    double maxDiff = 0.0;
    for (std::size_t i = 0; i < golden.samples.size(); ++i) {
        goldenPeak = std::max(goldenPeak, std::abs(golden.samples[i]));
        maxDiff = std::max(maxDiff, static_cast<double>(std::abs(golden.samples[i] - actual.interleaved[i])));
    }
    REQUIRE(goldenPeak > 0.05f);  // a silent reference would make this test meaningless
    REQUIRE(maxDiff <= 2e-4);     // 16-bit quantisation of the reference is about 3e-5
}

}  // namespace

TEST_CASE("golden: the demo project renders as the reference file", "[golden]") {
    test::TempDir tmp;
    const Project p = makeDemoProject(tmp.path);
    MediaStore media(tmp.path, /*streaming=*/false);
    RenderOptions o;
    o.frames = 96000;  // the first two seconds
    checkGolden("demo", renderOffline(p, media, o));
}
