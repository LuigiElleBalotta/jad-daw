#include <catch2/catch_test_macros.hpp>
#include <random>
#include "fake_plugin_host.h"
#include "lpc/audio/processors.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "lpc/model_json.h"
#include "lpc/plugin_host.h"
#include "lpc/undo_stack.h"

using namespace lpc;

namespace {
const char* kPlug = "vst3:000000000000000000000000000000aa";

struct Fixture {
    std::mt19937_64 rng{41};
    Project p;
    Uuid t;
    Fixture() {
        Track tr;
        tr.id = Uuid::random(rng);
        tr.kind = TrackKind::Audio;
        tr.name = "A";
        t = tr.id;
        REQUIRE(makeAddTrack(tr)->apply(p).ok());
        ProcessorRef gain;
        gain.processorId = "builtin.gain";
        gain.params["gainDb"] = 6.0;
        REQUIRE(makeAddInsert(t, gain)->apply(p).ok());
    }
};
}  // namespace

TEST_CASE("set_insert_bypass: sets, clears and undoes", "[commands][insert][bypass]") {
    Fixture f;
    UndoStack undo;
    const Project before = f.p;
    REQUIRE_FALSE(undo.execute(f.p, makeSetInsertBypass(f.t, 0, true)).has_value());
    REQUIRE(f.p.findTrack(f.t)->strip.inserts[0].bypass);
    REQUIRE_FALSE(undo.undo(f.p).has_value());
    REQUIRE(f.p == before);
}

TEST_CASE("set_insert_bypass: bad index and unknown track change nothing", "[commands][insert][bypass]") {
    Fixture f;
    const Project before = f.p;
    auto bad = [&](CommandPtr c, const char* code) {
        auto r = c->apply(f.p);
        REQUIRE_FALSE(r.ok());
        REQUIRE(r.error->code == code);
        REQUIRE(f.p == before);
    };
    bad(makeSetInsertBypass(f.t, 1, true), "bad_index");
    bad(makeSetInsertBypass(f.t, -1, true), "bad_index");
    bad(makeSetInsertBypass(Uuid{9, 9}, 0, true), "not_found");
}

TEST_CASE("set_insert_bypass: JSON command round trip", "[commands][insert][bypass][json]") {
    Fixture f;
    const auto j = makeSetInsertBypass(f.t, 0, true)->toJson();
    REQUIRE(j.at("bypass") == true);
    REQUIRE(commandFromJson(j)->toJson() == j);
    REQUIRE(commandFromJson(j)->apply(f.p).ok());
}

TEST_CASE("bypass: written to the project only when true, old documents load as false", "[model][json][bypass]") {
    Fixture f;
    REQUIRE_FALSE(toJson(f.p).at("tracks").at(1).at("strip").at("inserts").at(0).contains("bypass"));
    REQUIRE(makeSetInsertBypass(f.t, 0, true)->apply(f.p).ok());
    auto j = toJson(f.p);
    REQUIRE(j.at("tracks").at(1).at("strip").at("inserts").at(0).at("bypass") == true);
    REQUIRE(projectFromJson(j) == f.p);
    j["tracks"][1]["strip"]["inserts"][0]["bypass"] = "yes";
    REQUIRE_THROWS_AS(projectFromJson(j), std::runtime_error);
}

TEST_CASE("bypass: a bypassed gain insert passes the signal unchanged", "[audio][bypass]") {
    ProcessorRef ref;
    ref.processorId = "builtin.gain";
    ref.params["gainDb"] = 6.0;
    std::vector<float> l(64, 0.5f), r(64, -0.25f);
    makeInsert(ref, nullptr, InsertSlot{Uuid{1, 1}, 0}, 48000.0, 512)->process(l.data(), r.data(), 64);
    REQUIRE(l[0] > 0.9f);  // +6 dB
    std::fill(l.begin(), l.end(), 0.5f);
    std::fill(r.begin(), r.end(), -0.25f);
    ref.bypass = true;
    auto bypassed = makeInsert(ref, nullptr, InsertSlot{Uuid{1, 1}, 0}, 48000.0, 512);
    REQUIRE(bypassed->latencySamples() == 0);
    bypassed->process(l.data(), r.data(), 64);
    REQUIRE(l[0] == 0.5f);
    REQUIRE(r[63] == -0.25f);
}

TEST_CASE("bypass: a bypassed plug-in keeps its latency and delays the dry signal by it", "[audio][bypass][pdc]") {
    test::FakePluginHost host;
    host.known[kPlug] = test::FakeSpec{32, 0.5f};
    ProcessorRef ref;
    ref.processorId = kPlug;
    ref.bypass = true;
    auto proc = makeInsert(ref, &host, InsertSlot{Uuid{1, 1}, 0}, 48000.0, 512);
    REQUIRE(proc->latencySamples() == 32);
    std::vector<float> l(128, 0.0f), r(128, 0.0f);
    l[0] = r[0] = 1.0f;
    proc->process(l.data(), r.data(), 128);
    REQUIRE(l[32] == 1.0f);  // delayed by the latency, and not halved by the gain of the plug-in
    REQUIRE(l[0] == 0.0f);
}

TEST_CASE("bypass: delay compensation is the same with the insert bypassed or not", "[pdc][bypass]") {
    test::FakePluginHost host;
    host.known[kPlug] = test::FakeSpec{64, 1.0f};
    Fixture f;
    Track other;
    other.id = Uuid::random(f.rng);
    other.kind = TrackKind::Audio;
    other.name = "B";
    REQUIRE(makeAddTrack(other)->apply(f.p).ok());
    ProcessorRef plug;
    plug.processorId = kPlug;
    REQUIRE(makeAddInsert(f.t, plug)->apply(f.p).ok());
    const PdcPlan live = computePdc(f.p, &host);
    REQUIRE(makeSetInsertBypass(f.t, 1, true)->apply(f.p).ok());
    const PdcPlan bypassed = computePdc(f.p, &host);
    REQUIRE(bypassed.totalLatency == live.totalLatency);
    REQUIRE(bypassed.edges.at(other.id) == live.edges.at(other.id));
    REQUIRE(live.edges.at(other.id).output == 64);
}
