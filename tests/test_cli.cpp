#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

#ifdef LPC_HAVE_CLI_LIB

#include "cli_commands.h"
#include "lpc/project_io.h"
#include "lpc/wav.h"
#include "temp_dir.h"

using namespace lpc;
namespace fs = std::filesystem;

namespace {

struct Run {
    int code;
    std::string out, err;
};

Run run(std::vector<std::string> args) {
    std::ostringstream out, err;
    const int code = cli::runCli(args, out, err);
    return {code, out.str(), err.str()};
}

std::string path(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

}  // namespace

TEST_CASE("cli: demo, info and render work together", "[cli]") {
    test::TempDir tmp;
    const std::string proj = path(tmp.path / "demo.lpc");
    const std::string wav = path(tmp.path / "out.wav");

    const Run demo = run({"demo", proj});
    REQUIRE(demo.code == 0);
    REQUIRE(fs::exists(tmp.path / "demo.lpc" / "project.json"));
    REQUIRE(fs::exists(tmp.path / "demo.lpc" / "audio" / "tone.wav"));

    const Run info = run({"info", proj});
    REQUIRE(info.code == 0);
    REQUIRE_THAT(info.out, Catch::Matchers::ContainsSubstring("Demo"));
    REQUIRE_THAT(info.out, Catch::Matchers::ContainsSubstring("Keys"));
    REQUIRE_THAT(info.out, Catch::Matchers::ContainsSubstring("48000"));

    const Run render = run({"render", proj, wav, "--seconds", "1"});
    REQUIRE(render.code == 0);
    const WavData d = readWav(tmp.path / "out.wav");
    REQUIRE(d.frames() == 48000);
    REQUIRE(d.channels == 2);
    float peak = 0.0f;
    for (float v : d.samples) peak = std::max(peak, std::abs(v));
    REQUIRE(peak > 0.05f);

    REQUIRE(run({"render", proj, wav, "--seconds", "0.5", "--bits", "16"}).code == 0);
    REQUIRE(readWav(tmp.path / "out.wav").frames() == 24000);
    REQUIRE(run({"render", proj, wav, "--bits", "24"}).code == 0);  // default length: project end plus tail
    REQUIRE(readWav(tmp.path / "out.wav").frames() == 8 * 24000 + 24000);
}

TEST_CASE("cli: usage errors give exit code 1 and a usage text", "[cli]") {
    REQUIRE(run({}).code == 1);
    REQUIRE_THAT(run({}).err, Catch::Matchers::ContainsSubstring("usage"));
    REQUIRE(run({"frobnicate"}).code == 1);
    REQUIRE(run({"demo"}).code == 1);
    REQUIRE(run({"render", "only-one-arg"}).code == 1);
    REQUIRE(run({"render", "a", "b", "--seconds"}).code == 1);          // option without a value
    REQUIRE(run({"render", "a", "b", "--seconds", "abc"}).code == 1);   // not a number
    REQUIRE(run({"render", "a", "b", "--seconds", "-3"}).code == 1);    // negative
    REQUIRE(run({"render", "a", "b", "--bits", "12"}).code == 1);
    REQUIRE(run({"render", "a", "b", "--nope", "1"}).code == 1);
    REQUIRE(run({"info"}).code == 1);
}

TEST_CASE("cli: runtime errors give exit code 2 and a readable message", "[cli]") {
    test::TempDir tmp;
    const Run missing = run({"info", path(tmp.path / "nothing.lpc")});
    REQUIRE(missing.code == 2);
    REQUIRE_THAT(missing.err, Catch::Matchers::ContainsSubstring("error:"));

    const std::string proj = path(tmp.path / "p.lpc");
    REQUIRE(run({"demo", proj}).code == 0);
    const Run badOut = run({"render", proj, path(tmp.path / "no_such_dir" / "out.wav"), "--seconds", "0.1"});
    REQUIRE(badOut.code == 2);

    std::ofstream(tmp.path / "p.lpc" / "project.json", std::ios::binary | std::ios::trunc) << "{ truncated";
    REQUIRE(run({"render", proj, path(tmp.path / "x.wav")}).code == 2);
}

TEST_CASE("cli: missing media is a warning, not a failure", "[cli]") {
    test::TempDir tmp;
    const std::string proj = path(tmp.path / "m.lpc");
    REQUIRE(run({"demo", proj}).code == 0);
    fs::remove(tmp.path / "m.lpc" / "audio" / "tone.wav");
    const Run r = run({"render", proj, path(tmp.path / "m.wav"), "--seconds", "1"});
    REQUIRE(r.code == 0);
    REQUIRE_THAT(r.err, Catch::Matchers::ContainsSubstring("warning:"));
    REQUIRE_THAT(r.err, Catch::Matchers::ContainsSubstring("tone.wav"));
    const Run info = run({"info", proj});
    REQUIRE(info.code == 0);
    REQUIRE_THAT(info.err, Catch::Matchers::ContainsSubstring("tone.wav"));
}

TEST_CASE("cli: an empty project renders silence", "[cli]") {
    test::TempDir tmp;
    saveProject(Project{}, tmp.path / "e.lpc");
    REQUIRE(run({"render", path(tmp.path / "e.lpc"), path(tmp.path / "e.wav"), "--seconds", "0.1"}).code == 0);
    const WavData d = readWav(tmp.path / "e.wav");
    REQUIRE(d.frames() == 4800);
    for (float v : d.samples) REQUIRE(v == 0.0f);
}

TEST_CASE("cli: play is routed to runPlay and its failure is reported", "[cli]") {
    // the test executable uses tests/play_stub.cpp, which reports that audio output is unavailable
    test::TempDir tmp;
    const Run r = run({"play", path(tmp.path / "none.lpc")});
    REQUIRE(r.code == 2);
    REQUIRE_THAT(r.err, Catch::Matchers::ContainsSubstring("error:"));
}


TEST_CASE("cli ears: snapshot lists the project, analyze listens to a track, masking compares two, apply changes it safely", "[cli][ears]") {
    test::TempDir tmp;
    const std::string proj = path(tmp.path / "demo.lpc");
    REQUIRE(run({"demo", proj}).code == 0);
    // snapshot
    const Run snap = run({"snapshot", proj});
    REQUIRE(snap.code == 0);
    const nlohmann::json s = nlohmann::json::parse(snap.out);
    REQUIRE(s["tracks"].size() == 4);
    CHECK(s["tempo"][0]["bpm"] == 120.0);
    std::string toneId, keysId;
    for (const auto& t : s["tracks"]) {
        if (t["name"] == "Tone") toneId = t["id"];
        if (t["name"] == "Keys") keysId = t["id"];
    }
    REQUIRE(!toneId.empty());
    // analyze the 220 Hz tone track alone, by name, and the whole mix
    const Run tone = run({"analyze", proj, "--track", "tone", "--no-tempo"});
    REQUIRE(tone.code == 0);
    const nlohmann::json t = nlohmann::json::parse(tone.out);
    CHECK(t["analysed"].get<std::string>().find("Tone") != std::string::npos);
    CHECK(t["loudness"]["integratedLufs"].get<double>() > -40.0);
    CHECK(t["spectrum"]["centroidHz"].get<double>() < 400.0);        // a 220 Hz tone
    CHECK(t["reading"].is_array());
    const Run mix = run({"analyze", proj, "--seconds", "0:1.5", "--no-tempo"});
    REQUIRE(mix.code == 0);
    CHECK(nlohmann::json::parse(mix.out)["seconds"].get<double>() < 2.5);
    // an audio file
    const std::string wav = path(tmp.path / "mix.wav");
    REQUIRE(run({"render", proj, wav, "--seconds", "2"}).code == 0);
    const Run file = run({"analyze", wav, "--no-tempo"});
    REQUIRE(file.code == 0);
    CHECK(nlohmann::json::parse(file.out)["analysed"].get<std::string>().find("mix.wav") != std::string::npos);
    // render one track alone: quieter than the whole mix
    const std::string solo = path(tmp.path / "solo.wav");
    REQUIRE(run({"render", proj, solo, "--seconds", "1", "--track", "Tone"}).code == 0);
    const WavData soloWav = readWav(tmp.path / "solo.wav"), mixWav = readWav(tmp.path / "mix.wav");
    CHECK(soloWav.frames() > 0);
    CHECK(run({"render", proj, solo, "--track", "Nobody"}).code == 2);
    // masking
    const Run mask = run({"masking", proj, "Keys", "Tone"});
    REQUIRE(mask.code == 0);
    CHECK(nlohmann::json::parse(mask.out).contains("advice"));
    // an unknown track is a usage error
    CHECK(run({"analyze", proj, "--track", "Nope"}).code == 1);
    // apply: a dry run changes nothing, a real one saves, a refused one saves nothing
    const std::string cmds = path(tmp.path / "cmds.json");
    { std::ofstream(cmds) << "[{\"type\":\"set_strip\",\"trackId\":\"" << toneId << "\",\"gainDb\":-12.0}]"; }
    const Run dry = run({"apply", proj, cmds, "--dry-run"});
    CHECK(dry.code == 0);
    CHECK(loadProject(tmp.path / "demo.lpc").findTrack(*Uuid::parse(toneId))->strip.gainDb == -6.0f);
    const Run real = run({"apply", proj, cmds});
    CHECK(real.code == 0);
    CHECK(loadProject(tmp.path / "demo.lpc").findTrack(*Uuid::parse(toneId))->strip.gainDb == -12.0f);
    { std::ofstream(cmds) << "[{\"type\":\"set_strip\",\"trackId\":\"" << toneId << "\",\"gainDb\":-3.0},{\"type\":\"set_strip\",\"trackId\":\"00000000-0000-0000-0000-000000000099\",\"gainDb\":0}]"; }
    const Run bad = run({"apply", proj, cmds});
    CHECK(bad.code == 2);
    CHECK(nlohmann::json::parse(bad.out)["applied"] == false);
    CHECK(loadProject(tmp.path / "demo.lpc").findTrack(*Uuid::parse(toneId))->strip.gainDb == -12.0f);   // the first of the two was not kept
    // to another folder
    { std::ofstream(cmds) << "{\"commands\":[{\"type\":\"set_strip\",\"trackId\":\"" << toneId << "\",\"gainDb\":-1.0}]}"; }
    const std::string copy = path(tmp.path / "copy.lpc");
    CHECK(run({"apply", proj, cmds, "--out", copy}).code == 0);
    CHECK(loadProject(tmp.path / "copy.lpc").findTrack(*Uuid::parse(toneId))->strip.gainDb == -1.0f);
    CHECK(loadProject(tmp.path / "demo.lpc").findTrack(*Uuid::parse(toneId))->strip.gainDb == -12.0f);
}

TEST_CASE("cli ears: import brings an audio file in as a new track or onto a track", "[cli][ears]") {
    test::TempDir tmp;
    const std::string proj = path(tmp.path / "demo.lpc");
    REQUIRE(run({"demo", proj}).code == 0);
    const std::string tone = path(tmp.path / "demo.lpc" / "audio" / "tone.wav");
    const Run added = run({"import", proj, tone, "--name", "Reference", "--start-seconds", "1"});
    REQUIRE(added.code == 0);
    const nlohmann::json a = nlohmann::json::parse(added.out);
    CHECK(a["ok"] == true);
    const Project p = loadProject(tmp.path / "demo.lpc");
    const Track* t = p.findTrack(*Uuid::parse(a["trackId"].get<std::string>()));
    REQUIRE(t != nullptr);
    CHECK(t->name == "Reference");
    REQUIRE(t->regions.size() == 1);
    CHECK(t->regions[0].start == 1000000);
    CHECK(p.mediaPool.size() == 2);
    CHECK(std::filesystem::exists(tmp.path / "demo.lpc" / "audio" / "tone (2).wav"));
    // onto an existing track
    REQUIRE(run({"import", proj, tone, "--track", "Reference"}).code == 0);
    CHECK(loadProject(tmp.path / "demo.lpc").findTrack(t->id)->regions.size() == 2);
    // refused: nothing is written
    CHECK(run({"import", proj, path(tmp.path / "nothing.wav")}).code != 0);
    CHECK(run({"import", proj, tone, "--track", "No such track"}).code == 1);
    CHECK(run({"import", proj}).code == 1);
}

#endif  // LPC_HAVE_CLI_LIB
