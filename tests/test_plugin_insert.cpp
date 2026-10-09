#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <random>
#include "fake_plugin_host.h"
#include "lpc/audio/render_graph.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"

using namespace lpc;
using namespace lpc::audio;
using lpc::test::FakePluginHost;
using lpc::test::FakeSpec;

namespace {
const char* kId = "vst3:00112233445566778899aabbccddeeff";
ProcessorRef plugin(const char* id = kId) {
    ProcessorRef r;
    r.processorId = id;
    return r;
}
InsertSlot slot() { return InsertSlot{Uuid{1, 2}, 0}; }
}  // namespace

TEST_CASE("makeInsert: built-ins are unchanged", "[plugin]") {
    ProcessorRef gain;
    gain.processorId = "builtin.gain";
    gain.params["gainDb"] = -6.0;
    auto p = makeInsert(gain, nullptr, slot(), 48000.0, 512);
    REQUIRE(p);
    float l[1] = {1.0f}, r[1] = {1.0f};
    p->process(l, r, 1);
    REQUIRE(l[0] == Catch::Approx(0.5012f).margin(0.001));
    REQUIRE(makeInsert(plugin("nope"), nullptr, slot(), 48000.0, 512) == nullptr);  // unknown non-plug-in ids are skipped
}

TEST_CASE("makeInsert: a plug-in without a host is a pass-through marked missing", "[plugin]") {
    auto p = makeInsert(plugin(), nullptr, slot(), 48000.0, 512);
    REQUIRE(p);
    REQUIRE(p->describe().at("missing") == true);
    REQUIRE(p->latencySamples() == 0);
    float l[2] = {0.25f, -0.5f}, r[2] = {1.0f, 2.0f};
    p->process(l, r, 2);
    REQUIRE(l[0] == 0.25f);
    REQUIRE(l[1] == -0.5f);
    REQUIRE(r[1] == 2.0f);
}

TEST_CASE("makeInsert: unknown to the host is missing too", "[plugin]") {
    FakePluginHost host;  // knows nothing
    auto p = makeInsert(plugin(), &host, slot(), 48000.0, 512);
    REQUIRE(p->describe().at("missing") == true);
}

TEST_CASE("makeInsert: a known plug-in is wrapped and forwards latency", "[plugin]") {
    FakePluginHost host;
    host.known[kId] = FakeSpec{4, 0.5f};
    auto p = makeInsert(plugin(), &host, slot(), 48000.0, 512);
    REQUIRE(p->latencySamples() == 4);
    float l[8] = {1, 0, 0, 0, 0, 0, 0, 0}, r[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    p->process(l, r, 8);
    REQUIRE(l[4] == 0.5f);  // delayed by 4 and scaled
    REQUIRE(l[0] == 0.0f);
    REQUIRE(p->describe().at("fake") == true);
}

TEST_CASE("makeInsert: a pending load is a pass-through, not a failure", "[plugin]") {
    FakePluginHost host;
    host.known[kId] = FakeSpec{};
    host.deferLoads = true;
    auto p = makeInsert(plugin(), &host, slot(), 48000.0, 512);
    REQUIRE(p->describe().at("missing") == true);
}

TEST_CASE("buildConfig: plug-in inserts keep their position and ask the host by slot", "[plugin][graph]") {
    std::mt19937_64 rng(3);
    Project project;
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Audio;
    t.name = "A";
    REQUIRE(makeAddTrack(t)->apply(project).ok());
    ProcessorRef gain;
    gain.processorId = "builtin.gain";
    REQUIRE(makeAddInsert(t.id, gain)->apply(project).ok());
    REQUIRE(makeAddInsert(t.id, plugin())->apply(project).ok());

    FakePluginHost host;
    host.known[kId] = FakeSpec{64, 1.0f};
    MediaStore media;
    auto cfg = buildConfig(project, *project.findTrack(t.id), media, &host);
    REQUIRE(cfg->inserts.size() == 2);
    REQUIRE(cfg->inserts[0]->latencySamples() == 0);
    REQUIRE(cfg->inserts[1]->latencySamples() == 64);
    REQUIRE(host.created == 1);

    auto again = buildConfig(project, *project.findTrack(t.id), media, &host);  // reuses the live instance
    REQUIRE(host.created == 1);
}
