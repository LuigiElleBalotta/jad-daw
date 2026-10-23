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

#include "lpc/atmos_render.h"

TEST_CASE("atmos: an object is spread over the speakers near it, with the power kept", "[atmos]") {
    const AtmosDownmix dm;
    const SpeakerGains front = objectGains(MonitorFormat::S714, 0.0, 1.0, 0.0, 0.0, dm);
    CHECK(front[2] > front[0]);        // the centre speaker has it
    CHECK(front[2] > front[8]);
    CHECK(front[3] == 0.0f);           // nothing goes to the LFE
    double power = 0;
    for (const float g : front) power += static_cast<double>(g) * g;
    CHECK(std::abs(power - 1.0) < 1e-4);
    const SpeakerGains overhead = objectGains(MonitorFormat::S714, 0.0, 0.0, 1.0, 0.0, dm);
    CHECK(overhead[8] + overhead[9] + overhead[10] + overhead[11] > overhead[0] + overhead[1] + overhead[2]);
    const SpeakerGains right = objectGains(MonitorFormat::S714, 1.0, 0.0, 0.0, 0.0, dm);
    CHECK(right[5] > right[4]);        // the right side surround, not the left
    // a big object spreads out
    const SpeakerGains big = objectGains(MonitorFormat::S714, 1.0, 0.0, 0.0, 1.0, dm);
    CHECK(big[5] < right[5]);
}

TEST_CASE("atmos: the smaller layouts fold the speakers they lack into the ones they have", "[atmos]") {
    const AtmosDownmix dm;
    const SpeakerGains top = objectGains(MonitorFormat::S51, -1.0, 0.5, 1.0, 0.0, dm);  // left front top: no height speakers in 5.1
    CHECK(top[8] == 0.0f);
    CHECK(top[10] == 0.0f);
    CHECK(top[0] + top[4] > 0.3f);
    const SpeakerGains s71 = objectGains(MonitorFormat::S71, -1.0, -1.0, 0.0, 0.0, dm);
    CHECK(s71[6] > s71[4]);            // the 7.1 keeps the rear surrounds
    const SpeakerGains s51 = objectGains(MonitorFormat::S51, -1.0, -1.0, 0.0, 0.0, dm);
    CHECK(s51[6] == 0.0f);
    CHECK(s51[4] > 0.5f);              // and the 5.1 folds them into Ls
    CHECK(monitorChannels(MonitorFormat::S514) == 10);
    CHECK(monitorChannelNames(MonitorFormat::S512).back() == "Rtm");
    const SpeakerGains tm = objectGains(MonitorFormat::S512, -1.0, 0.0, 1.0, 0.0, dm);
    CHECK(tm[8] > 0.5f);               // the top middle of 5.1.2
}

TEST_CASE("atmos: the downmix algorithms and the trims change the result as the guide says", "[atmos]") {
    AtmosDownmix loro, proLogic;
    proLogic.to51 = 1;
    const SpeakerGains a = objectGains(MonitorFormat::S51, 1.0, -1.0, 0.0, 0.0, loro);
    const SpeakerGains b = objectGains(MonitorFormat::S51, 1.0, -1.0, 0.0, 0.0, proLogic);
    CHECK(b[5] != a[5]);               // Pro Logic IIx adds the other rear speaker at -6.2 dB
    AtmosDownmix direct;
    direct.to51 = 3;
    const SpeakerGains d = objectGains(MonitorFormat::S51, 1.0, -1.0, 0.0, 0.0, direct);
    CHECK(d[5] > 0.5f);                // rendered straight to the 5.1 surround
    AtmosDownmix trimmed;
    trimmed.trim.heightDb = -12.0;
    const SpeakerGains h0 = objectGains(MonitorFormat::S514, 0.0, 0.0, 1.0, 0.0, loro);
    const SpeakerGains h1 = objectGains(MonitorFormat::S514, 0.0, 0.0, 1.0, 0.0, trimmed);
    CHECK(h1[8] < h0[8] * 0.3f);
    AtmosDownmix tilted;
    tilted.trim.plane = 1.0;
    const SpeakerGains p1 = objectGains(MonitorFormat::S71, 0.0, 0.0, 0.0, 0.0, tilted);
    const SpeakerGains p0 = objectGains(MonitorFormat::S71, 0.0, 0.0, 0.0, 0.0, loro);
    CHECK(p1[2] > p0[2]);              // the balance moved to the front speakers
    CHECK(p1[6] < p0[6]);
}

