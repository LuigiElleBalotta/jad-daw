#include <catch2/catch_test_macros.hpp>
#include "lpc/model.h"
#include "lpc/model_json.h"

using namespace lpc;

namespace {

Project makeRichProject() {
    std::mt19937_64 rng(1);
    Project p(Uuid::random(rng));
    p.name = "Rich";
    p.sampleRate = 44100;
    p.tempoMap.setTempo(4 * kPPQ, 90.0);
    p.tempoMap.setSignature(8 * kPPQ, 3, 4);
    p.markers.push_back({Uuid::random(rng), 2 * kPPQ, "Verse"});

    MediaItem media{Uuid::random(rng), "audio/tone.wav", "abc123", 44100, 2, 88200};
    p.mediaPool.push_back(media);

    Track bus;
    bus.id = Uuid::random(rng);
    bus.kind = TrackKind::Bus;
    bus.name = "Bus 1";
    p.tracks.push_back(bus);

    Track audio;
    audio.id = Uuid::random(rng);
    audio.kind = TrackKind::Audio;
    audio.name = "Audio 1";
    audio.color = "teal";
    audio.strip.gainDb = -6.0f;
    audio.strip.pan = -0.25f;
    audio.strip.solo = true;
    audio.strip.output = bus.id;
    audio.strip.inserts.push_back({"builtin.gain", {{"gainDb", 3.0}}, ""});
    audio.strip.sends.push_back({Uuid::random(rng), bus.id, -12.0f, true});
    Region r;
    r.id = Uuid::random(rng);
    r.timeBase = TimeBase::Absolute;
    r.start = 500000;
    r.length = 1500000;
    r.mediaId = media.id;
    r.sourceOffsetFrames = 100;
    r.gainDb = -1.5f;
    audio.regions.push_back(r);
    audio.automation.push_back({Uuid::random(rng), "strip.gainDb", {{0, 0.0}, {kPPQ, -6.0}}});
    p.tracks.push_back(audio);

    Track inst;
    inst.id = Uuid::random(rng);
    inst.kind = TrackKind::Instrument;
    inst.name = "Keys";
    inst.instrument = ProcessorRef{"builtin.sine", {}, ""};
    Region m;
    m.id = Uuid::random(rng);
    m.start = 0;
    m.length = 4 * kPPQ;
    m.notes.push_back({0, kPPQ, 60, 100});
    m.notes.push_back({kPPQ, kPPQ, 64, 90});
    inst.regions.push_back(m);
    p.tracks.push_back(inst);
    return p;
}

}  // namespace

TEST_CASE("model: new project has exactly one master track", "[model]") {
    const Project p;
    REQUIRE(p.tracks.size() == 1);
    REQUIRE(p.master() != nullptr);
    REQUIRE(p.master()->kind == TrackKind::Master);
}

TEST_CASE("model: lookups", "[model]") {
    const Project p = makeRichProject();
    const Track& audio = p.tracks[2];
    REQUIRE(p.findTrack(audio.id) == &audio);
    REQUIRE(p.findTrack(Uuid{}) == nullptr);
    std::size_t idx = 99;
    const Track* owner = const_cast<Project&>(p).findTrackOfRegion(audio.regions[0].id, &idx);
    REQUIRE(owner == &audio);
    REQUIRE(idx == 0);
    REQUIRE(p.findMedia(p.mediaPool[0].id) == &p.mediaPool[0]);
}

TEST_CASE("model json: round trip keeps every field", "[model][json]") {
    const Project p = makeRichProject();
    const nlohmann::json j = toJson(p);
    REQUIRE(projectFromJson(j) == p);
    // and through text, as it would be on disk
    REQUIRE(projectFromJson(nlohmann::json::parse(j.dump())) == p);
}

TEST_CASE("model json: ids are uuid strings and enums are readable", "[model][json]") {
    const Project p = makeRichProject();
    const nlohmann::json j = toJson(p);
    REQUIRE(j["tracks"][2]["kind"] == "audio");
    REQUIRE(j["tracks"][2]["id"].get<std::string>().size() == 36);
    REQUIRE(j["tracks"][2]["regions"][0]["timeBase"] == "absolute");
}

TEST_CASE("model json: invalid documents throw instead of producing a project", "[model][json]") {
    nlohmann::json j = toJson(makeRichProject());

    nlohmann::json badKind = j;
    badKind["tracks"][1]["kind"] = "banana";
    REQUIRE_THROWS(projectFromJson(badKind));

    nlohmann::json badId = j;
    badId["tracks"][1]["id"] = "not-a-uuid";
    REQUIRE_THROWS(projectFromJson(badId));

    nlohmann::json missing = j;
    missing.erase("tempoMap");
    REQUIRE_THROWS(projectFromJson(missing));

    nlohmann::json noMaster = j;
    noMaster["tracks"].erase(noMaster["tracks"].begin());  // master is tracks[0]
    REQUIRE_THROWS(projectFromJson(noMaster));

    REQUIRE_THROWS(projectFromJson(nlohmann::json::array()));
}

TEST_CASE("model json: showInTracks is written only when false and read back", "[model][json][visibility]") {
    Project p;
    Track bus;
    bus.id = Uuid{9, 9};
    bus.kind = TrackKind::Bus;
    bus.name = "Bus";
    p.tracks.push_back(bus);
    REQUIRE_FALSE(toJson(p).at("tracks").at(1).contains("showInTracks"));
    p.tracks[1].showInTracks = false;
    const auto j = toJson(p);
    REQUIRE(j.at("tracks").at(1).at("showInTracks") == false);
    REQUIRE(projectFromJson(j) == p);
}

TEST_CASE("model json: showInTracks must be a boolean", "[model][json][visibility]") {
    Project p;
    Track bus;
    bus.id = Uuid{9, 9};
    bus.kind = TrackKind::Bus;
    bus.name = "Bus";
    p.tracks.push_back(bus);
    auto j = toJson(p);
    j["tracks"][1]["showInTracks"] = "no";
    REQUIRE_THROWS_AS(projectFromJson(j), std::runtime_error);
    j["tracks"][1]["showInTracks"] = false;
    REQUIRE_FALSE(projectFromJson(j).tracks[1].showInTracks);
    j["tracks"][1]["kind"] = "audio";   // any track can be hidden now
    REQUIRE_FALSE(projectFromJson(j).tracks[1].showInTracks);
}
