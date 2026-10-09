#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "lpc/audio/engine.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "rt_guard.h"

using namespace lpc;
using namespace lpc::audio;

namespace {

constexpr double kSr = 48000.0;

AudioMsg msg(MsgKind kind, std::uint64_t seq, std::int64_t frame = 0, std::int64_t frame2 = 0) {
    AudioMsg m;
    m.kind = kind;
    m.seq = seq;
    m.frame = frame;
    m.frame2 = frame2;
    return m;
}

// One audio track playing a ramp (value = frame / 100000) from frame 0; the source is 48000 frames long.
struct Setup {
    std::mt19937_64 rng{5};
    Project p{Uuid::random(rng)};
    MediaStore media;
    Uuid audioId;

    Setup() {
        std::vector<float> ramp(2 * 48000);
        for (int i = 0; i < 48000; ++i) ramp[static_cast<std::size_t>(2 * i)] = ramp[static_cast<std::size_t>(2 * i + 1)] = static_cast<float>(i) / 100000.0f;
        const MediaItem item{Uuid::random(rng), "audio/ramp.wav", "h", 48000, 2, 48000};
        media.registerSource(item.id, std::make_shared<MemorySource>(48000, 2, ramp));
        REQUIRE(makeAddMedia(item)->apply(p).ok());
        Track t;
        t.id = Uuid::random(rng);
        t.kind = TrackKind::Audio;
        t.name = "A";
        Region r;
        r.id = Uuid::random(rng);
        r.length = 8 * kPPQ;
        r.mediaId = item.id;
        t.regions.push_back(r);
        audioId = t.id;
        REQUIRE(makeAddTrack(t)->apply(p).ok());
    }

    void load(AudioEngine& e) {
        for (const AudioMsg& m : initialMessages(p, media)) e.applyDirect(m);
    }
};

float rampAt(std::int64_t frame) { return static_cast<float>(frame) / 100000.0f; }

}  // namespace

TEST_CASE("engine: stopped is silent; play, locate and stop move the transport", "[engine]") {
    Setup s;
    AudioEngine e(kSr);
    s.load(e);
    std::vector<float> l(256), r(256);

    e.processBlock(l.data(), r.data(), 256);
    for (float v : l) REQUIRE(v == 0.0f);
    REQUIRE_FALSE(e.playing());
    REQUIRE(e.positionFrames() == 0);

    REQUIRE(e.postMessage(msg(MsgKind::Play, 1)));
    e.processBlock(l.data(), r.data(), 256);
    REQUIRE(e.playing());
    REQUIRE(l[100] == rampAt(100));
    REQUIRE(e.positionFrames() == 256);
    REQUIRE(e.appliedSeq() == 1);

    REQUIRE(e.postMessage(msg(MsgKind::Locate, 2, 1000)));
    e.processBlock(l.data(), r.data(), 256);
    REQUIRE(l[0] == rampAt(1000));
    REQUIRE(l[255] == rampAt(1255));
    REQUIRE(e.positionFrames() == 1256);
    REQUIRE(e.masterPeak() > 0.0f);

    REQUIRE(e.postMessage(msg(MsgKind::Stop, 3)));
    e.processBlock(l.data(), r.data(), 256);
    for (float v : l) REQUIRE(v == 0.0f);
    REQUIRE(e.positionFrames() == 1256);  // stopped: the playhead stays where it is
    REQUIRE_FALSE(e.playing());
}

TEST_CASE("engine: a full queue reports failure instead of dropping, and draining makes room", "[engine]") {
    AudioEngine e(kSr);
    for (std::size_t i = 0; i < kMessageQueueCapacity; ++i) REQUIRE(e.postMessage(msg(MsgKind::Stop, i + 1)));
    REQUIRE_FALSE(e.postMessage(msg(MsgKind::Stop, 99999)));
    std::vector<float> l(64), r(64);
    e.processBlock(l.data(), r.data(), 64);
    REQUIRE(e.appliedSeq() == kMaxMessagesPerBlock);  // one block applies a bounded batch, in order
    for (int block = 0; block < 20 && e.appliedSeq() < kMessageQueueCapacity; ++block) e.processBlock(l.data(), r.data(), 64);
    REQUIRE(e.appliedSeq() == kMessageQueueCapacity);  // all of them, in order
    REQUIRE(e.postMessage(msg(MsgKind::Stop, 99999)));
}

TEST_CASE("engine: replaced objects come back for destruction on the project thread", "[engine]") {
    Setup s;
    AudioEngine e(kSr);
    s.load(e);
    AudioMsg set;
    set.kind = MsgKind::SetConfig;
    set.seq = 5;
    set.track = s.audioId;
    set.obj = makeOwned(buildConfig(s.p, *s.p.findTrack(s.audioId), s.media).release());
    REQUIRE(e.postMessage(set));
    std::vector<float> l(64), r(64);
    e.processBlock(l.data(), r.data(), 64);
    REQUIRE(e.appliedSeq() == 5);
    REQUIRE(e.collectGarbage() == 1);
    REQUIRE(e.collectGarbage() == 0);
    REQUIRE(e.garbageOverflow() == 0);
}

