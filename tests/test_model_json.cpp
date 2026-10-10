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
    media.recordedAt = 1500000;  // a take
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

TEST_CASE("model json: recordedAt is written only for a take and defaults to -1", "[model][json]") {
    MediaItem item{Uuid::random(), "audio/a.wav", "h", 48000, 1, 10};
    nlohmann::json j = item;
    REQUIRE_FALSE(j.contains("recordedAt"));
    REQUIRE(j.get<MediaItem>().recordedAt == -1);
    item.recordedAt = 42;
    j = item;
    REQUIRE(j["recordedAt"] == 42);
    REQUIRE(j.get<MediaItem>() == item);
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

TEST_CASE("model json: every optional region and track field survives a save and a load and is absent when it has its default", "[model][json]") {
    std::mt19937_64 rng(77);
    Project p(Uuid::random(rng));
    MediaItem media{Uuid::random(rng), "audio/frozen.wav", "h", 48000, 2, 48000};
    p.mediaPool.push_back(media);
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Instrument;
    t.name = "Keys";
    ProcessorRef inst;
    inst.processorId = "builtin.sine";
    t.instrument = inst;
    t.midi = MidiShaping{3, -4, 24, 96, 10, 120};
    t.delayMs = -12.5;
    t.freeze = Freeze{media.id, 1234};
    Region r;
    r.id = Uuid::random(rng);
    r.timeBase = TimeBase::Musical;
    r.start = 0;
    r.length = 4 * kPPQ;
    r.muted = true;
    r.takeGroup = "group-1";
    r.transpose = -2;
    r.velocityOffset = 7;
    r.quantize = kPPQ / 4;
    r.loopLength = kPPQ;
    r.notes.push_back(MidiNote{0, kPPQ / 2, 60, 100, true});
    r.controls = {MidiControl{0, 0xB0, 64, 127}, MidiControl{kPPQ, 0xE0, 5, 70}, MidiControl{2 * kPPQ, 0xD0, 33, 0}};
    t.regions.push_back(r);
    p.tracks.push_back(t);

    const Project back = projectFromJson(toJson(p));
    const Track* bt = back.findTrack(t.id);
    REQUIRE(bt);
    REQUIRE(*bt == t);                                              // everything, the frozen audio and the shaping included
    REQUIRE(bt->regions[0] == r);

    Project plain(Uuid::random(rng));
    Track a;
    a.id = Uuid::random(rng);
    a.kind = TrackKind::Instrument;
    a.name = "Plain";
    a.instrument = inst;
    Region pr;
    pr.id = Uuid::random(rng);
    pr.timeBase = TimeBase::Musical;
    pr.length = kPPQ;
    a.regions.push_back(pr);
    plain.tracks.push_back(a);
    const nlohmann::json j = toJson(plain);
    for (const auto& tj : j.at("tracks")) {
        if (tj.at("name") != "Plain") continue;
        for (const char* key : {"midi", "delayMs", "freeze"}) REQUIRE_FALSE(tj.contains(key));              // a plain track is written as before
        for (const char* key : {"muted", "takeGroup", "transpose", "velocityOffset", "quantize", "loopLength", "controls"})
            REQUIRE_FALSE(tj.at("regions")[0].contains(key));
    }
}
