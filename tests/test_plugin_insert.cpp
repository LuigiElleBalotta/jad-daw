#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <random>
#include "fake_plugin_host.h"
#include "lpc/audio/render_graph.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "lpc/processor_ids.h"

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

TEST_CASE("automation: a lane drives a parameter of a plug-in insert, a lane for a missing insert is refused", "[plugin][automation]") {
    std::mt19937_64 rng(41);
    Project project;
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Audio;
    t.name = "A";
    REQUIRE(makeAddTrack(t)->apply(project).ok());
    REQUIRE(makeAddInsert(t.id, plugin())->apply(project).ok());
    const std::string target = makeParamTarget(kId, 0, 3);
    REQUIRE(parseParamTarget(target).has_value());
    REQUIRE(parseParamTarget(target)->index == 3);
    REQUIRE_FALSE(parseParamTarget("param/vst3:short/0/3").has_value());
    REQUIRE_FALSE(parseParamTarget("param/" + std::string(kId) + "/x/3").has_value());

    REQUIRE_FALSE(makeSetAutomation(t.id, makeParamTarget(kId, 1, 3), {AutomationPoint{0, 0.5}})->apply(project).ok());           // only one insert of it
    REQUIRE_FALSE(makeSetAutomation(t.id, makeParamTarget("vst3:ffffffffffffffffffffffffffffffff", 0, 3), {AutomationPoint{0, 0.5}})->apply(project).ok());
    REQUIRE_FALSE(makeSetAutomation(t.id, target, {AutomationPoint{0, 1.5}})->apply(project).ok());                                // normalised: 0..1
    auto set = makeSetAutomation(t.id, target, {AutomationPoint{0, 0.25}, AutomationPoint{kPPQ * 2, 0.75}})->apply(project);
    REQUIRE(set.ok());

    FakePluginHost host;
    host.known[kId] = FakeSpec{0, 1.0f};
    RenderGraph graph(48000.0);
    MediaStore media;
    for (const AudioMsg& m : initialMessages(project, media, &host)) graph.apply(m);
    REQUIRE(host.lastFake);
    std::vector<float> l(256), r(256);
    for (std::int64_t at = 0; at < 24000 + 512; at += 256) graph.render(at, 256, l.data(), r.data());   // one beat at 120 bpm: half of the ramp
    REQUIRE_FALSE(host.lastFake->given.empty());
    REQUIRE(host.lastFake->given.front().first == 3);
    REQUIRE(host.lastFake->given.front().second == Catch::Approx(0.25f).margin(0.01));
    REQUIRE(host.lastFake->given.back().second == Catch::Approx(0.5f).margin(0.02));
    REQUIRE(set.inverse->apply(project).ok());
    REQUIRE(project.findTrack(t.id)->automation.empty());
}
