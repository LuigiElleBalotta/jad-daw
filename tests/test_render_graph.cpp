#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "lpc/audio/frame_source.h"
#include "lpc/audio/render_graph.h"
#include "rt_guard.h"

using namespace lpc;
using namespace lpc::audio;

namespace {

constexpr double kSr = 48000.0;
constexpr std::uint64_t kMaster = 100;

Uuid id(std::uint64_t n) { return Uuid{0, n}; }

struct Stereo {
    std::vector<float> l, r;
};

// Builds nodes the way the project thread would and keeps media alive.
struct Rig {
    RenderGraph g{kSr};
    std::vector<std::shared_ptr<IFrameSource>> media;

    void addNode(std::uint64_t n, TrackKind kind, TrackConfig* cfg, StripParams strip = {}) {
        AudioMsg m;
        m.kind = MsgKind::AddTrack;
        m.obj = makeOwned(new TrackNode(id(n), kind, strip, cfg, kSr));
        REQUIRE(g.apply(m).ptr == nullptr);
    }
    void addMaster() { addNode(kMaster, TrackKind::Master, new TrackConfig); }

    // A track config with one audio region playing a constant (DC) stereo signal.
    TrackConfig* dcConfig(float dc, std::int64_t start, std::int64_t end, float regionGain = 1.0f) {
        auto* cfg = new TrackConfig;
        auto src = std::make_shared<MemorySource>(48000, 2, std::vector<float>(2 * 2000, dc));
        media.push_back(src);
        RegionPlayback r;
        r.startFrame = start;
        r.endFrame = end;
        r.source = src.get();
        r.gain = regionGain;
        cfg->regions.push_back(r);
        cfg->keepAlive.push_back(src);
        return cfg;
    }

    // A track config with one MIDI region holding note spans in absolute frames.
    static TrackConfig* noteConfig(std::vector<NoteSpan> notes) {
        auto* cfg = new TrackConfig;
        RegionPlayback r;
        r.startFrame = 0;
        r.endFrame = 1'000'000;
        r.notes = std::move(notes);
        cfg->regions.push_back(std::move(r));
        return cfg;
    }

    Stereo render(std::int64_t total, int block = 256) {
        Stereo s{std::vector<float>(static_cast<std::size_t>(total)), std::vector<float>(static_cast<std::size_t>(total))};
        for (std::int64_t pos = 0; pos < total; pos += block) {
            const int n = static_cast<int>(std::min<std::int64_t>(block, total - pos));
            g.render(pos, n, &s.l[static_cast<std::size_t>(pos)], &s.r[static_cast<std::size_t>(pos)]);
        }
        return s;
    }

    void send(const AudioMsg& m) { g.apply(m).destroy(); }
    void setStrip(std::uint64_t n, StripParams p) {
        AudioMsg m;
        m.kind = MsgKind::SetStrip;
        m.track = id(n);
        m.strip = p;
        send(m);
    }
};

StripParams strip(float gain, float pan = 0.0f, bool mute = false, bool solo = false) { return StripParams{gain, pan, mute, solo}; }

void requireAll(const std::vector<float>& v, std::size_t from, std::size_t to, float expected, float tol = 1e-6f) {
    for (std::size_t i = from; i < to; ++i) REQUIRE(std::abs(v[i] - expected) <= tol);
}

}  // namespace

TEST_CASE("graph: an empty graph and a lone master render silence", "[graph]") {
    Rig empty;
    const Stereo a = empty.render(600);
    requireAll(a.l, 0, 600, 0.0f);
    Rig lone;
    lone.addMaster();
    const Stereo b = lone.render(600);
    requireAll(b.l, 0, 600, 0.0f);
    REQUIRE(lone.g.masterPeak() == 0.0f);
}

TEST_CASE("graph: an audio region plays exactly inside its frame range", "[graph]") {
    Rig rig;
    rig.addNode(1, TrackKind::Audio, rig.dcConfig(0.5f, 100, 600));
    rig.addMaster();
    const Stereo s = rig.render(1000);
    requireAll(s.l, 0, 100, 0.0f);
    requireAll(s.l, 100, 600, 0.5f);
    requireAll(s.l, 600, 1000, 0.0f);
    requireAll(s.r, 100, 600, 0.5f);
    REQUIRE(rig.g.masterPeak() == 0.0f);  // the last block (frames 768..999) is silent
}

