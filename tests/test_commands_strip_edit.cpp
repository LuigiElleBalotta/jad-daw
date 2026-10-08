#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <optional>
#include <random>

#include "lpc/commands.h"
#include "lpc/model_json.h"
#include "lpc/processor_ids.h"
#include "lpc/validation.h"

using namespace lpc;

namespace {
std::mt19937_64 gRng(9001);

Track track(TrackKind kind, const char* name) {
    Track t;
    t.id = Uuid::random(gRng);
    t.kind = kind;
    t.name = name;
    if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{kProcSine, {}, ""};
    return t;
}

Project projectWith(std::initializer_list<Track> tracks) {
    Project p{Uuid::random(gRng)};
    for (const Track& t : tracks) REQUIRE(makeAddTrack(t)->apply(p).ok());
    return p;
}

void requireRejected(Project& p, CommandPtr c, const char* code) {
    const Project before = p;
    auto r = c->apply(p);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error->code == code);
    REQUIRE(p == before);
}

void requireRoundTrip(const CommandPtr& c) { REQUIRE(commandFromJson(c->toJson())->toJson() == c->toJson()); }

void requireUndo(Project& p, CommandPtr c) {
    const Project before = p;
    auto r = c->apply(p);
    REQUIRE(r.ok());
    REQUIRE_FALSE(p == before);
    REQUIRE(r.inverse->apply(p).ok());
    REQUIRE(p == before);
}
}  // namespace

TEST_CASE("set_patch_id: sets, clears, undo restores", "[commands][patch]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    requireUndo(p, makeSetPatchId(a.id, "audio.clean-vocal"));
    REQUIRE(makeSetPatchId(a.id, "audio.clean-vocal")->apply(p).ok());
    REQUIRE(p.findTrack(a.id)->patchId == "audio.clean-vocal");
    requireUndo(p, makeSetPatchId(a.id, ""));
    requireRoundTrip(makeSetPatchId(a.id, "x.y-1"));
}

TEST_CASE("set_patch_id: bad ids, master and unknown tracks are rejected", "[commands][patch]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    requireRejected(p, makeSetPatchId(a.id, "has space"), "bad_value");
    requireRejected(p, makeSetPatchId(a.id, std::string(65, 'x')), "bad_value");
    requireRejected(p, makeSetPatchId(a.id, "tab\t"), "bad_value");
    requireRejected(p, makeSetPatchId(p.master()->id, "audio.x"), "invalid_kind");
    requireRejected(p, makeSetPatchId(Uuid::random(gRng), "audio.x"), "not_found");
}

TEST_CASE("a project keeps its patch ids through JSON and loads old files without them", "[commands][patch]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    REQUIRE(makeSetPatchId(a.id, "audio.warm")->apply(p).ok());
    nlohmann::json j = toJson(p);
    REQUIRE(projectFromJson(j) == p);
    for (auto& t : j["tracks"]) t.erase("patchId");  // a file written before patch ids existed
    REQUIRE(projectFromJson(j).findTrack(a.id)->patchId.empty());
    j["tracks"][1]["patchId"] = "has space";
    REQUIRE_THROWS(projectFromJson(j));
}

TEST_CASE("set_instrument: swaps the instrument of an instrument track, undo restores", "[commands][instrument]") {
    const Track k = track(TrackKind::Instrument, "Keys");
    Project p = projectWith({k});
    ProcessorRef other{kProcSine, {}, "state"};
    requireUndo(p, makeSetInstrument(k.id, other));
    requireRoundTrip(makeSetInstrument(k.id, other));
}

TEST_CASE("set_instrument: only instrument tracks, only known instruments", "[commands][instrument]") {
    const Track a = track(TrackKind::Audio, "Audio");
    const Track k = track(TrackKind::Instrument, "Keys");
    Project p = projectWith({a, k});
    requireRejected(p, makeSetInstrument(a.id, ProcessorRef{kProcSine, {}, ""}), "invalid_kind");
    requireRejected(p, makeSetInstrument(k.id, ProcessorRef{"vendor.unknown", {}, ""}), "bad_value");
    requireRejected(p, makeSetInstrument(Uuid::random(gRng), ProcessorRef{kProcSine, {}, ""}), "not_found");
}

