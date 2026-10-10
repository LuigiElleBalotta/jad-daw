#include <catch2/catch_test_macros.hpp>
#include <random>
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "lpc/model_json.h"
#include "lpc/undo_stack.h"

using namespace lpc;

TEST_CASE("markers: set_markers sorts by position, undoes and redoes", "[markers]") {
    std::mt19937_64 rng(3);
    Project p(Uuid::random(rng));
    UndoStack s;
    Marker a{Uuid::random(rng), 4 * kPPQ, "Chorus"};
    Marker b{Uuid::random(rng), 0, "Intro"};
    REQUIRE_FALSE(s.execute(p, makeSetMarkers({a, b})).has_value());
    REQUIRE(p.markers.size() == 2);
    REQUIRE(p.markers[0].name == "Intro");  // sorted by tick
    REQUIRE(p.markers[1].name == "Chorus");
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p.markers.empty());
    REQUIRE_FALSE(s.redo(p).has_value());
    REQUIRE(p.markers.size() == 2);
}

TEST_CASE("markers: bad lists are refused and leave the project alone", "[markers]") {
    std::mt19937_64 rng(4);
    Project p(Uuid::random(rng));
    UndoStack s;
    Marker a{Uuid::random(rng), 0, "A"};
    REQUIRE(s.execute(p, makeSetMarkers({a, a})).has_value());                       // duplicate ids
    REQUIRE(s.execute(p, makeSetMarkers({Marker{Uuid::random(rng), -1, "x"}})).has_value());  // negative position
    REQUIRE(s.execute(p, makeSetMarkers({Marker{Uuid::random(rng), 0, std::string(201, 'x')}})).has_value());
    REQUIRE(p.markers.empty());
}

TEST_CASE("markers: the command round-trips through JSON", "[markers]") {
    std::mt19937_64 rng(5);
    Marker a{Uuid::random(rng), 2 * kPPQ, "Verse"};
    const nlohmann::json j = makeSetMarkers({a})->toJson();
    REQUIRE(j.at("type") == "set_markers");
    const CommandPtr again = commandFromJson(j);
    REQUIRE(again);
    REQUIRE(again->toJson() == j);
}

TEST_CASE("automation: set_automation creates, replaces and removes a lane with undo", "[automation]") {
    std::mt19937_64 rng(6);
    Project p(Uuid::random(rng));
    UndoStack s;
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Audio;
    t.name = "A";
    REQUIRE_FALSE(s.execute(p, makeAddTrack(t)).has_value());
    REQUIRE_FALSE(s.execute(p, makeSetAutomation(t.id, "volume", {{4 * kPPQ, -6.0}, {0, 0.0}})).has_value());
    REQUIRE(p.findTrack(t.id)->automation.size() == 1);
    REQUIRE(p.findTrack(t.id)->automation[0].points[0].tick == 0);  // sorted
    REQUIRE_FALSE(s.execute(p, makeSetAutomation(t.id, "volume", {{0, -3.0}})).has_value());
    REQUIRE(p.findTrack(t.id)->automation[0].points.size() == 1);
    REQUIRE_FALSE(s.execute(p, makeSetAutomation(t.id, "volume", {})).has_value());
    REQUIRE(p.findTrack(t.id)->automation.empty());
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p.findTrack(t.id)->automation[0].points[0].value == -3.0);
    REQUIRE(s.execute(p, makeSetAutomation(t.id, "cutoff", {{0, 0.0}})).has_value());   // unknown target
    REQUIRE(s.execute(p, makeSetAutomation(t.id, "pan", {{0, 2.0}})).has_value());      // out of range
    REQUIRE(s.execute(p, makeSetAutomation(Uuid::random(rng), "pan", {{0, 0.0}})).has_value());  // no such track
}

