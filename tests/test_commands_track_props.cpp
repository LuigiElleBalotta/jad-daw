#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <random>
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "lpc/model_json.h"
#include "lpc/validation.h"

using namespace lpc;

namespace {
std::mt19937_64 gRng(4242);
Track track(TrackKind kind, const char* name) {
    Track t;
    t.id = Uuid::random(gRng);
    t.kind = kind;
    t.name = name;
    return t;
}
}  // namespace

TEST_CASE("set_track_props: renames and recolours, undo restores", "[commands][props]") {
    Project p{Uuid::random(gRng)};
    const Track a = track(TrackKind::Audio, "Audio");
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    const Project before = p;
    TrackPatch patch;
    patch.name = "Lead \xC3\xA8 \xE6\x9B\xB2";  // non-ASCII is fine
    patch.color = "orange";
    auto res = makeSetTrackProps(a.id, patch)->apply(p);
    REQUIRE(res.ok());
    REQUIRE(p.findTrack(a.id)->name == *patch.name);
    REQUIRE(p.findTrack(a.id)->color == "orange");
    REQUIRE(res.inverse->apply(p).ok());
    REQUIRE(p == before);
}

TEST_CASE("set_track_props: a patch changes only the fields it carries", "[commands][props]") {
    Project p{Uuid::random(gRng)};
    Track a = track(TrackKind::Audio, "Audio");
    a.color = "teal";
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    TrackPatch patch;
    patch.name = "Only name";
    REQUIRE(makeSetTrackProps(a.id, patch)->apply(p).ok());
    REQUIRE(p.findTrack(a.id)->color == "teal");
}

TEST_CASE("set_track_props: bad names, colours, tracks are rejected and change nothing", "[commands][props]") {
    Project p{Uuid::random(gRng)};
    const Track a = track(TrackKind::Audio, "Audio");
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    const Project before = p;
    auto bad = [&](TrackPatch patch, const Uuid& id, const char* code) {
        auto r = makeSetTrackProps(id, patch)->apply(p);
        REQUIRE_FALSE(r.ok());
        REQUIRE(r.error->code == code);
        REQUIRE(p == before);
    };
    TrackPatch empty;
    empty.name = "";
    bad(empty, a.id, "bad_value");
    TrackPatch longName;
    longName.name = std::string(65, 'x');
    bad(longName, a.id, "bad_value");
    TrackPatch control;
    control.name = "tab\there";
    bad(control, a.id, "bad_value");
    TrackPatch colour;
    colour.color = "chartreuse";
    bad(colour, a.id, "bad_value");
    bad(TrackPatch{std::string("x"), std::nullopt}, Uuid::random(gRng), "not_found");
    bad(TrackPatch{std::string("Master?"), std::nullopt}, p.master()->id, "invalid_kind");

    TrackPatch sixtyFour;
    sixtyFour.name = std::string(64, 'x');
    REQUIRE(makeSetTrackProps(a.id, sixtyFour)->apply(p).ok());
}

TEST_CASE("add_track applies the same name and colour rules", "[commands][props]") {
    Project p{Uuid::random(gRng)};
    Track t = track(TrackKind::Audio, "");
    REQUIRE(makeAddTrack(t)->apply(p).error->code == "bad_value");
    t.name = "ok";
    t.color = "nope";
    REQUIRE(makeAddTrack(t)->apply(p).error->code == "bad_value");
    t.color = "";
    REQUIRE(makeAddTrack(t)->apply(p).ok());
}

TEST_CASE("load: a project with an invalid track name is rejected", "[commands][props][io]") {
    Project p{Uuid::random(gRng)};
    Track t = track(TrackKind::Audio, "Fine");
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    nlohmann::json j = toJson(p);
    j["tracks"][1]["name"] = std::string(200, 'x');
    REQUIRE_THROWS(projectFromJson(j));
}

