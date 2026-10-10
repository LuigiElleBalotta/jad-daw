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

TEST_CASE("regions: controller events are validated and follow split, resize and join", "[controls][commands]") {
    Fixture f;
    Region* region = &f.project.findTrack(f.track.id)->regions[0];
    Region withControls = *region;
    withControls.controls = {MidiControl{0, 0xB0, 64, 127}, MidiControl{kPPQ, 0xE0, 0, 0x40}, MidiControl{3 * kPPQ, 0xB0, 64, 0}};
    REQUIRE(makeReplaceRegion(withControls)->apply(f.project).ok());

    Region bad = withControls;
    bad.controls.push_back(MidiControl{kPPQ, 0xB0, 1, 5});         // out of order
    REQUIRE_FALSE(makeReplaceRegion(bad)->apply(f.project).ok());
    bad = withControls;
    bad.controls = {MidiControl{0, 0x90, 60, 100}};                // a note is not a controller event
    REQUIRE_FALSE(makeReplaceRegion(bad)->apply(f.project).ok());
    bad = withControls;
    bad.controls = {MidiControl{0, 0xB0, 200, 1}};                 // data above 127
    REQUIRE_FALSE(makeReplaceRegion(bad)->apply(f.project).ok());
    bad = withControls;
    bad.controls = {MidiControl{10 * kPPQ, 0xB0, 1, 1}};           // outside the region
    REQUIRE_FALSE(makeReplaceRegion(bad)->apply(f.project).ok());

    std::mt19937_64 rng(5);
    const Uuid right = Uuid::random(rng);
    auto split = makeSplitRegion(withControls.id, 2 * kPPQ, right)->apply(f.project);
    REQUIRE(split.ok());
    const auto& regions = f.project.findTrack(f.track.id)->regions;
    REQUIRE(regions.size() == 2);
    REQUIRE(regions[0].controls.size() == 2);                      // before the cut
    REQUIRE(regions[1].controls.size() == 1);
    REQUIRE(regions[1].controls[0].tick == kPPQ);                  // 3 quarters - 2 quarters
    auto joined = makeJoinRegions({withControls.id, right})->apply(f.project);
    REQUIRE(joined.ok());
    REQUIRE(f.project.findTrack(f.track.id)->regions.size() == 1);
    REQUIRE(f.project.findTrack(f.track.id)->regions[0].controls == withControls.controls);
    REQUIRE(joined.inverse->apply(f.project).ok());
    REQUIRE(f.project.findTrack(f.track.id)->regions.size() == 2);
}

TEST_CASE("regions: controller events reach a plug-in instrument at their frames", "[controls][plugin][instrument][graph]") {
    Fixture f;
    Region withControls = f.project.findTrack(f.track.id)->regions[0];
    withControls.controls = {MidiControl{kPPQ / 2, 0xB0, 64, 127}, MidiControl{kPPQ, 0xE0, 0, 0x50}};
    REQUIRE(makeReplaceRegion(withControls)->apply(f.project).ok());
    FakePluginHost host;
    host.instruments[kInst] = 0;
    RenderGraph graph(48000.0);
    for (const AudioMsg& m : initialMessages(f.project, f.media, &host)) graph.apply(m);
    std::vector<float> l(256), r(256);
    for (std::int64_t at = 0; at < Fixture::kQuarter + 512; at += 256) graph.render(at, 256, l.data(), r.data());
    bool pedal = false, bend = false;
    for (const auto& s : host.lastInstrument->seen) {
        if (s.event.status == 0xb0 && s.event.data1 == 64 && s.event.data2 == 127 && s.frame == Fixture::kQuarter / 2) pedal = true;
        if (s.event.status == 0xe0 && s.event.data2 == 0x50 && s.frame == Fixture::kQuarter) bend = true;
    }
    REQUIRE(pedal);
    REQUIRE(bend);
    REQUIRE(graph.liveControl(f.track.id, 0xb0, 1, 99));           // live controller messages are queued for the next block
    graph.render(0, 256, l.data(), r.data(), false);
    REQUIRE(host.lastInstrument->seen.back().event.data1 == 1);
}