TEST_CASE("engine: any block length gives the same samples", "[engine]") {
    Setup s;
    AudioEngine big(kSr), small(kSr);
    s.load(big);
    s.load(small);
    big.postMessage(msg(MsgKind::Play, 1));
    small.postMessage(msg(MsgKind::Play, 1));
    std::vector<float> a(1000), ar(1000), b(1000), br(1000);
    big.processBlock(a.data(), ar.data(), 1000);  // sliced internally (> kMaxBlock)
    for (int i = 0; i < 4; ++i) small.processBlock(b.data() + i * 250, br.data() + i * 250, 250);
    REQUIRE(a == b);
    REQUIRE(a[999] == rampAt(999));
}

TEST_CASE("engine: loop wraps sample-accurately and can be switched off", "[engine][loop]") {
    Setup s;
    AudioEngine e(kSr);
    s.load(e);
    e.postMessage(msg(MsgKind::SetLoop, 1, 100, 300));
    e.postMessage(msg(MsgKind::Locate, 2, 100));
    e.postMessage(msg(MsgKind::Play, 3));
    std::vector<float> l(1100), r(1100);
    e.processBlock(l.data(), r.data(), 1100);
    for (std::int64_t i = 0; i < 1100; ++i) REQUIRE(l[static_cast<std::size_t>(i)] == rampAt(100 + i % 200));
    REQUIRE(e.positionFrames() == 200);  // five full loops plus 100 frames

    e.postMessage(msg(MsgKind::SetLoop, 4, 0, 0));  // end <= start: loop off
    std::vector<float> m(400), mr(400);
    e.processBlock(m.data(), mr.data(), 400);
    REQUIRE(m[0] == rampAt(200));
    REQUIRE(m[399] == rampAt(599));
}

TEST_CASE("engine: playing, message handling and garbage hand-back never allocate", "[engine][rt]") {
    Setup s;
    AudioEngine e(kSr);
    s.load(e);
    AudioMsg set;
    set.kind = MsgKind::SetConfig;
    set.seq = 10;
    set.track = s.audioId;
    set.obj = makeOwned(buildConfig(s.p, *s.p.findTrack(s.audioId), s.media).release());  // built outside the section
    std::vector<float> l(256), r(256);
    e.postMessage(msg(MsgKind::Play, 1));
    e.postMessage(set);
    e.postMessage(msg(MsgKind::SetLoop, 11, 0, 5000));
    test::rt::reset();
    {
        test::rt::Scope scope;
        for (int i = 0; i < 40; ++i) e.processBlock(l.data(), r.data(), 256);
    }
    REQUIRE(test::rt::violations() == 0);
    REQUIRE(e.collectGarbage() == 1);
}

TEST_CASE("engine: the metronome clicks on the beats while playing and stays silent when off", "[engine][click]") {
    AudioEngine e(kSr);
    AudioMsg click = msg(MsgKind::SetClick, 1, 1);
    click.obj = makeOwned(new ClickTrack{{100, 24000}, {1, 0}});
    e.applyDirect(click);
    e.applyDirect(msg(MsgKind::Play, 2));
    std::vector<float> l(512), r(512);
    e.processBlock(l.data(), r.data(), 512);
    for (int i = 0; i < 100; ++i) REQUIRE(l[static_cast<std::size_t>(i)] == 0.0f);   // before the first beat
    float peak = 0;
    for (int i = 100; i < 512; ++i) peak = std::max(peak, std::abs(l[static_cast<std::size_t>(i)]));
    REQUIRE(peak > 0.1f);                                                              // the click sounds
    REQUIRE(l[100] == r[100]);
    e.applyDirect(msg(MsgKind::SetClick, 3, 0));                                       // off: keeps the beats, mutes the click
    e.applyDirect(msg(MsgKind::Locate, 4, 0));
    e.processBlock(l.data(), r.data(), 512);
    for (int i = 0; i < 512; ++i) REQUIRE(l[static_cast<std::size_t>(i)] == 0.0f);
}

TEST_CASE("engine: a click slot with a sample plays the sample, the others the built-in blip", "[engine][click]") {
    AudioEngine e(kSr);
    auto* kit = new ClickKit;
    kit->sample[1] = std::vector<float>(10, 0.5f);
    kit->gain[1] = 0.5f;
    AudioMsg k = msg(MsgKind::SetClickKit, 1);
    k.obj = makeOwned(kit);
    e.applyDirect(k);
    AudioMsg c = msg(MsgKind::SetClick, 2, 1);
    c.obj = makeOwned(new ClickTrack{{100, 300}, {1, 0}, {1, 2}});  // slot 1 has a sample, slot 2 does not
    e.applyDirect(c);
    e.applyDirect(msg(MsgKind::Play, 3));
    std::vector<float> l(512), r(512);
    e.processBlock(l.data(), r.data(), 512);
    for (int i = 100; i < 110; ++i) REQUIRE(l[static_cast<std::size_t>(i)] == Catch::Approx(0.25f));  // 0.5 * gain 0.5
    REQUIRE(l[110] == 0.0f);                                                                          // the sample ended
    float blip = 0;
    for (int i = 300; i < 512; ++i) blip = std::max(blip, std::abs(l[static_cast<std::size_t>(i)]));
    REQUIRE(blip > 0.05f);                                                                            // slot 2: the built-in click
}