TEST_CASE("set_signature: sets, validates, undo restores or removes", "[commands][signature]") {
    Project p{Uuid::random(gRng)};
    const Project before = p;
    auto res = makeSetSignature(4 * kPPQ * 4, 3, 4)->apply(p);  // bar 5
    REQUIRE(res.ok());
    REQUIRE(p.tempoMap.signatures().size() == 2);
    REQUIRE(res.inverse->apply(p).ok());
    REQUIRE(p == before);

    auto change = makeSetSignature(0, 6, 8)->apply(p);  // replace the event at tick 0
    REQUIRE(change.ok());
    REQUIRE(p.tempoMap.signatures().front().numerator == 6);
    REQUIRE(change.inverse->apply(p).ok());
    REQUIRE(p == before);

    for (auto [n, d] : {std::pair<int, int>{0, 4}, {33, 4}, {4, 0}, {4, 3}, {4, 64}, {-1, 4}}) {
        auto bad = makeSetSignature(0, n, d)->apply(p);
        REQUIRE_FALSE(bad.ok());
        REQUIRE(bad.error->code == "bad_value");
    }
    REQUIRE(makeSetSignature(-1, 4, 4)->apply(p).error->code == "bad_value");
    REQUIRE(makeRemoveSignature(0)->apply(p).error->code == "bad_value");    // tick 0 cannot be removed
    REQUIRE(makeRemoveSignature(960)->apply(p).error->code == "bad_value");  // no event there
    REQUIRE(p == before);
}

TEST_CASE("the property and signature commands survive a JSON round trip", "[commands][props][json]") {
    TrackPatch patch;
    patch.name = "N";
    patch.color = "red";
    std::vector<CommandPtr> cmds;
    cmds.push_back(makeSetTrackProps(Uuid::random(gRng), patch));
    cmds.push_back(makeSetTrackProps(Uuid::random(gRng), TrackPatch{}));
    cmds.push_back(makeSetSignature(960, 7, 8));
    cmds.push_back(makeRemoveSignature(960));
    for (const CommandPtr& c : cmds) {
        const nlohmann::json j = c->toJson();
        REQUIRE(commandFromJson(j)->toJson() == j);
    }
}

TEST_CASE("set_track_props: showInTracks hides a bus and undo restores it", "[commands][props][visibility]") {
    Project p{Uuid::random(gRng)};
    const Track bus = track(TrackKind::Bus, "Bus 1");
    REQUIRE(makeAddTrack(bus)->apply(p).ok());
    const Project before = p;
    TrackPatch patch;
    patch.showInTracks = false;
    auto res = makeSetTrackProps(bus.id, patch)->apply(p);
    REQUIRE(res.ok());
    REQUIRE_FALSE(p.findTrack(bus.id)->showInTracks);
    REQUIRE(res.inverse->apply(p).ok());
    REQUIRE(p == before);
    REQUIRE(p.findTrack(bus.id)->showInTracks);
}

TEST_CASE("set_track_props: any track but the master can be hidden", "[commands][props][visibility]") {
    Project p{Uuid::random(gRng)};
    const Track audio = track(TrackKind::Audio, "Audio");
    REQUIRE(makeAddTrack(audio)->apply(p).ok());
    TrackPatch patch;
    patch.showInTracks = false;
    auto hidden = makeSetTrackProps(audio.id, patch)->apply(p);
    REQUIRE(hidden.ok());
    REQUIRE_FALSE(p.findTrack(audio.id)->showInTracks);
    REQUIRE(hidden.inverse->apply(p).ok());
    REQUIRE(p.findTrack(audio.id)->showInTracks);
    const Project before = p;
    auto r = makeSetTrackProps(p.master()->id, patch)->apply(p);   // the master is never listed in the Tracks area anyway
    REQUIRE_FALSE(r.ok());
    REQUIRE(p == before);
}

TEST_CASE("add_track: a hidden track is accepted, the master is not hidden", "[commands][visibility]") {
    Project p{Uuid::random(gRng)};
    Track bus = track(TrackKind::Bus, "Bus 1");
    bus.showInTracks = false;
    auto added = makeAddTrack(bus)->apply(p);
    REQUIRE(added.ok());
    REQUIRE_FALSE(p.findTrack(bus.id)->showInTracks);
    REQUIRE(added.inverse->apply(p).ok());

    Track audio = track(TrackKind::Audio, "Audio");
    audio.showInTracks = false;
    REQUIRE(makeAddTrack(audio)->apply(p).ok());
}

