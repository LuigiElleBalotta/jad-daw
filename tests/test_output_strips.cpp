#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include <nlohmann/json.hpp>
#include "lpc/audio/engine.h"
#include "lpc/commands.h"
#include "lpc/demo_project.h"
#include "lpc/graph_builder.h"
#include "lpc/media_store.h"
#include "lpc/model_json.h"
#include "lpc/offline_render.h"
#include "lpc/validation.h"

using namespace lpc;
using nlohmann::json;

namespace {

Track outputStrip(int pair, const std::string& name = "") {
    Track t;
    t.id = Uuid::random();
    t.kind = TrackKind::Output;
    t.name = name.empty() ? "Out " + std::to_string(2 * pair + 1) + "-" + std::to_string(2 * pair + 2) : name;
    t.outputPair = pair;
    t.showInTracks = false;
    return t;
}

bool applyOk(Project& p, const json& command, std::string* error = nullptr) {
    const ApplyResult r = commandFromJson(command)->apply(p);
    if (!r.ok() && error) *error = r.error->code;
    return r.ok();
}

double energy(const std::vector<float>& v) {
    double e = 0.0;
    for (float x : v) e += static_cast<double>(x) * x;
    return e;
}

const Track* keysOf(const Project& p) {
    for (const Track& t : p.tracks)
        if (t.kind == TrackKind::Instrument) return &t;
    return nullptr;
}

}  // namespace

TEST_CASE("output strips: pairs are 1 to 8 and unique, the strip ends the flow, only an output track has a pair", "[output]") {
    Project p = makeDemoProject({});
    REQUIRE(applyOk(p, {{"type", "add_track"}, {"track", outputStrip(1)}, {"index", -1}}));
    std::string code;
    CHECK(!applyOk(p, {{"type", "add_track"}, {"track", outputStrip(1, "again")}, {"index", -1}}, &code));  // the pair is taken
    CHECK(code == "duplicate_id");
    CHECK(!applyOk(p, {{"type", "add_track"}, {"track", outputStrip(0, "zero")}, {"index", -1}}));
    CHECK(!applyOk(p, {{"type", "add_track"}, {"track", outputStrip(9, "nine")}, {"index", -1}}));
    Track routed = outputStrip(2);
    routed.strip.output = p.master()->id;  // an output strip has no output
    CHECK(!applyOk(p, {{"type", "add_track"}, {"track", routed}, {"index", -1}}));
    Track plain;
    plain.id = Uuid::random();
    plain.kind = TrackKind::Bus;
    plain.name = "Bus";
    plain.outputPair = 3;  // a bus has no pair
    CHECK(!applyOk(p, {{"type", "add_track"}, {"track", plain}, {"index", -1}}));
    CHECK(checkProject(p) == std::nullopt);
}

TEST_CASE("output strips: tracks route to them, they cannot route or send on, and the JSON round trip keeps the pair", "[output]") {
    Project p = makeDemoProject({});
    const Track strip = outputStrip(1);
    REQUIRE(applyOk(p, {{"type", "add_track"}, {"track", strip}, {"index", -1}}));
    const Uuid keys = keysOf(p)->id;
    REQUIRE(applyOk(p, {{"type", "set_output"}, {"trackId", keys}, {"output", strip.id}}));
    CHECK(p.findTrack(keys)->strip.output == strip.id);
    std::string code;
    CHECK(!applyOk(p, {{"type", "set_output"}, {"trackId", strip.id}, {"output", p.master()->id}}, &code));
    CHECK(code == "invalid_kind");
    CHECK(!applyOk(p, {{"type", "remove_track"}, {"trackId", strip.id}}, &code));  // a track still routes to it
    CHECK(code == "in_use");
    const json j = toJson(p);
    const Project back = projectFromJson(j);
    const Track* again = back.findTrack(strip.id);
    REQUIRE(again != nullptr);
    CHECK(again->kind == TrackKind::Output);
    CHECK(again->outputPair == 1);
    CHECK(back.findTrack(keys)->strip.output == strip.id);
    CHECK(checkProject(back) == std::nullopt);
}