TEST_CASE("set_output: routes to a bus or back to master, undo restores", "[commands][output]") {
    const Track a = track(TrackKind::Audio, "Audio");
    const Track bus = track(TrackKind::Bus, "Bus");
    Project p = projectWith({a, bus});
    requireUndo(p, makeSetOutput(a.id, bus.id));
    REQUIRE(makeSetOutput(a.id, bus.id)->apply(p).ok());
    requireUndo(p, makeSetOutput(a.id, Uuid{}));  // back to master
    requireRoundTrip(makeSetOutput(a.id, bus.id));
    requireRoundTrip(makeSetOutput(a.id, Uuid{}));
}

TEST_CASE("set_output: self, non-bus, cycles, master, unknown are rejected", "[commands][output]") {
    const Track a = track(TrackKind::Audio, "Audio");
    const Track b1 = track(TrackKind::Bus, "Bus 1");
    const Track b2 = track(TrackKind::Bus, "Bus 2");
    Project p = projectWith({a, b1, b2});
    REQUIRE(makeSetOutput(b1.id, b2.id)->apply(p).ok());
    requireRejected(p, makeSetOutput(b1.id, b1.id), "bad_output");
    requireRejected(p, makeSetOutput(a.id, p.master()->id), "bad_output");  // master is the null output, not a bus
    requireRejected(p, makeSetOutput(b2.id, b1.id), "cycle");               // b1 -> b2 already
    requireRejected(p, makeSetOutput(a.id, Uuid::random(gRng)), "bad_output");
    requireRejected(p, makeSetOutput(p.master()->id, b1.id), "invalid_kind");
    requireRejected(p, makeSetOutput(Uuid::random(gRng), b1.id), "not_found");
}

namespace {
Send makeSend(const Uuid& target) { return Send{Uuid::random(gRng), target, -6.0f, false}; }
ProcessorRef gainInsert(double db) { return ProcessorRef{kProcGain, {{"gainDb", db}}, ""}; }
}  // namespace

TEST_CASE("set_send: changes level and pre/post, a patch changes only what it carries, undo restores", "[commands][send]") {
    const Track a = track(TrackKind::Audio, "Audio");
    const Track bus = track(TrackKind::Bus, "Bus");
    Project p = projectWith({a, bus});
    const Send s = makeSend(bus.id);
    REQUIRE(makeAddSend(a.id, s)->apply(p).ok());
    SendPatch level;
    level.levelDb = -12.0f;
    requireUndo(p, makeSetSend(s.id, level));
    REQUIRE(makeSetSend(s.id, level)->apply(p).ok());
    REQUIRE(p.findTrack(a.id)->strip.sends[0].levelDb == -12.0f);
    REQUIRE_FALSE(p.findTrack(a.id)->strip.sends[0].preFader);
    SendPatch pre;
    pre.preFader = true;
    requireUndo(p, makeSetSend(s.id, pre));
    requireRoundTrip(makeSetSend(s.id, level));
    requireRoundTrip(makeSetSend(s.id, pre));
}

TEST_CASE("set_send: out of range, NaN and unknown sends are rejected", "[commands][send]") {
    const Track a = track(TrackKind::Audio, "Audio");
    const Track bus = track(TrackKind::Bus, "Bus");
    Project p = projectWith({a, bus});
    const Send s = makeSend(bus.id);
    REQUIRE(makeAddSend(a.id, s)->apply(p).ok());
    SendPatch tooLoud;
    tooLoud.levelDb = 13.0f;
    requireRejected(p, makeSetSend(s.id, tooLoud), "bad_value");
    SendPatch nan;
    nan.levelDb = std::nanf("");
    requireRejected(p, makeSetSend(s.id, nan), "bad_value");
    requireRejected(p, makeSetSend(Uuid::random(gRng), SendPatch{}), "not_found");
}