TEST_CASE("set_track_props: the JSON command carries showInTracks", "[commands][json][visibility]") {
    const Uuid id = Uuid::random(gRng);
    TrackPatch patch;
    patch.showInTracks = false;
    const auto j = makeSetTrackProps(id, patch)->toJson();
    REQUIRE(j.at("showInTracks") == false);
    const auto back = commandFromJson(j);
    REQUIRE(back->toJson() == j);
}

TEST_CASE("set_track_props: an icon is a short plain key, undo restores, json keeps it", "[commands][props]") {
    Project p{Uuid::random(gRng)};
    const Track a = track(TrackKind::Audio, "Audio");
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    const Project before = p;
    TrackPatch bad;
    bad.icon = "Guitar!";
    REQUIRE_FALSE(makeSetTrackProps(a.id, bad)->apply(p).ok());
    bad.icon = std::string(41, 'a');
    REQUIRE_FALSE(makeSetTrackProps(a.id, bad)->apply(p).ok());
    REQUIRE(p == before);
    TrackPatch patch;
    patch.icon = "guitar";
    auto res = makeSetTrackProps(a.id, patch)->apply(p);
    REQUIRE(res.ok());
    REQUIRE(p.findTrack(a.id)->icon == "guitar");
    REQUIRE(projectFromJson(toJson(p)) == p);
    REQUIRE(toJson(before)["tracks"][1].contains("icon") == false);
    REQUIRE(res.inverse->apply(p).ok());
    REQUIRE(p == before);
}

TEST_CASE("set_project_key: a key is a tonic with a sharp and a mode, undo restores, json keeps it", "[commands][props]") {
    Project p{Uuid::random(gRng)};
    REQUIRE(p.key == "C major");
    const Project before = p;
    REQUIRE_FALSE(makeSetProjectKey("Bb major")->apply(p).ok());
    REQUIRE_FALSE(makeSetProjectKey("C dorian")->apply(p).ok());
    REQUIRE_FALSE(makeSetProjectKey("")->apply(p).ok());
    REQUIRE(p == before);
    auto res = makeSetProjectKey("F# minor")->apply(p);
    REQUIRE(res.ok());
    REQUIRE(p.key == "F# minor");
    REQUIRE(projectFromJson(toJson(p)) == p);
    REQUIRE_FALSE(toJson(before).contains("key"));
    REQUIRE(res.inverse->apply(p).ok());
    REQUIRE(p == before);
}

TEST_CASE("automation: a segment can be bent, 0 is straight, out of range is refused and json keeps it", "[automation][curve]") {
    Project project{Uuid::random(gRng)};
    const Track a = track(TrackKind::Audio, "A");
    REQUIRE(makeAddTrack(a)->apply(project).ok());
    std::vector<AutomationPoint> bent{{0, 0.0, 0.5}, {kPPQ * 4, 1.0, 0.0}};  // pan from 0 to 1 over four beats
    REQUIRE(makeSetAutomation(a.id, "pan", bent)->apply(project).ok());
    MediaStore media;
    auto cfg = buildConfig(project, *project.findTrack(a.id), media, nullptr);
    REQUIRE(cfg->panAuto.size() == 2);
    REQUIRE(cfg->panAuto[0].curve == Catch::Approx(0.5f));
    REQUIRE(cfg->panAuto[1].curve == 0.0f);
    REQUIRE_FALSE(makeSetAutomation(a.id, "pan", std::vector<AutomationPoint>{{0, 0.0, 1.5}})->apply(project).ok());
    REQUIRE_FALSE(makeSetAutomation(a.id, "pan", std::vector<AutomationPoint>{{0, 0.0, -2.0}})->apply(project).ok());
    const nlohmann::json j = toJson(project);
    REQUIRE(j["tracks"][1]["automation"][0]["points"][0]["curve"] == 0.5);
    REQUIRE_FALSE(j["tracks"][1]["automation"][0]["points"][1].contains("curve"));  // a straight segment writes nothing
    REQUIRE(projectFromJson(j) == project);
}
