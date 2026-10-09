#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <random>
#include "fake_plugin_host.h"
#include "lpc/audio/render_graph.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "lpc/offline_render.h"

using namespace lpc;
using namespace lpc::audio;
using lpc::test::FakePluginHost;
using lpc::test::FakeSpec;

namespace {

std::string idOf(int n) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "vst3:%032x", n);
    return buf;
}

struct Rig {
    std::mt19937_64 rng{21};
    Project p;
    FakePluginHost host;

    Uuid addTrack(TrackKind kind, const char* name, int latency = 0) {
        Track t;
        t.id = Uuid::random(rng);
        t.kind = kind;
        t.name = name;
        if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{"builtin.sine", {}, ""};
        REQUIRE(makeAddTrack(t)->apply(p).ok());
        if (latency > 0) {
            const std::string id = idOf(latency);
            host.known[id] = FakeSpec{latency, 1.0f};
            ProcessorRef r;
            r.processorId = id;
            REQUIRE(makeAddInsert(t.id, r)->apply(p).ok());
        }
        return t.id;
    }
    void routeOutput(const Uuid& track, const Uuid& to) { REQUIRE(makeSetOutput(track, to)->apply(p).ok()); }
    void send(const Uuid& track, const Uuid& to) {
        Send s;
        s.id = Uuid::random(rng);
        s.targetTrackId = to;
        REQUIRE(makeAddSend(track, s)->apply(p).ok());
    }
};

}  // namespace

TEST_CASE("DelayLine: delays by N frames across block boundaries", "[pdc]") {
    DelayLine d(4);
    std::vector<float> in(10, 0.0f), inR(10, 0.0f), outL(10), outR(10);
    in[0] = 1.0f;
    inR[1] = 1.0f;
    int pos = 0;
    for (int n : {3, 3, 4}) {  // blocks of 3, 3, 4
        d.process(in.data() + pos, inR.data() + pos, outL.data() + pos, outR.data() + pos, n);
        pos += n;
    }
    for (int i = 0; i < 10; ++i) {
        REQUIRE(outL[static_cast<std::size_t>(i)] == (i == 4 ? 1.0f : 0.0f));
        REQUIRE(outR[static_cast<std::size_t>(i)] == (i == 5 ? 1.0f : 0.0f));
    }
}

TEST_CASE("DelayLine: zero frames is a copy", "[pdc]") {
    DelayLine d(0);
    const float inL[3] = {1, 2, 3}, inR[3] = {4, 5, 6};
    float outL[3], outR[3];
    d.process(inL, inR, outL, outR, 3);
    REQUIRE(outL[2] == 3.0f);
    REQUIRE(outR[0] == 4.0f);
}

TEST_CASE("pdc: no plug-ins means no delays", "[pdc]") {
    Rig r;
    const Uuid a = r.addTrack(TrackKind::Audio, "A");
    const PdcPlan plan = computePdc(r.p, &r.host);
    REQUIRE(plan.totalLatency == 0);
    REQUIRE(plan.edges.at(a).output == 0);
}

TEST_CASE("pdc: two tracks to the master", "[pdc]") {
    Rig r;
    const Uuid a = r.addTrack(TrackKind::Audio, "A", 64);
    const Uuid b = r.addTrack(TrackKind::Audio, "B");
    const PdcPlan plan = computePdc(r.p, &r.host);
    REQUIRE(plan.totalLatency == 64);
    REQUIRE(plan.edges.at(a).output == 0);
    REQUIRE(plan.edges.at(b).output == 64);
}

TEST_CASE("pdc: through a bus that has its own latency", "[pdc]") {
    Rig r;
    const Uuid bus = r.addTrack(TrackKind::Bus, "Bus", 30);
    const Uuid a = r.addTrack(TrackKind::Audio, "A", 100);
    const Uuid b = r.addTrack(TrackKind::Audio, "B");
    r.routeOutput(a, bus);
    const PdcPlan plan = computePdc(r.p, &r.host);
    REQUIRE(plan.edges.at(a).output == 0);    // alone into the bus
    REQUIRE(plan.edges.at(bus).output == 0);  // 100 + 30 = 130 arrives at the master
    REQUIRE(plan.edges.at(b).output == 130);
    REQUIRE(plan.totalLatency == 130);
}