TEST_CASE("strip input: set_strip carries the recording input, with undo and JSON", "[strip][input]") {
    std::mt19937_64 rng(8);
    Project p(Uuid::random(rng));
    UndoStack s;
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Audio;
    t.name = "A";
    REQUIRE_FALSE(s.execute(p, makeAddTrack(t)).has_value());
    StripPatch patch;
    patch.input = 3;
    REQUIRE_FALSE(s.execute(p, makeSetStrip(t.id, patch)).has_value());
    REQUIRE(p.findTrack(t.id)->strip.input == 3);
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p.findTrack(t.id)->strip.input == 0);
    patch.input = 65;
    REQUIRE(s.execute(p, makeSetStrip(t.id, patch)).has_value());   // out of range
    nlohmann::json plain = p.findTrack(t.id)->strip;
    REQUIRE_FALSE(plain.contains("input"));                          // 0 is not written
    Strip with;
    with.input = 2;
    nlohmann::json j = with;
    REQUIRE(j.at("input") == 2);
    REQUIRE(j.get<Strip>().input == 2);
    const CommandPtr again = commandFromJson(makeSetStrip(t.id, StripPatch{.input = 4})->toJson());
    REQUIRE(again);
}

TEST_CASE("fades: set_region_fades is validated, undone, kept through JSON and split between the parts", "[fade]") {
    std::mt19937_64 rng(12);
    Project p(Uuid::random(rng));
    UndoStack s;
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Audio;
    t.name = "A";
    REQUIRE_FALSE(s.execute(p, makeAddTrack(t)).has_value());
    MediaItem item{Uuid::random(rng), "audio/a.wav", "h", 48000, 2, 480000};
    REQUIRE_FALSE(s.execute(p, makeAddMedia(item)).has_value());
    Region r;
    r.id = Uuid::random(rng);
    r.timeBase = TimeBase::Musical;
    r.start = 0;
    r.length = 8 * kPPQ;
    r.mediaId = item.id;
    REQUIRE_FALSE(s.execute(p, makeAddRegion(t.id, r)).has_value());
    REQUIRE_FALSE(s.execute(p, makeSetRegionFades(r.id, kPPQ, 2 * kPPQ)).has_value());
    REQUIRE(p.findTrack(t.id)->regions[0].fadeIn == kPPQ);
    REQUIRE(s.execute(p, makeSetRegionFades(r.id, -1, 0)).has_value());               // negative
    REQUIRE(s.execute(p, makeSetRegionFades(r.id, 9 * kPPQ, 0)).has_value());         // longer than the region
    nlohmann::json j = p.findTrack(t.id)->regions[0];
    REQUIRE(j.at("fadeIn") == kPPQ);
    REQUIRE(j.get<Region>().fadeOut == 2 * kPPQ);
    const Uuid right = Uuid::random(rng);
    REQUIRE_FALSE(s.execute(p, makeSplitRegion(r.id, 4 * kPPQ, right)).has_value());
    REQUIRE(p.findTrack(t.id)->regions[0].fadeIn == kPPQ);                            // the left part keeps the fade-in
    REQUIRE(p.findTrack(t.id)->regions[0].fadeOut == 0);                              // and loses the fade-out
    REQUIRE(p.findTrack(t.id)->regions[1].fadeIn == 0);
    REQUIRE(p.findTrack(t.id)->regions[1].fadeOut == 2 * kPPQ);
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p.findTrack(t.id)->regions.size() == 1);
    REQUIRE(p.findTrack(t.id)->regions[0].fadeOut == 2 * kPPQ);                       // the split is undone with its fades
    nlohmann::json plain = Region{};
    REQUIRE_FALSE(plain.contains("fadeIn"));
}