TEST_CASE("engine: a recording counts in with the click, then captures every input channel of the playing blocks", "[engine][record]") {
    AudioEngine e(kSr);
    AudioMsg rec = msg(MsgKind::StartRecord, 1, 300);  // 300 frames of count-in
    rec.obj = makeOwned(new ClickTrack{{0, 150}, {1, 0}, {1, 2}});
    e.applyDirect(rec);
    REQUIRE(e.recording());
    std::vector<float> l(512), r(512), a(512, 0.25f), b(512, -0.25f), c3(512, 0.5f);
    const float* in[3] = {a.data(), b.data(), c3.data()};
    e.input(in, 3, 512);
    e.processBlock(l.data(), r.data(), 512);
    float blip = 0;
    for (int i = 0; i < 300; ++i) blip = std::max(blip, std::abs(l[static_cast<std::size_t>(i)]));
    REQUIRE(blip > 0.05f);                      // the count-in is audible
    AudioEngine::RecChunk c;
    std::size_t frames = 0;
    while (e.takeRecorded(c)) {
        REQUIRE(c.channels == 3);
        for (int i = 0; i < c.frames; ++i) { REQUIRE(c.ch[0][i] == 0.25f); REQUIRE(c.ch[1][i] == -0.25f); REQUIRE(c.ch[2][i] == 0.5f); }
        frames += static_cast<std::size_t>(c.frames);
    }
    REQUIRE(frames == 212);                     // only the part after the count-in (512 - 300)
    e.applyDirect(msg(MsgKind::Stop, 2));
    REQUIRE_FALSE(e.recording());
    e.input(in, 3, 512);
    e.processBlock(l.data(), r.data(), 512);
    REQUIRE_FALSE(e.takeRecorded(c));           // stopped: nothing more is captured
}

TEST_CASE("engine: the capture reports where each block lies, StopRecord leaves the transport running, input levels are measured", "[engine][record]") {
    AudioEngine e(kSr);
    e.applyDirect(msg(MsgKind::Locate, 1, 1000));
    e.applyDirect(msg(MsgKind::Play, 2));
    AudioMsg rec = msg(MsgKind::StartRecord, 3, 0);   // a punch-in: no count-in, the transport is already running
    rec.obj = makeOwned(new ClickTrack);
    e.applyDirect(rec);
    std::vector<float> l(256), r(256), a(256, 0.4f);
    const float* in[1] = {a.data()};
    e.input(in, 1, 256);
    e.processBlock(l.data(), r.data(), 256);
    AudioEngine::RecChunk c;
    REQUIRE(e.takeRecorded(c));
    REQUIRE(c.position == 1000);
    REQUIRE(e.takeInputPeak(0) == Catch::Approx(0.4f));
    REQUIRE(e.takeInputPeak(0) == 0.0f);                // reading clears it
    e.applyDirect(msg(MsgKind::StopRecord, 4));
    REQUIRE_FALSE(e.recording());
    REQUIRE(e.playing());                               // still playing
    e.input(in, 1, 256);
    e.processBlock(l.data(), r.data(), 256);
    REQUIRE_FALSE(e.takeRecorded(c));
}

TEST_CASE("engine: a monitored track plays the chosen input channels through its strip", "[engine][monitor]") {
    Setup s;
    AudioEngine e(kSr);
    for (const AudioMsg& m : initialMessages(s.p, s.media, nullptr, nullptr)) e.applyDirect(m);
    Uuid track;
    for (const Track& t : s.p.tracks) if (t.kind == TrackKind::Audio) track = t.id;
    AudioMsg mon = msg(MsgKind::SetMonitor, 100, 1, 2);   // inputs 1 and 2
    mon.track = track;
    e.applyDirect(mon);
    std::vector<float> l(256), r(256), a(256, 0.3f), b(256, -0.1f);
    const float* in[2] = {a.data(), b.data()};
    e.applyDirect(msg(MsgKind::Locate, 101, 200000));      // far past the track's region: only the input is heard
    e.applyDirect(msg(MsgKind::Play, 102));
    e.input(in, 2, 256);
    e.processBlock(l.data(), r.data(), 256);
    REQUIRE(l[100] == Catch::Approx(0.3f).margin(0.02f));
    REQUIRE(r[100] == Catch::Approx(-0.1f).margin(0.02f));
    AudioMsg off = msg(MsgKind::SetMonitor, 103, 0, 0);
    off.track = track;
    e.applyDirect(off);
    e.input(in, 2, 256);
    e.processBlock(l.data(), r.data(), 256);
    REQUIRE(std::abs(l[100]) < 0.01f);
}
