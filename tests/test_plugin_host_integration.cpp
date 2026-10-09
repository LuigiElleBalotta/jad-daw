#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <random>
#include <thread>
#include "fake_plugin_host.h"
#include "lpc/audio/engine.h"
#include "lpc/commands.h"
#include "lpc/project_host.h"

using namespace lpc;
using namespace lpc::audio;
using lpc::test::FakePluginHost;
using lpc::test::FakeSpec;

namespace {
const char* kId = "vst3:00112233445566778899aabbccddeeff";

// The audio thread is the test thread: it drains the messages the host posted.
void drain(AudioEngine& engine, ProjectHost& host) {
    host.read([](const Project&) { return 0; }).get();  // everything queued on the project thread has run
    float l[64], r[64];
    for (int i = 0; i < 3000 && engine.appliedSeq() < host.lastPostedSeq(); ++i) {
        engine.processBlock(l, r, 64);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    engine.processBlock(l, r, 64);
}

nlohmann::json insertOf(AudioEngine& engine, const Uuid& track) {
    const nlohmann::json graph = engine.describeForTest();  // a named copy: the loop must not iterate into a temporary
    for (const auto& t : graph.at("tracks"))
        if (t.at("id") == track.toString()) return t.at("inserts").at(0);
    return {};
}

struct Rig {
    std::mt19937_64 rng{17};
    Project initial;
    Uuid track;
    FakePluginHost plugins;
    MediaStore media;
    AudioEngine engine{48000.0};
    Rig() {
        Track t;
        t.id = Uuid::random(rng);
        t.kind = TrackKind::Audio;
        t.name = "A";
        track = t.id;
        REQUIRE(makeAddTrack(t)->apply(initial).ok());
        ProcessorRef r;
        r.processorId = kId;
        r.label = "Fake";
        REQUIRE(makeAddInsert(track, r)->apply(initial).ok());
        plugins.known[kId] = FakeSpec{8, 1.0f};
    }
};
}  // namespace

TEST_CASE("host+plugins: a plug-in that is not ready is replaced when it arrives", "[host][plugin][threads]") {
    Rig rig;
    rig.plugins.deferLoads = true;
    ProjectHost host(rig.initial, rig.engine, rig.media, &rig.plugins);
    drain(rig.engine, host);
    REQUIRE(insertOf(rig.engine, rig.track).at("missing") == true);

    rig.plugins.finishLoads();  // fires the ready listener from this thread
    drain(rig.engine, host);
    const auto after = insertOf(rig.engine, rig.track);
    REQUIRE(after.at("fake") == true);
    REQUIRE(after.at("latency") == 8);
}

TEST_CASE("host+plugins: removing the insert prunes the instance", "[host][plugin][threads]") {
    Rig rig;
    ProjectHost host(rig.initial, rig.engine, rig.media, &rig.plugins);
    drain(rig.engine, host);
    REQUIRE(rig.plugins.liveCount() == 1);
    REQUIRE_FALSE(host.submit(makeRemoveInsert(rig.track, 0)).get().has_value());
    drain(rig.engine, host);
    REQUIRE(rig.plugins.liveCount() == 0);
}

TEST_CASE("host+plugins: committing a captured state does not reload the instance", "[host][plugin][threads]") {
    Rig rig;
    ProjectHost host(rig.initial, rig.engine, rig.media, &rig.plugins);
    drain(rig.engine, host);
    REQUIRE(rig.plugins.created == 1);
    const std::optional<std::string> captured = rig.plugins.captureState(InsertSlot{rig.track, 0});  // the editor closed
    REQUIRE(captured);
    REQUIRE_FALSE(host.submit(makeSetInsertState(rig.track, 0, *captured)).get().has_value());
    drain(rig.engine, host);
    REQUIRE(rig.plugins.created == 1);

    // undo puts the old state back in the model: the instance must be reloaded with it
    REQUIRE_FALSE(host.undo().get().has_value());
    drain(rig.engine, host);
    REQUIRE(rig.plugins.created == 2);
}

TEST_CASE("host+plugins: closing the host while a load is pending is safe", "[host][plugin][threads]") {
    Rig rig;
    rig.plugins.deferLoads = true;
    {
        ProjectHost host(rig.initial, rig.engine, rig.media, &rig.plugins);
        drain(rig.engine, host);
    }  // destructor clears the listener
    rig.plugins.finishLoads();  // must not call into the destroyed host
    SUCCEED();
}