TEST_CASE("regions: a looped MIDI region repeats its notes and a muted region is silent", "[loop][mute][graph]") {
    Fixture f;
    Region looped = f.project.findTrack(f.track.id)->regions[0];
    REQUIRE(makeSetRegionLoop(looped.id, 2 * kPPQ)->apply(f.project).ok());
    FakePluginHost host;
    host.instruments[kInst] = 0;
    {
        RenderGraph graph(48000.0);
        for (const AudioMsg& m : initialMessages(f.project, f.media, &host)) graph.apply(m);
        std::vector<float> l(256), r(256);
        for (std::int64_t at = 0; at < 4 * Fixture::kQuarter + 512; at += 256) graph.render(at, 256, l.data(), r.data());
        int ons = 0;
        long long secondPass = -1;
        for (const auto& s : host.lastInstrument->seen)
            if ((s.event.status & 0xf0) == 0x90) {
                ++ons;
                if (s.event.data1 == 60 && s.frame >= 2 * Fixture::kQuarter) secondPass = s.frame;
            }
        REQUIRE(ons == 4);                                           // two notes, played twice
        REQUIRE(secondPass == 2 * Fixture::kQuarter);                // the repeat starts one loop later
    }
    REQUIRE_FALSE(makeSetRegionLoop(looped.id, 5 * kPPQ)->apply(f.project).ok());      // longer than the region
    REQUIRE_FALSE(makeSplitRegion(looped.id, kPPQ, Uuid{9, 9})->apply(f.project).ok());   // a looped region is not split
    auto shorter = makeResizeRegion(looped.id, 0, kPPQ)->apply(f.project);             // the loop never exceeds the region
    REQUIRE(shorter.ok());
    REQUIRE(f.project.findTrack(f.track.id)->regions[0].loopLength == kPPQ);
    REQUIRE_FALSE(makeResizeRegion(looped.id, kPPQ, kPPQ)->apply(f.project).ok());     // the left edge of a looped region stays

    Region muted = f.project.findTrack(f.track.id)->regions[0];
    muted.muted = true;
    REQUIRE(makeReplaceRegion(muted)->apply(f.project).ok());
    FakePluginHost host2;
    host2.instruments[kInst] = 0;
    RenderGraph silent(48000.0);
    for (const AudioMsg& m : initialMessages(f.project, f.media, &host2)) silent.apply(m);
    std::vector<float> l(256), r(256);
    for (std::int64_t at = 0; at < 2 * Fixture::kQuarter; at += 256) silent.render(at, 256, l.data(), r.data());
    REQUIRE(host2.lastInstrument->seen.empty());
}

TEST_CASE("notes are shaped by the region and the track when they play", "[shaping][graph]") {
    Fixture f;
    Region shaped = f.project.findTrack(f.track.id)->regions[0];   // notes: 60 (velocity 100) at 0 and 64 (velocity 90) at one quarter
    shaped.transpose = 2;
    shaped.velocityOffset = -10;
    REQUIRE(makeReplaceRegion(shaped)->apply(f.project).ok());
    TrackPatch patch;
    MidiShaping m;
    m.transpose = 1;
    m.velocity = -5;
    patch.midi = m;
    REQUIRE(makeSetTrackProps(f.track.id, patch)->apply(f.project).ok());
    auto played = [&](Project& project) {
        FakePluginHost host;
        host.instruments[kInst] = 0;
        RenderGraph graph(48000.0);
        for (const AudioMsg& msg : initialMessages(project, f.media, &host)) graph.apply(msg);
        std::vector<float> l(256), r(256);
        for (std::int64_t at = 0; at < 2 * Fixture::kQuarter + 512; at += 256) graph.render(at, 256, l.data(), r.data());
        std::vector<std::pair<int, int>> ons;   // pitch, velocity
        if (host.lastInstrument)
            for (const auto& s : host.lastInstrument->seen)
                if ((s.event.status & 0xf0) == 0x90) ons.emplace_back(s.event.data1, s.event.data2);
        return ons;
    };
    auto ons = played(f.project);
    REQUIRE(ons.size() == 2);
    REQUIRE(ons[0] == std::make_pair(63, 85));      // 60 + 2 + 1, 100 - 10 - 5
    REQUIRE(ons[1] == std::make_pair(67, 75));

    m.keyLow = 65;                                    // Key Limit: the first note is not played
    patch.midi = m;
    REQUIRE(makeSetTrackProps(f.track.id, patch)->apply(f.project).ok());
    ons = played(f.project);
    REQUIRE(ons.size() == 1);
    REQUIRE(ons[0].first == 67);

    m.velocityHigh = 70;                              // Velocity Limit: brought inside
    patch.midi = m;
    REQUIRE(makeSetTrackProps(f.track.id, patch)->apply(f.project).ok());
    REQUIRE(played(f.project)[0].second == 70);

    patch.midi = MidiShaping{-99, 0, 0, 127, 1, 127};    // out of range is refused
    REQUIRE_FALSE(makeSetTrackProps(f.track.id, patch)->apply(f.project).ok());
    MidiShaping crossed;
    crossed.keyLow = 80;
    crossed.keyHigh = 20;
    patch.midi = crossed;
    REQUIRE_FALSE(makeSetTrackProps(f.track.id, patch)->apply(f.project).ok());
    shaped.transpose = 99;
    REQUIRE_FALSE(makeReplaceRegion(shaped)->apply(f.project).ok());
}

