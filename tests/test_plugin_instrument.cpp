#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <random>

#include "fake_plugin_host.h"
#include "lpc/audio/render_graph.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "lpc/validation.h"

using namespace lpc;
using namespace lpc::audio;
using lpc::test::FakePluginHost;

namespace {
const char* kInst = "vst3:aabbccddeeff00112233445566778899";

ProcessorRef pluginInstrument(const char* id = kInst, const char* state = "") {
    ProcessorRef r;
    r.processorId = id;
    r.state = state;
    return r;
}

struct Fixture {
    Project project;
    Track track;
    MediaStore media;
    Fixture() {
        std::mt19937_64 rng(11);
        track.id = Uuid::random(rng);
        track.kind = TrackKind::Instrument;
        track.name = "Keys";
        track.instrument = pluginInstrument();
        Region region;
        region.id = Uuid::random(rng);
        region.timeBase = TimeBase::Musical;
        region.start = 0;
        region.length = 4 * kPPQ;
        region.notes.push_back(MidiNote{0, kPPQ, 60, 100, false});
        region.notes.push_back(MidiNote{kPPQ, kPPQ, 64, 90, false});
        track.regions.push_back(region);
        REQUIRE(makeAddTrack(track)->apply(project).ok());
    }
    // 120 bpm at 48 kHz: a quarter note is 24000 frames
    static constexpr std::int64_t kQuarter = 24000;
};
}  // namespace

TEST_CASE("plug-in instruments: ids are valid for an instrument track, effects and junk are not", "[plugin][instrument]") {
    REQUIRE_FALSE(checkInstrument(pluginInstrument()).has_value());
    REQUIRE_FALSE(checkInstrument(pluginInstrument(kInst, "QUJD")).has_value());   // base64 state
    REQUIRE(checkInstrument(pluginInstrument(kInst, "not base64!")).has_value());
    ProcessorRef params = pluginInstrument();
    params.params["x"] = 1;
    REQUIRE(checkInstrument(params).has_value());                                    // plug-ins take no parameters
    REQUIRE(checkInstrument(pluginInstrument("vst3:short")).has_value());
    REQUIRE(checkInstrument(pluginInstrument("builtin.gain")).has_value());
    ProcessorRef sine;
    sine.processorId = "builtin.sine";
    REQUIRE_FALSE(checkInstrument(sine).has_value());
}

TEST_CASE("set_instrument accepts a plug-in and set_instrument_state stores its state with undo", "[plugin][instrument][commands]") {
    Fixture f;
    ProcessorRef sine;
    sine.processorId = "builtin.sine";
    REQUIRE(makeSetInstrument(f.track.id, sine)->apply(f.project).ok());
    auto set = makeSetInstrument(f.track.id, pluginInstrument())->apply(f.project);
    REQUIRE(set.ok());
    REQUIRE(f.project.findTrack(f.track.id)->instrument->processorId == kInst);
    auto state = makeSetInstrumentState(f.track.id, "QUJD")->apply(f.project);
    REQUIRE(state.ok());
    REQUIRE(f.project.findTrack(f.track.id)->instrument->state == "QUJD");
    REQUIRE(state.inverse->apply(f.project).ok());
    REQUIRE(f.project.findTrack(f.track.id)->instrument->state.empty());
    REQUIRE_FALSE(makeSetInstrumentState(f.track.id, "???")->apply(f.project).ok());
    REQUIRE(makeSetInstrument(f.track.id, sine)->apply(f.project).ok());
    REQUIRE_FALSE(makeSetInstrumentState(f.track.id, "QUJD")->apply(f.project).ok());  // the sine has no state
    auto fromJson = commandFromJson(makeSetInstrumentState(f.track.id, "QUJD")->toJson());
    REQUIRE(fromJson);
}