TEST_CASE("pdc: sends are aligned at the bus", "[pdc]") {
    Rig r;
    const Uuid bus = r.addTrack(TrackKind::Bus, "Bus");
    const Uuid a = r.addTrack(TrackKind::Audio, "A", 50);
    const Uuid b = r.addTrack(TrackKind::Audio, "B");
    r.send(a, bus);
    r.send(b, bus);
    const PdcPlan plan = computePdc(r.p, &r.host);
    REQUIRE(plan.edges.at(a).sends == std::vector<int>{0});
    REQUIRE(plan.edges.at(b).sends == std::vector<int>{50});
    REQUIRE(plan.edges.at(a).output == 0);
    REQUIRE(plan.edges.at(b).output == 50);  // B's direct output is aligned with the delayed signals
    REQUIRE(plan.totalLatency == 50);
}

TEST_CASE("pdc: a plug-in that is not loaded yet has no latency", "[pdc]") {
    Rig r;
    const Uuid a = r.addTrack(TrackKind::Audio, "A", 64);
    r.host.deferLoads = true;
    const PdcPlan plan = computePdc(r.p, &r.host);
    REQUIRE(plan.totalLatency == 0);
    REQUIRE(plan.edges.at(a).output == 0);
}

TEST_CASE("pdc: absurd latencies are clamped", "[pdc]") {
    Rig r;
    r.addTrack(TrackKind::Audio, "A", 10'000'000);
    const PdcPlan plan = computePdc(r.p, &r.host);
    REQUIRE(plan.totalLatency <= kMaxPdcFrames);
}

TEST_CASE("pdc: diffToMessages rebuilds the configs whose delays change", "[pdc][graph]") {
    Rig r;
    const Uuid a = r.addTrack(TrackKind::Audio, "A");
    const Uuid b = r.addTrack(TrackKind::Audio, "B");
    Project before = r.p;
    // a plug-in with latency appears on A: A's config changes (insert), and B's too (its output now needs 64 frames)
    const std::string id = idOf(64);
    r.host.known[id] = FakeSpec{64, 1.0f};
    ProcessorRef ref;
    ref.processorId = id;
    REQUIRE(makeAddInsert(a, ref)->apply(r.p).ok());
    MediaStore media;
    const auto msgs = diffToMessages(before, r.p, media, &r.host);
    int configs = 0;
    for (const AudioMsg& m : msgs) {
        if (m.kind == MsgKind::SetConfig) {
            ++configs;
            if (m.track == b) REQUIRE(static_cast<TrackConfig*>(m.obj.ptr)->outputDelay.frames() == 64);
        }
    }
    REQUIRE(configs == 2);
    for (AudioMsg m : msgs) m.obj.destroy();
}

TEST_CASE("pdc: refreshMessages sends a config for every track", "[pdc][graph]") {
    Rig r;
    r.addTrack(TrackKind::Audio, "A");
    r.addTrack(TrackKind::Audio, "B");
    MediaStore media;
    auto msgs = refreshMessages(r.p, media, &r.host);
    REQUIRE(msgs.size() == r.p.tracks.size());
    for (AudioMsg m : msgs) {
        REQUIRE(m.kind == MsgKind::SetConfig);
        m.obj.destroy();
    }
}

TEST_CASE("pdc: offline render is aligned with the timeline", "[pdc][render]") {
    Rig r;
    const Uuid a = r.addTrack(TrackKind::Audio, "A", 64);
    const Uuid b = r.addTrack(TrackKind::Audio, "B");
    // an impulse at frame 100, on both tracks
    std::vector<float> impulse(2 * 4800, 0.0f);
    impulse[2 * 100] = impulse[2 * 100 + 1] = 1.0f;
    MediaItem item{Uuid::random(r.rng), "x.wav", "h", 48000, 2, 4800};
    REQUIRE(makeAddMedia(item)->apply(r.p).ok());
    MediaStore media;
    media.registerSource(item.id, std::make_shared<MemorySource>(48000, 2, impulse));
    for (const Uuid& t : {a, b}) {
        Region reg;
        reg.id = Uuid::random(r.rng);
        reg.timeBase = TimeBase::Absolute;
        reg.start = 0;
        reg.length = 100000;  // 0.1 s
        reg.mediaId = item.id;
        REQUIRE(makeAddRegion(t, reg)->apply(r.p).ok());
    }
    RenderOptions o;
    o.frames = 400;
    o.plugins = &r.host;
    const RenderResult res = renderOffline(r.p, media, o);
    REQUIRE(res.frames == 400);
    for (int i = 0; i < 400; ++i) {
        const float expected = i == 100 ? 2.0f : 0.0f;  // both impulses on the same frame
        REQUIRE(std::abs(res.interleaved[static_cast<std::size_t>(i) * 2] - expected) < 1e-5f);
    }
}