TEST_CASE("output strips: the signal of a routed track leaves Stereo Out and plays on its pair", "[output]") {
    Project p = makeDemoProject({});
    const Uuid keys = keysOf(p)->id;
    Track strip = outputStrip(1);
    strip.strip.gainDb = -6.0f;  // the strip's own fader works
    REQUIRE(applyOk(p, {{"type", "add_track"}, {"track", strip}, {"index", -1}}));
    MediaStore media({}, false);
    RenderOptions o;
    o.tailSeconds = 0.1;
    const RenderResult before = renderOffline(p, media, o);
    const double stereoBefore = energy(before.interleaved);
    REQUIRE(stereoBefore > 0.0);
    REQUIRE(applyOk(p, {{"type", "set_output"}, {"trackId", keys}, {"output", strip.id}}));
    const RenderResult after = renderOffline(p, media, o);
    CHECK(energy(after.interleaved) < stereoBefore);  // the keys left Stereo Out
    RenderOptions pair = o;
    pair.outputPair = 1;
    const RenderResult second = renderOffline(p, media, pair);
    CHECK(energy(second.interleaved) > 0.0);  // and are heard on outputs 3-4
    // the strip's own fader works: at 0 dB the pair carries four times the energy of -6 dB
    Project flat = p;
    flat.findTrack(strip.id)->strip.gainDb = 0.0f;
    CHECK(std::abs(energy(renderOffline(flat, media, pair).interleaved) / energy(second.interleaved) - 4.0) < 0.2);
    // a pair without a strip is silent
    RenderOptions empty = o;
    empty.outputPair = 2;
    CHECK(energy(renderOffline(p, media, empty).interleaved) == 0.0);
}

TEST_CASE("output strips: the engine fills the extra buffers of the device and silences the pairs without a strip", "[output]") {
    Project p = makeDemoProject({});
    Track strip = outputStrip(1);
    REQUIRE(applyOk(p, {{"type", "add_track"}, {"track", strip}, {"index", -1}}));
    REQUIRE(applyOk(p, {{"type", "set_output"}, {"trackId", keysOf(p)->id}, {"output", strip.id}}));
    audio::AudioEngine engine(p.sampleRate);
    MediaStore media({}, false);
    for (const audio::AudioMsg& m : initialMessages(p, media, nullptr)) engine.applyDirect(m);
    audio::AudioMsg play;
    play.kind = audio::MsgKind::Play;
    engine.applyDirect(play);
    constexpr int n = 4800;
    std::vector<float> l(n), r(n), a(n, 9.0f), b(n, 9.0f), c(n, 9.0f), d(n, 9.0f);
    float* extra[4] = {a.data(), b.data(), c.data(), d.data()};
    double pair1 = 0.0, pair2 = 0.0;
    for (int block = 0; block < 5; ++block) {
        engine.processBlock(l.data(), r.data(), n, extra, 2);
        pair1 += energy(a) + energy(b);
        pair2 += energy(c) + energy(d);
    }
    CHECK(pair1 > 0.0);
    CHECK(pair2 == 0.0);  // pair 2 has no strip: silence, not the 9.0 the buffer held
}

TEST_CASE("master volume: one fader scales Stereo Out and the Output strips, is undoable and survives the JSON round trip", "[output][master]") {
    Project p = makeDemoProject({});
    Track strip = outputStrip(1);
    REQUIRE(applyOk(p, {{"type", "add_track"}, {"track", strip}, {"index", -1}}));
    REQUIRE(applyOk(p, {{"type", "set_output"}, {"trackId", keysOf(p)->id}, {"output", strip.id}}));
    MediaStore media({}, false);
    RenderOptions stereo;
    stereo.tailSeconds = 0.1;
    RenderOptions pair = stereo;
    pair.outputPair = 1;
    const double stereo0 = energy(renderOffline(p, media, stereo).interleaved);
    const double pair0 = energy(renderOffline(p, media, pair).interleaved);
    REQUIRE(stereo0 > 0.0);
    REQUIRE(pair0 > 0.0);
    const ApplyResult r = commandFromJson({{"type", "set_master_volume"}, {"gainDb", -6.0}})->apply(p);
    REQUIRE(r.ok());
    CHECK(p.masterVolumeDb == -6.0f);
    // -6 dB is a quarter of the energy, on both outputs (the gain ramps within the first block: a small difference)
    CHECK(std::abs(energy(renderOffline(p, media, stereo).interleaved) / stereo0 - 0.2512) < 0.01);
    CHECK(std::abs(energy(renderOffline(p, media, pair).interleaved) / pair0 - 0.2512) < 0.01);
    CHECK(checkProject(projectFromJson(toJson(p))) == std::nullopt);
    CHECK(projectFromJson(toJson(p)).masterVolumeDb == -6.0f);
    r.inverse->apply(p);  // the inverse command
    CHECK(p.masterVolumeDb == 0.0f);
    std::string code;
    CHECK(!applyOk(p, {{"type", "set_master_volume"}, {"gainDb", 40.0}}, &code));
    CHECK(code == "bad_value");
    CHECK(!applyOk(p, {{"type", "set_master_volume"}, {"gainDb", -200.0}}));
    // a project saved before the Master existed reads as 0 dB
    json j = toJson(Project{});
    j.erase("masterVolumeDb");
    CHECK(projectFromJson(j).masterVolumeDb == 0.0f);
}
