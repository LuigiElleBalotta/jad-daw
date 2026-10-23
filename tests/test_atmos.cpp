#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <random>

#include "lpc/adm_bwf.h"
#include "lpc/atmos.h"
#include "lpc/audio/effects.h"
#include "lpc/audio/object_panner.h"
#include "lpc/commands.h"
#include "lpc/processor_ids.h"

using namespace lpc;

namespace {
std::mt19937_64 gRng(5150);

AdmFile sampleFile() {
    AdmFile f;
    f.sampleRate = 48000;
    f.bed.assign(kAdmBedChannels, std::vector<float>(4800, 0.0f));
    for (int i = 0; i < 4800; ++i) f.bed[0][static_cast<std::size_t>(i)] = 0.25f * std::sin(0.05f * static_cast<float>(i));
    for (int o = 0; o < 2; ++o) {
        AdmObject obj;
        obj.name = o == 0 ? "Vocal" : "Guitar & Amp";
        obj.samples.resize(4800);
        for (int i = 0; i < 4800; ++i) obj.samples[static_cast<std::size_t>(i)] = 0.5f * std::sin(0.02f * static_cast<float>((o + 1) * i));
        obj.positions = {{0.0, -0.5, 0.75, 0.0, 0.0}, {0.05, 0.5, -0.25, 0.5, 0.25}};
        f.objects.push_back(std::move(obj));
    }
    return f;
}
}  // namespace

TEST_CASE("atmos: the ears hear a sound at the side with a delay and a duller far ear", "[atmos]") {
    const audio::EarCues front = audio::earCues(0.0, 1.0, 0.0, 0.0);
    CHECK(front.delayLeft == 0.0);
    CHECK(front.delayRight == 0.0);
    CHECK(front.gainLeft == front.gainRight);
    const audio::EarCues right = audio::earCues(1.0, 0.0, 0.0, 0.0);
    CHECK(right.delayLeft > 0.0004);
    CHECK(right.delayLeft < 0.0008);
    CHECK(right.delayRight == 0.0);
    CHECK(right.gainLeft < right.gainRight);
    CHECK(right.cutoffLeft < 4000.0);
    const audio::EarCues left = audio::earCues(-1.0, 0.0, 0.0, 0.0);
    CHECK(left.delayRight == right.delayLeft);
    CHECK(left.delayLeft == 0.0);
    const audio::EarCues big = audio::earCues(1.0, 0.0, 0.0, 1.0);
    CHECK(big.delayLeft < right.delayLeft);  // a big object is not placed so sharply
}

TEST_CASE("atmos: the object panner puts a click at the right where the right ear hears it first and louder", "[atmos]") {
    ProcessorRef ref{kProcObjectPanner, {{"x", 1.0}, {"y", 0.0}, {"z", 0.0}, {"size", 0.0}, {"mode", 1.0}}, "", "", false};
    auto panner = audio::makeBuiltinEffect(ref, 48000.0);
    REQUIRE(panner);
    std::vector<float> l(4800, 0.0f), r(4800, 0.0f);
    l[100] = r[100] = 1.0f;
    for (int block = 0; block < 4800; block += 480) panner->process(l.data() + block, r.data() + block, 480);
    int firstL = -1, firstR = -1;
    float peakL = 0, peakR = 0;
    for (int i = 0; i < 4800; ++i) {
        if (firstL < 0 && std::abs(l[static_cast<std::size_t>(i)]) > 0.01f) firstL = i;
        if (firstR < 0 && std::abs(r[static_cast<std::size_t>(i)]) > 0.01f) firstR = i;
        peakL = std::max(peakL, std::abs(l[static_cast<std::size_t>(i)]));
        peakR = std::max(peakR, std::abs(r[static_cast<std::size_t>(i)]));
    }
    REQUIRE(firstL >= 0);
    REQUIRE(firstR >= 0);
    CHECK(firstR < firstL);
    CHECK(peakR > peakL);
}

TEST_CASE("atmos: the stereo monitoring is a pan from the left/right place", "[atmos]") {
    ProcessorRef ref{kProcObjectPanner, {{"x", -1.0}, {"mode", 0.0}}, "", "", false};
    auto panner = audio::makeBuiltinEffect(ref, 48000.0);
    std::vector<float> l(2000, 0.5f), r(2000, 0.5f);
    for (int block = 0; block < 2000; block += 500) panner->process(l.data() + block, r.data() + block, 500);
    CHECK(l[1999] > 0.6f);
    CHECK(std::abs(r[1999]) < 0.05f);
}

