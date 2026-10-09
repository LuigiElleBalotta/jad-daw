#include <catch2/catch_test_macros.hpp>
#include "lpc/commands.h"
#include "lpc/undo_stack.h"

using namespace lpc;

namespace {
Track audioTrack(std::mt19937_64& rng, const char* name) {
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Audio;
    t.name = name;
    return t;
}
}  // namespace

TEST_CASE("undo stack: execute, undo, redo", "[undo]") {
    std::mt19937_64 rng(5);
    Project p(Uuid::random(rng));
    const Project initial = p;
    UndoStack s;
    REQUIRE_FALSE(s.canUndo());
    REQUIRE_FALSE(s.canRedo());

    REQUIRE_FALSE(s.execute(p, makeAddTrack(audioTrack(rng, "A"))).has_value());
    REQUIRE_FALSE(s.execute(p, makeAddTrack(audioTrack(rng, "B"))).has_value());
    REQUIRE(p.tracks.size() == 3);
    const Project afterTwo = p;

    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p.tracks.size() == 2);
    REQUIRE(s.canRedo());
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p == initial);
    REQUIRE_FALSE(s.canUndo());

    REQUIRE_FALSE(s.redo(p).has_value());
    REQUIRE_FALSE(s.redo(p).has_value());
    REQUIRE(p == afterTwo);
}

TEST_CASE("undo stack: a new command clears the redo history", "[undo]") {
    std::mt19937_64 rng(6);
    Project p(Uuid::random(rng));
    UndoStack s;
    REQUIRE_FALSE(s.execute(p, makeAddTrack(audioTrack(rng, "A"))).has_value());
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(s.canRedo());
    REQUIRE_FALSE(s.execute(p, makeAddTrack(audioTrack(rng, "B"))).has_value());
    REQUIRE_FALSE(s.canRedo());
}

TEST_CASE("undo stack: a rejected command changes nothing and is not recorded", "[undo]") {
    std::mt19937_64 rng(7);
    Project p(Uuid::random(rng));
    UndoStack s;
    const Project before = p;
    const auto err = s.execute(p, makeRemoveTrack(Uuid::random(rng)));
    REQUIRE(err.has_value());
    REQUIRE(err->code == "not_found");
    REQUIRE(p == before);
    REQUIRE_FALSE(s.canUndo());
}

TEST_CASE("undo stack: undo and redo on empty history report an error", "[undo]") {
    Project p;
    UndoStack s;
    REQUIRE(s.undo(p)->code == "nothing_to_undo");
    REQUIRE(s.redo(p)->code == "nothing_to_redo");
}

TEST_CASE("undo stack: coalesceFrom turns many steps on one parameter into one step", "[undo]") {
    std::mt19937_64 rng(9);
    Project p(Uuid::random(rng));
    UndoStack s;
    Track t = audioTrack(rng, "A");
    const Uuid id = t.id;
    REQUIRE_FALSE(s.execute(p, makeAddTrack(t)).has_value());
    const std::size_t mark = s.size();
    for (float db : {-1.0f, -2.0f, -3.0f, -4.0f}) {
        StripPatch patch;
        patch.gainDb = db;
        REQUIRE_FALSE(s.execute(p, makeSetStrip(id, patch)).has_value());
    }
    s.coalesceFrom(mark);
    REQUIRE(s.size() == mark + 1);
    REQUIRE(p.findTrack(id)->strip.gainDb == -4.0f);
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p.findTrack(id)->strip.gainDb == 0.0f);  // back to what it was before the drag
    REQUIRE_FALSE(s.redo(p).has_value());
    REQUIRE(p.findTrack(id)->strip.gainDb == -4.0f);
    s.coalesceFrom(s.size());  // nothing to merge
    s.coalesceFrom(s.size() + 5);
    REQUIRE(s.size() == mark + 1);
}
