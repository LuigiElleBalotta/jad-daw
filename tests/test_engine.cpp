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
