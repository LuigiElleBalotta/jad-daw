#include <catch2/catch_test_macros.hpp>
#include <random>
#include "lpc/commands.h"
#include "lpc/undo_stack.h"

using namespace lpc;

namespace {
const char* kId = "vst3:00112233445566778899aabbccddeeff";

struct Fixture {
    std::mt19937_64 rng{9};
    Project p;
    Uuid trackId;
    Fixture() {
        Track t;
        t.id = Uuid::random(rng);
        t.kind = TrackKind::Audio;
        t.name = "A";
        trackId = t.id;
        REQUIRE(makeAddTrack(t)->apply(p).ok());
        ProcessorRef gain;
        gain.processorId = "builtin.gain";
        REQUIRE(makeAddInsert(trackId, gain)->apply(p).ok());
        ProcessorRef plug;
        plug.processorId = kId;
        plug.state = "AAAA";
        plug.label = "Test";
        REQUIRE(makeAddInsert(trackId, plug)->apply(p).ok());
    }
    const ProcessorRef& insert(int i) const { return p.findTrack(trackId)->strip.inserts[static_cast<std::size_t>(i)]; }
};
}  // namespace

TEST_CASE("set_insert_state: replaces the state and keeps id and label", "[plugin][commands]") {
    Fixture f;
    REQUIRE(makeSetInsertState(f.trackId, 1, "BBBB")->apply(f.p).ok());
    REQUIRE(f.insert(1).state == "BBBB");
    REQUIRE(f.insert(1).processorId == kId);
    REQUIRE(f.insert(1).label == "Test");
}

TEST_CASE("set_insert_state: undo and redo", "[plugin][commands]") {
    Fixture f;
    UndoStack undo;
    REQUIRE_FALSE(undo.execute(f.p, makeSetInsertState(f.trackId, 1, "BBBB")).has_value());
    REQUIRE(f.insert(1).state == "BBBB");
    REQUIRE_FALSE(undo.undo(f.p).has_value());
    REQUIRE(f.insert(1).state == "AAAA");
    REQUIRE_FALSE(undo.redo(f.p).has_value());
    REQUIRE(f.insert(1).state == "BBBB");
}

TEST_CASE("set_insert_state: errors", "[plugin][commands]") {
    Fixture f;
    auto code = [&](CommandPtr c) { return c->apply(f.p).error->code; };
    REQUIRE(code(makeSetInsertState(Uuid::random(f.rng), 1, "AAAA")) == "not_found");
    REQUIRE(code(makeSetInsertState(f.trackId, 5, "AAAA")) == "bad_index");
    REQUIRE(code(makeSetInsertState(f.trackId, -1, "AAAA")) == "bad_index");
    REQUIRE(code(makeSetInsertState(f.trackId, 0, "AAAA")) == "bad_value");   // a built-in has no state
    REQUIRE(code(makeSetInsertState(f.trackId, 1, "***")) == "bad_value");
    REQUIRE(f.insert(1).state == "AAAA");
}

TEST_CASE("set_insert_state: JSON round trip", "[plugin][commands]") {
    Fixture f;
    const nlohmann::json j = makeSetInsertState(f.trackId, 1, "BBBB")->toJson();
    REQUIRE(j.at("type") == "set_insert_state");
    REQUIRE(commandFromJson(j)->apply(f.p).ok());
    REQUIRE(f.insert(1).state == "BBBB");
}
