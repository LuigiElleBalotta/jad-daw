#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <fstream>
#include <sstream>

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

#endif  // LPC_HAVE_CLI_LIB