TEST_CASE("graph: region gain, strip gain and pan", "[graph]") {
    Rig rig;
    rig.addNode(1, TrackKind::Audio, rig.dcConfig(0.5f, 0, 1000, 0.5f), strip(0.5f));
    rig.addMaster();
    requireAll(rig.render(400).l, 0, 400, 0.125f);  // 0.5 * 0.5 * 0.5

    Rig panned;
    panned.addNode(1, TrackKind::Audio, panned.dcConfig(0.5f, 0, 1000), strip(1.0f, 1.0f));
    panned.addMaster();
    const Stereo hardRight = panned.render(300);
    requireAll(hardRight.l, 0, 300, 0.0f);
    requireAll(hardRight.r, 0, 300, 0.5f);

    Rig half;
    half.addNode(1, TrackKind::Audio, half.dcConfig(0.5f, 0, 1000), strip(1.0f, -0.5f));
    half.addMaster();
    const Stereo halfLeft = half.render(300);
    requireAll(halfLeft.l, 0, 300, 0.5f);
    requireAll(halfLeft.r, 0, 300, 0.25f);
}

TEST_CASE("graph: mute and solo", "[graph]") {
    Rig rig;
    rig.addNode(1, TrackKind::Audio, rig.dcConfig(0.5f, 0, 1000));
    rig.addNode(2, TrackKind::Audio, rig.dcConfig(0.25f, 0, 1000));
    rig.addMaster();
    requireAll(rig.render(300).l, 0, 300, 0.75f);

    rig.setStrip(1, strip(1.0f, 0.0f, true));  // mute track 1 (ramps in over one block, so skip the first block)
    rig.render(256);
    requireAll(rig.render(300).l, 0, 300, 0.25f);

    rig.setStrip(1, strip(1.0f));
    rig.setStrip(2, strip(1.0f, 0.0f, false, true));  // solo track 2
    rig.render(256);
    requireAll(rig.render(300).l, 0, 300, 0.25f);

    rig.setStrip(1, strip(1.0f, 0.0f, false, true));  // both soloed
    rig.render(256);
    requireAll(rig.render(300).l, 0, 300, 0.75f);
}

TEST_CASE("graph: solo does not silence buses", "[graph]") {
    Rig rig;
    TrackConfig* a = rig.dcConfig(0.5f, 0, 1000);
    a->output = id(50);
    rig.addNode(1, TrackKind::Audio, a, strip(1.0f, 0.0f, false, true));
    rig.addNode(50, TrackKind::Bus, new TrackConfig);
    rig.addMaster();
    requireAll(rig.render(300).l, 0, 300, 0.5f);
}

TEST_CASE("graph: output routing and sends through a bus", "[graph]") {
    // track -> bus (strip gain 0.5) -> master
    Rig routed;
    TrackConfig* t = routed.dcConfig(0.5f, 0, 1000);
    t->output = id(50);
    routed.addNode(1, TrackKind::Audio, t);
    routed.addNode(50, TrackKind::Bus, new TrackConfig, strip(0.5f));
    routed.addMaster();
    requireAll(routed.render(300).l, 0, 300, 0.25f);

    // post-fader send: direct 0.5 + (0.5 * 0.5 send) = 0.75
    Rig post;
    TrackConfig* p = post.dcConfig(0.5f, 0, 1000);
    p->sends.push_back({id(50), 0.5f, false});
    post.addNode(1, TrackKind::Audio, p);
    post.addNode(50, TrackKind::Aux, new TrackConfig);
    post.addMaster();
    requireAll(post.render(300).l, 0, 300, 0.75f);

    // with strip gain 0.5: post-fader send follows the fader, pre-fader does not
    Rig postHalf;
    TrackConfig* ph = postHalf.dcConfig(0.5f, 0, 1000);
    ph->sends.push_back({id(50), 0.5f, false});
    postHalf.addNode(1, TrackKind::Audio, ph, strip(0.5f));
    postHalf.addNode(50, TrackKind::Aux, new TrackConfig);
    postHalf.addMaster();
    requireAll(postHalf.render(300).l, 0, 300, 0.375f);  // 0.25 direct + 0.125 send

    Rig pre;
    TrackConfig* pr = pre.dcConfig(0.5f, 0, 1000);
    pr->sends.push_back({id(50), 0.5f, true});
    pre.addNode(1, TrackKind::Audio, pr, strip(0.5f));
    pre.addNode(50, TrackKind::Aux, new TrackConfig);
    pre.addMaster();
    requireAll(pre.render(300).l, 0, 300, 0.5f);  // 0.25 direct + 0.25 pre-fader send

    // a muted track sends nothing, pre-fader included
    Rig muted;
    TrackConfig* mu = muted.dcConfig(0.5f, 0, 1000);
    mu->sends.push_back({id(50), 1.0f, true});
    muted.addNode(1, TrackKind::Audio, mu, strip(1.0f, 0.0f, true));
    muted.addNode(50, TrackKind::Aux, new TrackConfig);
    muted.addMaster();
    muted.render(256);
    requireAll(muted.render(300).l, 0, 300, 0.0f);
}