TEST_CASE("atmos: an ADM BWF file is written and read back with its bed, its objects and their movement", "[atmos]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "jad_atmos_test.wav";
    const AdmFile f = sampleFile();
    writeAdmBwf(path, f);
    const AdmFile back = readAdmBwf(path);
    REQUIRE(back.sampleRate == 48000);
    REQUIRE(back.bed.size() == static_cast<std::size_t>(kAdmBedChannels));
    REQUIRE(back.objects.size() == 2);
    CHECK(back.objects[0].name == "Vocal");
    CHECK(back.objects[1].name == "Guitar & Amp");
    REQUIRE(back.objects[1].samples.size() == 4800);
    for (const std::size_t i : {0u, 17u, 4799u}) {
        CHECK(std::abs(back.objects[1].samples[i] - f.objects[1].samples[i]) < 2e-7f);
        CHECK(std::abs(back.bed[0][i] - f.bed[0][i]) < 2e-7f);
    }
    REQUIRE(back.objects[0].positions.size() == 2);
    CHECK(back.objects[0].positions[0].x == -0.5);
    CHECK(back.objects[0].positions[0].y == 0.75);
    CHECK(back.objects[0].positions[1].time == 0.05);
    CHECK(back.objects[0].positions[1].z == 0.5);
    CHECK(back.objects[0].positions[1].size == 0.25);
    const std::string xml = admXml(f);
    CHECK(xml.find("typeDefinition=\"Objects\"") != std::string::npos);
    CHECK(xml.find("DirectSpeakers") != std::string::npos);
    CHECK(xml.find("Guitar &amp; Amp") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("atmos: a file that is not an ADM BWF is refused", "[atmos]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "jad_atmos_plain.wav";
    AdmFile f = sampleFile();
    writeAdmBwf(path, f);
    {  // cut the file after the audio: no axml
        std::filesystem::resize_file(path, 44 + 100);
    }
    CHECK_THROWS(readAdmBwf(path));
    std::filesystem::remove(path);
}

TEST_CASE("atmos: a project is made from an ADM file and rendered back to one", "[atmos]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "jad_atmos_project";
    std::filesystem::remove_all(dir);
    const AdmFile f = sampleFile();
    const Project p = projectFromAdm(f, dir);
    CHECK(dolbyAtmosOn(p));
    CHECK(atmosObjects(p).size() == 2);
    CHECK(p.tracks.size() == 4);  // the master, the bed and the two objects
    const Track* vocal = nullptr;
    for (const Track& t : p.tracks)
        if (t.name == "Vocal") vocal = &t;
    REQUIRE(vocal);
    REQUIRE(objectPannerIndex(*vocal) >= 0);
    CHECK(vocal->automation.size() == 4);  // x, y, z and size
    const AdmPosition at0 = objectPositionAt(p, *vocal, 0);
    CHECK(std::abs(at0.x - -0.5) < 1e-6);
    CHECK(std::abs(at0.y - 0.75) < 1e-6);
    const AdmPosition late = objectPositionAt(p, *vocal, 960 * 4);  // far past the second point (0.05 s)
    CHECK(std::abs(late.x - 0.5) < 1e-6);
    CHECK(std::abs(late.z - 0.5) < 1e-6);
    MediaStore media(dir, false);
    AtmosExportOptions options;
    options.tailSeconds = 0.0;
    options.frames = 4800;
    const AdmFile out = renderAdm(p, media, options);
    REQUIRE(out.objects.size() == 2);
    float peak = 0.0f;
    for (const float v : out.objects[0].samples) peak = std::max(peak, std::abs(v));
    CHECK(peak > 0.3f);
    float bedPeak = 0.0f;
    for (const float v : out.bed[0]) bedPeak = std::max(bedPeak, std::abs(v));
    CHECK(bedPeak > 0.1f);  // the bed track plays in the front of the bed
    CHECK(out.objects[0].positions.front().x == -0.5);
    std::filesystem::remove_all(dir);
}