TEST_CASE("plug-in instruments: the notes of the regions reach the plug-in at the right frames", "[plugin][instrument][graph]") {
    Fixture f;
    FakePluginHost host;
    host.instruments[kInst] = 0;
    auto cfg = buildConfig(f.project, *f.project.findTrack(f.track.id), f.media, &host);
    REQUIRE(cfg->instrument);
    REQUIRE(cfg->instrument->describe().at("fakeInstrument") == true);

    RenderGraph graph(48000.0);
    for (const AudioMsg& m : initialMessages(f.project, f.media, &host)) graph.apply(m);
    std::vector<float> l(256), r(256);
    // blocks of 256 frames across the whole first two quarter notes
    for (std::int64_t at = 0; at < 2 * Fixture::kQuarter + 512; at += 256) graph.render(at, 256, l.data(), r.data());
    REQUIRE(host.lastInstrument);
    const auto& seen = host.lastInstrument->seen;
    REQUIRE(seen.size() == 4);
    REQUIRE((seen[0].event.status & 0xf0) == 0x90);
    REQUIRE(seen[0].event.data1 == 60);
    REQUIRE(seen[0].event.data2 == 100);
    REQUIRE(seen[0].frame == 0);
    REQUIRE((seen[1].event.status & 0xf0) == 0x80);   // the first note ends where the second begins; off comes first
    REQUIRE(seen[1].frame == Fixture::kQuarter);
    REQUIRE((seen[2].event.status & 0xf0) == 0x90);
    REQUIRE(seen[2].event.data1 == 64);
    REQUIRE(seen[2].frame == Fixture::kQuarter);
    REQUIRE((seen[3].event.status & 0xf0) == 0x80);
    REQUIRE(seen[3].frame == 2 * Fixture::kQuarter);
}

TEST_CASE("plug-in instruments: the sound goes through the strip and the stop sends the controller resets", "[plugin][instrument][graph]") {
    Fixture f;
    FakePluginHost host;
    host.instruments[kInst] = 0;
    RenderGraph graph(48000.0);
    for (const AudioMsg& m : initialMessages(f.project, f.media, &host)) graph.apply(m);
    std::vector<float> l(256), r(256);
    graph.render(0, 256, l.data(), r.data());
    REQUIRE(l[10] != 0.0f);                                  // the held note sounds through the fader and the master
    graph.allNotesOff();                                     // the transport stopped with the first note held
    REQUIRE(graph.liveNeeded());
    graph.render(256, 256, l.data(), r.data(), false);       // stopped: only what is pending is rendered
    const auto& seen = host.lastInstrument->seen;
    REQUIRE(seen.size() >= 8);
    bool noteOff = false, sustain = false, bend = false;
    for (const auto& s : seen) {
        if ((s.event.status & 0xf0) == 0x80 && s.event.data1 == 60) noteOff = true;
        if (s.event.status == 0xb0 && s.event.data1 == 64 && s.event.data2 == 0) sustain = true;
        if (s.event.status == 0xe0 && s.event.data2 == 0x40) bend = true;
    }
    REQUIRE(noteOff);
    REQUIRE(sustain);
    REQUIRE(bend);
}

TEST_CASE("plug-in instruments: live notes reach the plug-in and a missing plug-in is silent", "[plugin][instrument][graph]") {
    Fixture f;
    FakePluginHost host;
    host.instruments[kInst] = 0;
    RenderGraph graph(48000.0);
    for (const AudioMsg& m : initialMessages(f.project, f.media, &host)) graph.apply(m);
    std::vector<float> l(256), r(256);
    REQUIRE(graph.liveNote(f.track.id, true, 72, 80));
    graph.render(0, 256, l.data(), r.data(), false);
    REQUIRE(l[5] != 0.0f);
    REQUIRE(host.lastInstrument->seen.back().event.data1 == 72);

    FakePluginHost empty;                                    // knows nothing: the track is silent, not an error
    RenderGraph silent(48000.0);
    for (const AudioMsg& m : initialMessages(f.project, f.media, &empty)) silent.apply(m);
    silent.render(0, 256, l.data(), r.data());
    for (float v : l) REQUIRE(v == 0.0f);
}

TEST_CASE("plug-in instruments: the plug-in latency is part of the track's delay compensation", "[plugin][instrument][pdc]") {
    Fixture f;
    FakePluginHost host;
    host.instruments[kInst] = 120;
    const PdcPlan plan = computePdc(f.project, &host);
    REQUIRE(plan.totalLatency == 120);
}