TEST_CASE("graph: sends to a missing track are ignored", "[graph]") {
    Rig rig;
    TrackConfig* cfg = rig.dcConfig(0.5f, 0, 1000);
    cfg->sends.push_back({id(777), 1.0f, false});
    cfg->output = id(778);  // missing output: the signal goes nowhere, nothing crashes
    rig.addNode(1, TrackKind::Audio, cfg);
    rig.addMaster();
    requireAll(rig.render(300).l, 0, 300, 0.0f);
}

TEST_CASE("graph: insert processors run before the strip", "[graph]") {
    Rig rig;
    TrackConfig* cfg = rig.dcConfig(0.5f, 0, 1000);
    cfg->inserts.push_back(std::make_unique<GainProcessor>(-6.0206f));
    rig.addNode(1, TrackKind::Audio, cfg);
    rig.addMaster();
    const Stereo s = rig.render(300);
    for (std::size_t i = 0; i < 300; ++i) REQUIRE(s.l[i] == Catch::Approx(0.25f).margin(1e-4));
}

TEST_CASE("graph: sine instrument plays notes with sample-accurate timing", "[graph][synth]") {
    Rig rig;
    rig.addNode(2, TrackKind::Instrument, Rig::noteConfig({NoteSpan{100, 4900, 69, 127}}));
    rig.addMaster();
    const Stereo s = rig.render(6000);

    requireAll(s.l, 0, 100, 0.0f, 0.0f);  // silent until the note starts
    double sum = 0.0;
    for (std::size_t i = 1000; i < 4000; ++i) sum += double(s.l[i]) * s.l[i];
    const double rms = std::sqrt(sum / 3000.0);
    REQUIRE(rms == Catch::Approx(0.2 / std::sqrt(2.0)).epsilon(0.05));  // amplitude 0.2 at velocity 127
    int crossings = 0;
    for (std::size_t i = 1001; i < 4000; ++i)
        if (s.l[i - 1] < 0.0f && s.l[i] >= 0.0f) ++crossings;
    REQUIRE(crossings >= 26);  // 440 Hz over 3000 samples at 48 kHz is 27.5 cycles
    REQUIRE(crossings <= 29);
    requireAll(s.l, 5200, 6000, 0.0f, 1e-7f);  // released well before frame 5200
    for (std::size_t i = 0; i < 6000; ++i) REQUIRE(s.l[i] == s.r[i]);
}

TEST_CASE("graph: rendering is identical for any block size", "[graph]") {
    auto build = [](Rig& rig) {
        TrackConfig* a = rig.dcConfig(0.4f, 50, 5000);
        a->sends.push_back({id(50), 0.5f, false});
        rig.addNode(1, TrackKind::Audio, a, strip(0.8f, -0.3f));
        rig.addNode(2, TrackKind::Instrument, Rig::noteConfig({NoteSpan{100, 3000, 60, 100}, NoteSpan{1500, 1500, 67, 90}, NoteSpan{2900, 5900, 72, 127}}), strip(0.9f, 0.4f));
        rig.addNode(50, TrackKind::Bus, new TrackConfig, strip(0.7f));
        rig.addMaster();
    };
    Rig reference;
    build(reference);
    const Stereo expected = reference.render(6000, 256);
    for (const int block : {1, 7, 64, 300, 512}) {
        CAPTURE(block);
        Rig rig;
        build(rig);
        const Stereo got = rig.render(6000, block);
        REQUIRE(got.l == expected.l);
        REQUIRE(got.r == expected.r);
    }
}