TEST_CASE("atmos: the stereo downmixes of a back object", "[atmos]") {
    AtmosDownmix dm;
    const StereoGains direct = stereoGains(MonitorFormat::Direct20, 1.0, -1.0, 0.0, 0.0, dm);
    CHECK(direct.r > direct.l);
    const StereoGains lo = stereoGains(MonitorFormat::From51To20, 1.0, -1.0, 0.0, 0.0, dm);
    CHECK(lo.r > lo.l);
    dm.to20 = 1;  // Lt/Rt: the surround goes in with opposite signs
    const StereoGains lt = stereoGains(MonitorFormat::From51To20, 1.0, -1.0, 0.0, 0.0, dm);
    CHECK(lt.l < 0.0f);
    dm.to20 = 2;  // with phase 90 the surround is in the shifted copy
    const StereoGains ph = stereoGains(MonitorFormat::From51To20, 1.0, -1.0, 0.0, 0.0, dm);
    CHECK(ph.lPhase < 0.0f);
    CHECK(ph.rPhase > 0.0f);
}

TEST_CASE("atmos: the panner in a speaker format keeps the sound and publishes the gains", "[atmos]") {
    ProcessorRef ref{kProcObjectPanner, {{"x", 1.0}, {"y", 1.0}, {"z", 0.0}, {"mode", 8.0}}, "", "", false};
    auto panner = audio::makeBuiltinEffect(ref, 48000.0);
    REQUIRE(panner);
    std::vector<float> l(256, 0.5f), r(256, 0.5f);
    panner->process(l.data(), r.data(), 256);
    CHECK(l[100] == 0.5f);  // untouched
    const float* gains = panner->speakerGains();
    REQUIRE(gains);
    CHECK(gains[1] > gains[0]);
    ProcessorRef binaural{kProcObjectPanner, {{"mode", 1.0}}, "", "", false};
    CHECK(audio::makeBuiltinEffect(binaural, 48000.0)->speakerGains() == nullptr);
}

TEST_CASE("atmos: a mix is rendered to a speaker file with the bed and the moving objects", "[atmos]") {
    AdmFile f = sampleFile();
    f.objects.resize(1);
    f.objects[0].positions = {{0.0, -1.0, 1.0, 0.0, 0.0}, {0.1, 1.0, 1.0, 0.0, 0.0}};  // from the front left to the front right in 0.1 s
    const AtmosDownmix dm;
    const std::vector<float> out = renderSpeakers(f, MonitorFormat::S51, dm);
    const std::size_t frames = 4800;
    REQUIRE(out.size() == frames * 6);
    double lStart = 0, rStart = 0, lEnd = 0, rEnd = 0;
    for (std::size_t i = 0; i < 400; ++i) {
        lStart += std::abs(out[i * 6]);
        rStart += std::abs(out[i * 6 + 1]);
    }
    for (std::size_t i = 4400; i < 4800; ++i) {
        lEnd += std::abs(out[i * 6]);
        rEnd += std::abs(out[i * 6 + 1]);
    }
    CHECK(lStart > rStart);  // the sound begins on the left
    CHECK(rEnd > lEnd);      // and has moved to the right
    const std::vector<float> stereo = renderSpeakers(f, MonitorFormat::Direct20, dm);
    CHECK(stereo.size() == frames * 2);
}