TEST_CASE("groups: set_groups is validated, undone and kept through JSON; removing a track takes it out of its group", "[groups]") {
    std::mt19937_64 rng(13);
    Project p(Uuid::random(rng));
    UndoStack s;
    std::vector<Uuid> ids;
    for (int i = 0; i < 3; ++i) {
        Track t;
        t.id = Uuid::random(rng);
        t.kind = TrackKind::Audio;
        t.name = "T" + std::to_string(i);
        ids.push_back(t.id);
        REQUIRE_FALSE(s.execute(p, makeAddTrack(t)).has_value());
    }
    Group g;
    g.id = Uuid::random(rng);
    g.name = "Drums";
    g.members = {ids[0], ids[1]};
    REQUIRE_FALSE(s.execute(p, makeSetGroups({g})).has_value());
    REQUIRE(p.groups.size() == 1);
    Group other = g;
    other.id = Uuid::random(rng);
    other.name = "Again";
    REQUIRE(s.execute(p, makeSetGroups({g, other})).has_value());                       // a track in two groups
    Group bad = g;
    bad.members = {p.tracks[0].id};                                                       // the master
    REQUIRE(s.execute(p, makeSetGroups({bad})).has_value());
    Group unnamed = g;
    unnamed.name = "";
    REQUIRE(s.execute(p, makeSetGroups({unnamed})).has_value());
    const nlohmann::json doc = toJson(p);
    REQUIRE(doc.contains("groups"));
    REQUIRE(projectFromJson(doc).groups.size() == 1);
    Project none(Uuid::random(rng));
    REQUIRE_FALSE(toJson(none).contains("groups"));                                      // no groups: not written
    REQUIRE_FALSE(s.execute(p, makeRemoveTrack(ids[0])).has_value());
    REQUIRE(p.groups[0].members == std::vector<Uuid>{ids[1]});                           // it left the group
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p.groups[0].members.size() == 2);                                            // and is back with its place in the group
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p.groups.empty());
}

TEST_CASE("automation modes: set_track_props carries the mode, the JSON omits the default, off leaves the lanes out of the engine", "[automation][mode]") {
    std::mt19937_64 rng(14);
    Project p(Uuid::random(rng));
    UndoStack s;
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Audio;
    t.name = "A";
    REQUIRE_FALSE(s.execute(p, makeAddTrack(t)).has_value());
    REQUIRE_FALSE(s.execute(p, makeSetAutomation(t.id, "volume", {{0, -6.0}})).has_value());
    TrackPatch patch;
    patch.automationMode = "touch";
    REQUIRE_FALSE(s.execute(p, makeSetTrackProps(t.id, patch)).has_value());
    REQUIRE(p.findTrack(t.id)->automationMode == "touch");
    nlohmann::json j = p.findTrack(t.id)->automationMode == "touch" ? nlohmann::json(*p.findTrack(t.id)) : nlohmann::json();
    REQUIRE(j.at("automationMode") == "touch");
    REQUIRE(j.get<Track>().automationMode == "touch");
    patch.automationMode = "sideways";
    REQUIRE(s.execute(p, makeSetTrackProps(t.id, patch)).has_value());                  // not a mode
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p.findTrack(t.id)->automationMode == "read");
    nlohmann::json plain = *p.findTrack(t.id);
    REQUIRE_FALSE(plain.contains("automationMode"));
    MediaStore media;
    auto withLane = buildConfig(p, *p.findTrack(t.id), media, nullptr, nullptr);
    REQUIRE(withLane->volumeAuto.size() == 1);                                           // read: the lane plays
    patch.automationMode = "off";
    REQUIRE_FALSE(s.execute(p, makeSetTrackProps(t.id, patch)).has_value());
    auto withoutLane = buildConfig(p, *p.findTrack(t.id), media, nullptr, nullptr);
    REQUIRE(withoutLane->volumeAuto.empty());                                            // off: ignored
}

TEST_CASE("project name: set_project_name is validated and undone", "[project]") {
    std::mt19937_64 rng(15);
    Project p(Uuid::random(rng));
    UndoStack s;
    p.name = "Old";
    REQUIRE_FALSE(s.execute(p, makeSetProjectName("New name")).has_value());
    REQUIRE(p.name == "New name");
    REQUIRE(s.execute(p, makeSetProjectName("")).has_value());
    REQUIRE(s.execute(p, makeSetProjectName("bad\nname")).has_value());
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p.name == "Old");
}