TEST_CASE("graph: strip changes are ramped, not stepped", "[graph]") {
    Rig rig;
    rig.addNode(1, TrackKind::Audio, rig.dcConfig(0.5f, 0, 1000));
    rig.addMaster();
    rig.render(100, 100);  // first block: gain 1
    rig.setStrip(1, strip(0.0f));
    const Stereo s = rig.render(100, 100);
    // frames 100..199 are rendered by the second call, which starts at position 0 again; only the ramp matters here
    REQUIRE(std::abs(s.l[0] - 0.5f * 0.99f) < 1e-3f);
    for (std::size_t i = 1; i < 100; ++i) REQUIRE(s.l[i] < s.l[i - 1]);
    REQUIRE(std::abs(s.l[99]) < 1e-5f);
}

TEST_CASE("graph: structural messages return what must be freed", "[graph][messages]") {
    Rig rig;
    TrackConfig* a = rig.dcConfig(0.5f, 0, 1000);
    a->output = id(50);
    rig.addNode(1, TrackKind::Audio, a);
    rig.addNode(50, TrackKind::Bus, new TrackConfig);
    rig.addMaster();
    REQUIRE(rig.g.trackCount() == 3);
    requireAll(rig.render(300).l, 0, 300, 0.5f);

    // wrong order: the bus is processed before its input, so nothing reaches the master in the same block
    auto* wrong = new std::vector<Uuid>{id(50), id(1), id(kMaster)};
    AudioMsg reorder;
    reorder.kind = MsgKind::Reorder;
    reorder.obj = makeOwned(wrong);
    Owned back = rig.g.apply(reorder);
    REQUIRE(back.ptr == wrong);  // handed back for destruction on the project thread
    back.destroy();
    requireAll(rig.render(300).l, 0, 300, 0.0f);

    // a reorder that does not list every track is ignored
    auto* partial = new std::vector<Uuid>{id(1)};
    reorder.obj = makeOwned(partial);
    rig.g.apply(reorder).destroy();
    requireAll(rig.render(300).l, 0, 300, 0.0f);  // still the wrong order from before

    auto* right = new std::vector<Uuid>{id(1), id(50), id(kMaster)};
    reorder.obj = makeOwned(right);
    rig.g.apply(reorder).destroy();
    requireAll(rig.render(300).l, 0, 300, 0.5f);

    // SetConfig swaps the config and returns the old one
    TrackConfig* fresh = rig.dcConfig(0.25f, 0, 1000);
    fresh->output = id(50);
    AudioMsg cfg;
    cfg.kind = MsgKind::SetConfig;
    cfg.track = id(1);
    cfg.obj = makeOwned(fresh);
    Owned old = rig.g.apply(cfg);
    REQUIRE(old.ptr != nullptr);
    REQUIRE(old.ptr != fresh);
    old.destroy();
    requireAll(rig.render(300).l, 0, 300, 0.25f);

    // SetConfig for an unknown track gives the new config straight back
    TrackConfig* orphan = new TrackConfig;
    cfg.track = id(999);
    cfg.obj = makeOwned(orphan);
    Owned returned = rig.g.apply(cfg);
    REQUIRE(returned.ptr == orphan);
    returned.destroy();

    // RemoveTrack returns the node
    AudioMsg rm;
    rm.kind = MsgKind::RemoveTrack;
    rm.track = id(1);
    Owned node = rig.g.apply(rm);
    REQUIRE(node.ptr != nullptr);
    node.destroy();
    REQUIRE(rig.g.trackCount() == 2);
    requireAll(rig.render(300).l, 0, 300, 0.0f);

    AudioMsg rmUnknown;
    rmUnknown.kind = MsgKind::RemoveTrack;
    rmUnknown.track = id(12345);
    REQUIRE(rig.g.apply(rmUnknown).ptr == nullptr);
}