TEST_CASE("set_region_gain: sets the gain of one region, undo restores", "[commands][regiongain]") {
    Track k = track(TrackKind::Instrument, "Keys");
    Region r;
    r.id = Uuid::random(gRng);
    r.length = kPPQ * 4;
    k.regions.push_back(r);
    Project p = projectWith({k});
    requireUndo(p, makeSetRegionGain(r.id, -6.0f));
    requireRoundTrip(makeSetRegionGain(r.id, -6.0f));
    requireRejected(p, makeSetRegionGain(r.id, 30.0f), "bad_value");
    requireRejected(p, makeSetRegionGain(r.id, -100.0f), "bad_value");
    requireRejected(p, makeSetRegionGain(r.id, std::nanf("")), "bad_value");
    requireRejected(p, makeSetRegionGain(Uuid::random(gRng), 0.0f), "not_found");
}

TEST_CASE("add_insert and remove_insert: append, insert at an index, remove, exact inverses", "[commands][insert]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    REQUIRE(makeAddInsert(a.id, gainInsert(1.0))->apply(p).ok());
    requireUndo(p, makeAddInsert(a.id, gainInsert(2.0)));              // append
    requireUndo(p, makeAddInsert(a.id, gainInsert(3.0), 0));            // at the front
    requireUndo(p, makeRemoveInsert(a.id, 0));
    REQUIRE(makeAddInsert(a.id, gainInsert(2.0), 0)->apply(p).ok());
    REQUIRE(p.findTrack(a.id)->strip.inserts.size() == 2);
    REQUIRE(p.findTrack(a.id)->strip.inserts[0].params.at("gainDb") == 2.0);
    requireRoundTrip(makeAddInsert(a.id, gainInsert(2.0), 1));
    requireRoundTrip(makeRemoveInsert(a.id, 1));
}

TEST_CASE("add_insert and remove_insert: bad processors, parameters and indexes are rejected", "[commands][insert]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    requireRejected(p, makeAddInsert(a.id, ProcessorRef{"vendor.unknown", {}, ""}), "bad_value");
    requireRejected(p, makeAddInsert(a.id, ProcessorRef{kProcGain, {{"gainDb", 99.0}}, ""}), "bad_value");
    requireRejected(p, makeAddInsert(a.id, gainInsert(0.0), 5), "bad_index");
    requireRejected(p, makeAddInsert(a.id, gainInsert(0.0), -2), "bad_index");
    requireRejected(p, makeRemoveInsert(a.id, 0), "bad_index");
    requireRejected(p, makeAddInsert(Uuid::random(gRng), gainInsert(0.0)), "not_found");
}

TEST_CASE("set_insert_param: sets, adds and removes a parameter, undo is exact", "[commands][insert]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    REQUIRE(makeAddInsert(a.id, ProcessorRef{kProcGain, {}, ""})->apply(p).ok());  // no parameters yet
    requireUndo(p, makeSetInsertParam(a.id, 0, "gainDb", 3.0));                     // inverse removes it again
    REQUIRE(makeSetInsertParam(a.id, 0, "gainDb", 3.0)->apply(p).ok());
    requireUndo(p, makeSetInsertParam(a.id, 0, "gainDb", -2.0));                    // inverse restores 3.0
    requireUndo(p, makeSetInsertParam(a.id, 0, "gainDb", std::nullopt));            // removes, inverse restores
    requireRoundTrip(makeSetInsertParam(a.id, 0, "gainDb", 3.0));
    requireRoundTrip(makeSetInsertParam(a.id, 0, "gainDb", std::nullopt));
}

TEST_CASE("set_insert_param: unknown parameter, out of range, NaN, bad index are rejected", "[commands][insert]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    REQUIRE(makeAddInsert(a.id, gainInsert(0.0))->apply(p).ok());
    requireRejected(p, makeSetInsertParam(a.id, 0, "cutoff", 1.0), "bad_value");
    requireRejected(p, makeSetInsertParam(a.id, 0, "gainDb", 25.0), "bad_value");
    requireRejected(p, makeSetInsertParam(a.id, 0, "gainDb", std::nan("")), "bad_value");
    requireRejected(p, makeSetInsertParam(a.id, 1, "gainDb", 1.0), "bad_index");
    requireRejected(p, makeSetInsertParam(a.id, -1, "gainDb", 1.0), "bad_index");
    requireRejected(p, makeSetInsertParam(Uuid::random(gRng), 0, "gainDb", 1.0), "not_found");
}
