#include <catch2/catch_test_macros.hpp>
#include <random>
#include "lpc/commands.h"
#include "lpc/model_json.h"
#include "lpc/undo_stack.h"

using namespace lpc;

namespace {
const char* kPlug = "vst3:00112233445566778899aabbccddeeff";

struct Fixture {
    std::mt19937_64 rng{31};
    Project p;
    Uuid a, b;

    Uuid addTrack(TrackKind kind, const char* name) {
        Track t;
        t.id = Uuid::random(rng);
        t.kind = kind;
        t.name = name;
        REQUIRE(makeAddTrack(t)->apply(p).ok());
        return t.id;
    }
    void addGain(const Uuid& track, double db) {
        ProcessorRef r;
        r.processorId = "builtin.gain";
        r.params["gainDb"] = db;
        REQUIRE(makeAddInsert(track, r)->apply(p).ok());
    }
    void addPlugin(const Uuid& track, const char* state, bool bypass) {
        ProcessorRef r;
        r.processorId = kPlug;
        r.state = state;
        r.label = "Test";
        r.bypass = bypass;
        REQUIRE(makeAddInsert(track, r)->apply(p).ok());
    }
    Fixture() {
        a = addTrack(TrackKind::Audio, "A");
        b = addTrack(TrackKind::Audio, "B");
        addGain(a, 1);
        addGain(a, 2);
        addGain(a, 3);
        addGain(b, 10);
    }
    std::vector<double> gains(const Uuid& t) const {
        std::vector<double> out;
        for (const ProcessorRef& r : p.findTrack(t)->strip.inserts) out.push_back(r.params.count("gainDb") ? r.params.at("gainDb") : -1);
        return out;
    }
};
}  // namespace

TEST_CASE("move_insert: reorders inside a track", "[commands][insert][move]") {
    Fixture f;
    REQUIRE(makeMoveInsert(f.a, 0, 2)->apply(f.p).ok());
    REQUIRE(f.gains(f.a) == std::vector<double>{2, 3, 1});
    REQUIRE(makeMoveInsert(f.a, 2, 0)->apply(f.p).ok());
    REQUIRE(f.gains(f.a) == std::vector<double>{1, 2, 3});
}

TEST_CASE("move_insert: the same index is accepted and changes nothing", "[commands][insert][move]") {
    Fixture f;
    const Project before = f.p;
    REQUIRE(makeMoveInsert(f.a, 1, 1)->apply(f.p).ok());
    REQUIRE(f.p == before);
}

TEST_CASE("move_insert: bad index, unknown track and bad target are rejected and change nothing", "[commands][insert][move]") {
    Fixture f;
    const Project before = f.p;
    auto bad = [&](CommandPtr c, const char* code) {
        auto r = c->apply(f.p);
        REQUIRE_FALSE(r.ok());
        REQUIRE(r.error->code == code);
        REQUIRE(f.p == before);
    };
    bad(makeMoveInsert(f.a, -1, 0), "bad_index");
    bad(makeMoveInsert(f.a, 3, 0), "bad_index");
    bad(makeMoveInsert(f.a, 0, 3), "bad_index");  // same track: to is 0..n-1
    bad(makeMoveInsert(f.a, 0, -1), "bad_index");
    bad(makeMoveInsert(Uuid{7, 7}, 0, 0), "not_found");
    bad(makeMoveInsert(f.a, 0, 0, Uuid{7, 7}), "not_found");
    bad(makeMoveInsert(f.a, 0, 0, f.p.master()->id), "bad_target");
    bad(makeMoveInsert(f.a, 0, 2, f.b), "bad_index");  // another track: to is 0..m, here m = 1
}

TEST_CASE("move_insert: to another track at the start, middle and end", "[commands][insert][move]") {
    for (int to : {0, 1}) {
        Fixture f;
        REQUIRE(makeMoveInsert(f.a, 1, to, f.b)->apply(f.p).ok());
        REQUIRE(f.gains(f.a) == std::vector<double>{1, 3});
        std::vector<double> expected{10};
        expected.insert(expected.begin() + to, 2.0);
        REQUIRE(f.gains(f.b) == expected);
    }
}

TEST_CASE("move_insert: a plug-in travels whole with its state, label and bypass", "[commands][insert][move][plugin]") {
    Fixture f;
    f.addPlugin(f.a, "AAAA", true);
    const ProcessorRef moved = f.p.findTrack(f.a)->strip.inserts.back();
    REQUIRE(makeMoveInsert(f.a, 3, 1, f.b)->apply(f.p).ok());
    REQUIRE(f.p.findTrack(f.b)->strip.inserts[1] == moved);
    REQUIRE(f.p.findTrack(f.b)->strip.inserts[1].bypass);
}

TEST_CASE("move_insert: the inverse restores both tracks exactly", "[commands][insert][move]") {
    Fixture f;
    f.addPlugin(f.a, "AAAA", false);
    UndoStack undo;
    const Project before = f.p;
    REQUIRE_FALSE(undo.execute(f.p, makeMoveInsert(f.a, 3, 0, f.b)).has_value());
    REQUIRE_FALSE(f.p == before);
    REQUIRE_FALSE(undo.undo(f.p).has_value());
    REQUIRE(f.p == before);
    REQUIRE_FALSE(undo.redo(f.p).has_value());
    REQUIRE(f.p.findTrack(f.b)->strip.inserts.front().processorId == kPlug);
    REQUIRE_FALSE(undo.undo(f.p).has_value());
    REQUIRE_FALSE(undo.execute(f.p, makeMoveInsert(f.a, 0, 2)).has_value());  // same track, then undo
    REQUIRE_FALSE(undo.undo(f.p).has_value());
    REQUIRE(f.p == before);
}

TEST_CASE("move_insert: JSON round trip with and without toTrackId", "[commands][insert][move][json]") {
    Fixture f;
    const auto same = makeMoveInsert(f.a, 0, 2)->toJson();
    REQUIRE(commandFromJson(same)->toJson() == same);
    const auto cross = makeMoveInsert(f.a, 0, 1, f.b)->toJson();
    REQUIRE(cross.at("toTrackId") == f.b.toString());
    REQUIRE(commandFromJson(cross)->toJson() == cross);
    REQUIRE(commandFromJson(cross)->apply(f.p).ok());
    REQUIRE(f.gains(f.b) == std::vector<double>{10, 1});
}