TEST_CASE("quantize moves the start of the notes to the grid when they play", "[shaping][graph]") {
    Fixture f;
    Region r = f.project.findTrack(f.track.id)->regions[0];
    r.notes = {MidiNote{kPPQ / 2 + 100, kPPQ, 60, 100, false}};    // a little late after the eighth
    r.quantize = kPPQ / 2;                                          // eighth notes
    REQUIRE(makeReplaceRegion(r)->apply(f.project).ok());
    FakePluginHost host;
    host.instruments[kInst] = 0;
    RenderGraph graph(48000.0);
    for (const AudioMsg& msg : initialMessages(f.project, f.media, &host)) graph.apply(msg);
    std::vector<float> l(256), r2(256);
    for (std::int64_t at = 0; at < 2 * Fixture::kQuarter; at += 256) graph.render(at, 256, l.data(), r2.data());
    bool found = false;
    for (const auto& s : host.lastInstrument->seen)
        if ((s.event.status & 0xf0) == 0x90) {
            REQUIRE(s.frame == Fixture::kQuarter / 2);              // on the eighth, not 100 ticks after it
            found = true;
        }
    REQUIRE(found);
}

TEST_CASE("track delay moves the notes of an instrument track by milliseconds", "[delay][graph]") {
    Fixture f;
    TrackPatch patch;
    patch.delayMs = 10.0;
    REQUIRE(makeSetTrackProps(f.track.id, patch)->apply(f.project).ok());
    FakePluginHost host;
    host.instruments[kInst] = 0;
    RenderGraph graph(48000.0);
    for (const AudioMsg& msg : initialMessages(f.project, f.media, &host)) graph.apply(msg);
    std::vector<float> l(256), r(256);
    for (std::int64_t at = 0; at < Fixture::kQuarter; at += 256) graph.render(at, 256, l.data(), r.data());
    REQUIRE(host.lastInstrument->seen.front().frame == 480);           // 10 ms at 48 kHz
    patch.delayMs = 5000.0;
    REQUIRE_FALSE(makeSetTrackProps(f.track.id, patch)->apply(f.project).ok());
    patch.delayMs = -20.0;
    REQUIRE(makeSetTrackProps(f.track.id, patch)->apply(f.project).ok());
    Track audio;
    audio.id = Uuid{4, 4};
    audio.kind = TrackKind::Bus;
    audio.name = "Bus";
    REQUIRE(makeAddTrack(audio)->apply(f.project).ok());
    TrackPatch onBus;
    onBus.delayMs = 1.0;
    REQUIRE_FALSE(makeSetTrackProps(audio.id, onBus)->apply(f.project).ok());   // a bus has no delay
}