TEST_CASE("graph: adding more than kMaxTracks hands the node back instead of overflowing", "[graph][messages]") {
    RenderGraph g(kSr);
    for (int i = 0; i < kMaxTracks; ++i) {
        AudioMsg m;
        m.kind = MsgKind::AddTrack;
        m.obj = makeOwned(new TrackNode(id(1000 + i), TrackKind::Bus, StripParams{}, new TrackConfig, kSr));
        REQUIRE(g.apply(m).ptr == nullptr);
    }
    AudioMsg extra;
    extra.kind = MsgKind::AddTrack;
    extra.obj = makeOwned(new TrackNode(id(5000), TrackKind::Bus, StripParams{}, new TrackConfig, kSr));
    Owned rejected = g.apply(extra);
    REQUIRE(rejected.ptr == extra.obj.ptr);
    rejected.destroy();
    REQUIRE(g.trackCount() == kMaxTracks);
}

TEST_CASE("graph: describe lists tracks in processing order", "[graph]") {
    Rig rig;
    rig.addNode(1, TrackKind::Audio, rig.dcConfig(0.5f, 10, 20));
    rig.addMaster();
    const nlohmann::json d = rig.g.describe();
    REQUIRE(d["tracks"].size() == 2);
    REQUIRE(d["tracks"][0]["id"] == id(1).toString());
    REQUIRE(d["tracks"][0]["regions"][0]["start"] == 10);
    REQUIRE(d["tracks"][1]["id"] == id(kMaster).toString());
}

TEST_CASE("graph: rendering and strip/config messages never allocate", "[graph][rt]") {
    Rig rig;
    TrackConfig* a = rig.dcConfig(0.4f, 0, 5000);
    a->sends.push_back({id(50), 0.5f, true});
    a->inserts.push_back(std::make_unique<GainProcessor>(-3.0f));
    rig.addNode(1, TrackKind::Audio, a, strip(0.8f, -0.3f, false, true));
    rig.addNode(2, TrackKind::Instrument, Rig::noteConfig({NoteSpan{100, 3000, 60, 100}, NoteSpan{500, 900, 64, 100}}));
    rig.addNode(50, TrackKind::Bus, new TrackConfig);
    rig.addMaster();

    TrackConfig* replacement = rig.dcConfig(0.1f, 0, 5000);  // built outside the real-time section
    AudioMsg set;
    set.kind = MsgKind::SetConfig;
    set.track = id(1);
    set.obj = makeOwned(replacement);
    AudioMsg strip2;
    strip2.kind = MsgKind::SetStrip;
    strip2.track = id(1);
    strip2.strip = strip(0.2f, 0.9f);

    std::vector<float> l(256), r(256);
    test::rt::reset();
    Owned garbage;
    {
        test::rt::Scope scope;
        for (int b = 0; b < 20; ++b) {
            if (b == 5) rig.g.apply(strip2);
            if (b == 10) garbage = rig.g.apply(set);
            rig.g.render(b * 256, 256, l.data(), r.data());
        }
        rig.g.allNotesOff();
    }
    garbage.destroy();
    REQUIRE(test::rt::violations() == 0);
}

TEST_CASE("graph: every track reports its post-fader peak and reading clears it", "[graph]") {
    Rig rig;
    rig.addNode(1, TrackKind::Audio, rig.dcConfig(0.5f, 0, 1000), strip(0.5f));
    rig.addNode(2, TrackKind::Audio, rig.dcConfig(0.25f, 0, 1000), strip(1.0f, 0.0f, true));  // muted: silent
    rig.addMaster();
    rig.render(512);
    std::vector<std::pair<Uuid, float>> peaks;
    rig.g.takeTrackPeaks(peaks);
    REQUIRE(peaks.size() == 3);
    auto peakOf = [&](std::uint64_t n) { for (auto& [u, p] : peaks) if (u == id(n)) return p; return -1.0f; };
    REQUIRE(peakOf(1) == Catch::Approx(0.25f));  // 0.5 * 0.5
    REQUIRE(peakOf(2) == 0.0f);
    REQUIRE(peakOf(kMaster) == Catch::Approx(0.25f));
    rig.g.takeTrackPeaks(peaks);  // nothing played since: cleared
    REQUIRE(peakOf(1) == 0.0f);
}