TEST_CASE("undo history: the steps are listed in words, oldest first, and the redo steps next first", "[undo][history]") {
    std::mt19937_64 rng(16);
    Project p(Uuid::random(rng));
    UndoStack s;
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Audio;
    t.name = "A";
    REQUIRE_FALSE(s.execute(p, makeAddTrack(t)).has_value());
    StripPatch patch;
    patch.gainDb = -3.0f;
    REQUIRE_FALSE(s.execute(p, makeSetStrip(t.id, patch)).has_value());
    REQUIRE_FALSE(s.execute(p, makeSetProjectName("X")).has_value());
    std::vector<CommandPtr> two;
    two.push_back(makeSetProjectName("Y"));
    two.push_back(makeSetProjectName("Z"));
    REQUIRE_FALSE(s.execute(p, makeTransaction(std::move(two))).has_value());
    const auto labels = s.undoLabels();
    REQUIRE(labels == std::vector<std::string>({"Add track", "Channel strip", "Project name", "Project name (2)"}));
    REQUIRE(s.redoLabels().empty());
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(s.undoLabels().size() == 2);
    REQUIRE(s.redoLabels() == std::vector<std::string>({"Project name", "Project name (2)"}));   // the next redo first
}

TEST_CASE("track order: set_track_order reorders, keeps the master first and is undone", "[project][order]") {
    std::mt19937_64 rng(16);
    Project p(Uuid::random(rng));
    UndoStack s;
    std::vector<Uuid> ids;
    for (const char* name : {"A", "B", "C"}) {
        Track t;
        t.id = Uuid::random(rng);
        t.kind = TrackKind::Audio;
        t.name = name;
        ids.push_back(t.id);
        REQUIRE_FALSE(s.execute(p, makeAddTrack(t)).has_value());
    }
    REQUIRE_FALSE(s.execute(p, makeSetTrackOrder({ids[2], ids[0], ids[1]})).has_value());
    REQUIRE(p.tracks.front().kind == TrackKind::Master);
    REQUIRE(p.tracks[1].id == ids[2]);
    REQUIRE(p.tracks[3].id == ids[1]);
    REQUIRE(s.execute(p, makeSetTrackOrder({ids[0], ids[1]})).has_value());          // a track missing
    REQUIRE(s.execute(p, makeSetTrackOrder({ids[0], ids[0], ids[1]})).has_value());  // twice
    REQUIRE(s.execute(p, makeSetTrackOrder({ids[0], ids[1], p.master()->id})).has_value());  // the master
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p.tracks[1].id == ids[0]);
    REQUIRE(p.tracks[3].id == ids[2]);
}

TEST_CASE("automation: a send can be automated, a missing send is refused", "[automation][sends]") {
    std::mt19937_64 rng(31);
    Project p(Uuid::random(rng));
    UndoStack s;
    Track bus;
    bus.id = Uuid::random(rng);
    bus.kind = TrackKind::Bus;
    bus.name = "Bus";
    REQUIRE_FALSE(s.execute(p, makeAddTrack(bus)).has_value());
    Track audio;
    audio.id = Uuid::random(rng);
    audio.kind = TrackKind::Audio;
    audio.name = "A";
    REQUIRE_FALSE(s.execute(p, makeAddTrack(audio)).has_value());
    Send send;
    send.id = Uuid::random(rng);
    send.targetTrackId = bus.id;
    REQUIRE_FALSE(s.execute(p, makeAddSend(audio.id, send)).has_value());
    const std::string target = "send:" + send.id.toString();
    REQUIRE_FALSE(s.execute(p, makeSetAutomation(audio.id, target, {AutomationPoint{0, -6.0}, AutomationPoint{960, 0.0}})).has_value());
    REQUIRE(p.findTrack(audio.id)->automation.size() == 1);
    REQUIRE(s.execute(p, makeSetAutomation(audio.id, target, {AutomationPoint{0, 20.0}})).has_value());                     // above +12 dB
    REQUIRE(s.execute(p, makeSetAutomation(audio.id, "send:" + Uuid::random(rng).toString(), {AutomationPoint{0, 0.0}})).has_value());  // no such send
    REQUIRE(s.execute(p, makeSetAutomation(audio.id, "send:nonsense", {AutomationPoint{0, 0.0}})).has_value());
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p.findTrack(audio.id)->automation.empty());
}
