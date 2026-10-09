#include <catch2/catch_test_macros.hpp>
#include <random>
#include "lpc/commands.h"
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
