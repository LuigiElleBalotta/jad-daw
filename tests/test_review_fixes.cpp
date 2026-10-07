// Regression tests for the findings of the whole-branch review.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <limits>
#include <random>
#include <thread>
#include "lpc/audio/engine.h"
#include "lpc/audio/frame_source.h"
#include "lpc/audio/render_graph.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "lpc/model_json.h"
#include "lpc/offline_render.h"
#include "lpc/project_host.h"
#include "lpc/wav.h"
#include "temp_dir.h"
#include "threaded_device.h"

#ifdef LPC_HAVE_CLI_LIB
#include "cli_commands.h"
#endif

using namespace lpc;
using namespace lpc::audio;
using Catch::Matchers::ContainsSubstring;

namespace {

Uuid id(std::uint64_t n) { return Uuid{0, n}; }

Track makeTrack(std::uint64_t n, TrackKind kind) {
    Track t;
    t.id = id(n);
    t.kind = kind;
    t.name = "t" + std::to_string(n);
    if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{"builtin.sine", {}, ""};
    return t;
}

Project baseProject() {
    Project p{id(1)};
    p.tracks[0].name = "Master";
    return p;
}

// A document that is well formed JSON but breaks a rule the commands enforce.
void requireRejected(const Project& bad) {
    CHECK_THROWS(projectFromJson(toJson(bad)));
}

}  // namespace

// ---------------------------------------------------------------- 1 (Critical): non-ASCII command lines

#if defined(_WIN32) && defined(LPC_HAVE_CLI_LIB)
TEST_CASE("cli: a UTF-16 command line argument becomes UTF-8", "[review][cli]") {
    const std::string utf8 = cli::wideToUtf8(L"My Song è 曲.lpc");
    REQUIRE(utf8 == "My Song \xC3\xA8 \xE6\x9B\xB2.lpc");
}
#endif

// ---------------------------------------------------------------- 2: project.json is validated like commands

TEST_CASE("load: duplicate track ids are rejected", "[review][io]") {
    Project p = baseProject();
    p.tracks.push_back(makeTrack(2, TrackKind::Audio));
    p.tracks.push_back(makeTrack(2, TrackKind::Audio));
    requireRejected(p);
}

TEST_CASE("load: routing to itself or in a loop is rejected", "[review][io]") {
    Project self = baseProject();
    Track bus = makeTrack(2, TrackKind::Bus);
    bus.strip.output = bus.id;
    self.tracks.push_back(bus);
    requireRejected(self);

    Project loop = baseProject();
    Track a = makeTrack(2, TrackKind::Bus), b = makeTrack(3, TrackKind::Bus);
    a.strip.output = b.id;
    b.strip.output = a.id;
    loop.tracks.push_back(a);
    loop.tracks.push_back(b);
    requireRejected(loop);
}

TEST_CASE("load: out of range values and unknown references are rejected", "[review][io]") {
    Project pan = baseProject();
    Track t = makeTrack(2, TrackKind::Audio);
    t.strip.pan = 2.0f;
    pan.tracks.push_back(t);
    requireRejected(pan);

    Project dangling = baseProject();
    Track u = makeTrack(2, TrackKind::Audio);
    u.strip.output = id(99);
    dangling.tracks.push_back(u);
    requireRejected(dangling);

    Project insert = baseProject();
    Track v = makeTrack(2, TrackKind::Audio);
    v.strip.inserts.push_back(ProcessorRef{"nope.fx", {}, ""});
    insert.tracks.push_back(v);
    requireRejected(insert);

    Project media = baseProject();
    media.mediaPool.push_back(MediaItem{id(7), "../outside.wav", "h", 48000, 2, 10});
    requireRejected(media);

    Project noMedia = baseProject();
    Track w = makeTrack(2, TrackKind::Audio);
    Region r;
    r.id = id(5);
    r.length = 960;
    r.mediaId = id(42);  // not in the pool
    w.regions.push_back(r);
    noMedia.tracks.push_back(w);
    requireRejected(noMedia);
}

TEST_CASE("load: a MIDI note above 127 is rejected instead of wrapping", "[review][io]") {
    Project p = baseProject();
    Track t = makeTrack(2, TrackKind::Instrument);
    Region r;
    r.id = id(5);
    r.length = 960;
    r.notes.push_back(MidiNote{0, 480, 60, 100});
    t.regions.push_back(r);
    p.tracks.push_back(t);
    nlohmann::json j = toJson(p);
    j["tracks"][1]["regions"][0]["notes"][0]["note"] = 300;
    REQUIRE_THROWS(projectFromJson(j));
}

TEST_CASE("load: a valid project still loads", "[review][io]") {
    Project p = baseProject();
    p.tracks.push_back(makeTrack(2, TrackKind::Bus));
    Track a = makeTrack(3, TrackKind::Audio);
    a.strip.output = id(2);
    p.tracks.push_back(a);
    REQUIRE(projectFromJson(toJson(p)) == p);
}

// ---------------------------------------------------------------- 3: positions are bounded

TEST_CASE("commands: region positions beyond the supported range are rejected", "[review][commands]") {
    Project p = baseProject();
    Track t = makeTrack(2, TrackKind::Instrument);
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    Region r;
    r.id = id(5);
    r.start = std::numeric_limits<std::int64_t>::max() - 10;
    r.length = 960;
    const auto res = makeAddRegion(t.id, r)->apply(p);
    REQUIRE_FALSE(res.ok());
    REQUIRE(res.error->code == "bad_region");

    r.start = 0;
    REQUIRE(makeAddRegion(t.id, r)->apply(p).ok());
    const auto moved = makeMoveRegion(r.id, (std::int64_t{1} << 62))->apply(p);
    REQUIRE_FALSE(moved.ok());
    REQUIRE(moved.error->code == "bad_region");
}

TEST_CASE("render: a project that would need an absurd amount of memory fails cleanly", "[review][render]") {
    Project p = baseProject();
    Track t = makeTrack(2, TrackKind::Instrument);
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    Region r;
    r.id = id(5);
    r.start = 1'000'000'000'000;  // inside the position limit, far beyond what can be rendered
    r.length = 960;
    REQUIRE(makeAddRegion(t.id, r)->apply(p).ok());
    MediaStore media(std::filesystem::path{}, false);
    REQUIRE_THROWS_WITH(renderOffline(p, media), ContainsSubstring("too long"));
}

// ---------------------------------------------------------------- 4: insert parameters

TEST_CASE("commands: insert parameters are validated", "[review][commands]") {
    Project p = baseProject();
    Track t = makeTrack(2, TrackKind::Audio);
    REQUIRE(makeAddTrack(t)->apply(p).ok());

    const auto loud = makeSetInserts(t.id, {ProcessorRef{"builtin.gain", {{"gainDb", 1000.0}}, ""}})->apply(p);
    REQUIRE_FALSE(loud.ok());
    REQUIRE(loud.error->code == "bad_value");

    const auto unknown = makeSetInserts(t.id, {ProcessorRef{"builtin.gain", {{"nonsense", 1.0}}, ""}})->apply(p);
    REQUIRE_FALSE(unknown.ok());
    REQUIRE(unknown.error->code == "bad_value");

    REQUIRE(makeSetInserts(t.id, {ProcessorRef{"builtin.gain", {{"gainDb", -6.0}}, ""}})->apply(p).ok());
}

// ---------------------------------------------------------------- 5: track limit

TEST_CASE("commands: the track limit is enforced with a structured error", "[review][commands]") {
    Project p = baseProject();  // the master counts
    for (int i = 0; i < kMaxTracks - 1; ++i) REQUIRE(makeAddTrack(makeTrack(100 + static_cast<std::uint64_t>(i), TrackKind::Audio))->apply(p).ok());
    const auto res = makeAddTrack(makeTrack(5000, TrackKind::Audio))->apply(p);
    REQUIRE_FALSE(res.ok());
    REQUIRE(res.error->code == "limit");
    REQUIRE(p.tracks.size() == static_cast<std::size_t>(kMaxTracks));
}

// ---------------------------------------------------------------- 6: notes held while the config changes

TEST_CASE("graph: a held note is released when the track config is replaced", "[review][graph]") {
    constexpr double kSr = 48000.0;
    RenderGraph g(kSr);
    auto add = [&](std::uint64_t n, TrackKind kind, TrackConfig* cfg) {
        AudioMsg m;
        m.kind = MsgKind::AddTrack;
        m.track = id(n);
        m.obj = makeOwned(new TrackNode(id(n), kind, StripParams{}, cfg, kSr));
        REQUIRE(g.apply(m).ptr == nullptr);
    };
    auto notes = [](std::int64_t on, std::int64_t off) {
        auto* cfg = new TrackConfig;
        RegionPlayback r;
        r.startFrame = 0;
        r.endFrame = 1'000'000;
        r.notes.push_back(NoteSpan{on, off, 69, 127});
        cfg->regions.push_back(std::move(r));
        return cfg;
    };
    add(100, TrackKind::Master, new TrackConfig);
    add(2, TrackKind::Instrument, notes(0, 500'000));

    std::vector<float> l(4800), r(4800);
    g.render(0, 480, l.data(), r.data());  // the note is sounding
    REQUIRE(*std::max_element(l.begin(), l.begin() + 480) > 0.01f);

    AudioMsg replace;
    replace.kind = MsgKind::SetConfig;
    replace.track = id(2);
    replace.obj = makeOwned(notes(0, 100));  // its note-off is already behind the playhead
    g.apply(replace).destroy();

    float peak = 0.0f;
    for (int pos = 480; pos < 4800; pos += 240) {  // plenty of time for a 5 ms release
        g.render(pos, 240, l.data(), r.data());
        if (pos >= 2400) for (int i = 0; i < 240; ++i) peak = std::max(peak, std::abs(l[static_cast<std::size_t>(i)]));
    }
    REQUIRE(peak == 0.0f);
}

// ---------------------------------------------------------------- 7: one streaming source, several playback positions

TEST_CASE("streaming source: two playback positions in the same file do not evict each other", "[review][streaming]") {
    test::TempDir tmp;
    constexpr int kFrames = 1'000'000;
    std::vector<float> samples(static_cast<std::size_t>(kFrames) * 2, 0.1f);
    writeWav(tmp.path / "x.wav", 48000, 2, samples, WavFormat::Float32);
    StreamingSource src(tmp.path / "x.wav", /*startReaderThread=*/false);

    std::vector<float> l(512), r(512);
    std::int64_t a = 0, b = 40 * StreamingSource::kChunkFrames;
    for (int i = 0; i < 4; ++i) {  // warm up: both positions get their window loaded
        src.read(a, l.data(), r.data(), 512);
        src.read(b, l.data(), r.data(), 512);
        src.pumpOnce();
    }
    src.takeUnderruns();
    for (int i = 0; i < 100; ++i) {
        a += 512;
        b += 512;
        src.read(a, l.data(), r.data(), 512);
        src.read(b, l.data(), r.data(), 512);
        src.pumpOnce();
    }
    REQUIRE(src.takeUnderruns() == 0);
}

// ---------------------------------------------------------------- 9: bursts of messages never leak garbage

TEST_CASE("engine: a burst of config swaps cannot overflow the feedback queue", "[review][engine]") {
    AudioEngine engine(48000.0);
    AudioMsg add;
    add.kind = MsgKind::AddTrack;
    add.track = id(2);
    add.obj = makeOwned(new TrackNode(id(2), TrackKind::Audio, StripParams{}, new TrackConfig, 48000.0));
    engine.applyDirect(add);

    constexpr int kBurst = 6000;
    for (int i = 1; i <= kBurst; ++i) {
        AudioMsg m;
        m.kind = MsgKind::SetConfig;
        m.seq = static_cast<std::uint64_t>(i);
        m.track = id(2);
        m.obj = makeOwned(new TrackConfig);
        REQUIRE(engine.postMessage(m));
    }
    std::vector<float> l(256), r(256);
    for (int block = 0; block < 20; ++block) engine.processBlock(l.data(), r.data(), 256);  // nobody collects garbage meanwhile
    REQUIRE(engine.garbageOverflow() == 0);

    for (int block = 0; block < 200 && engine.appliedSeq() < kBurst; ++block) {
        engine.collectGarbage();
        engine.processBlock(l.data(), r.data(), 256);
    }
    REQUIRE(engine.appliedSeq() == kBurst);  // nothing was lost, only deferred
    REQUIRE(engine.garbageOverflow() == 0);
    engine.collectGarbage();
}

// ---------------------------------------------------------------- 10: a stalled device must not freeze the project thread

TEST_CASE("host: a device that stops draining does not block reads, and the graph recovers", "[review][host][threads]") {
    std::mt19937_64 rng{5};
    Project initial{Uuid::random(rng)};
    MediaStore media;
    AudioEngine engine{48000.0};
    ProjectHost host(initial, engine, media);

    Track t = makeTrack(2, TrackKind::Audio);
    REQUIRE_FALSE(host.submit(makeAddTrack(t)).get().has_value());
    for (int i = 0; i < static_cast<int>(kMessageQueueCapacity) + 200; ++i) {
        StripPatch patch;
        patch.gainDb = (i % 2) ? -3.0f : -6.0f;
        (void)host.submit(makeSetStrip(t.id, patch));  // nobody calls processBlock: the queue fills up
    }
    auto snapshot = host.read([](const Project& p) { return p; });
    REQUIRE(snapshot.wait_for(std::chrono::seconds(20)) == std::future_status::ready);

    Project finalProject;
    {
        test::ThreadedDevice device(engine);  // the device comes back
        StripPatch last;
        last.gainDb = -12.0f;
        REQUIRE_FALSE(host.submit(makeSetStrip(t.id, last)).get().has_value());
        for (int i = 0; i < 3000 && host.degraded(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        REQUIRE_FALSE(host.degraded());  // the graph was rebuilt once the queue had room
        for (int i = 0; i < 3000 && engine.appliedSeq() < host.lastPostedSeq(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        REQUIRE(engine.appliedSeq() >= host.lastPostedSeq());
        device.stop();
        finalProject = host.read([](const Project& p) { return p; }).get();
    }
    RenderGraph fresh(48000.0);
    for (AudioMsg& m : initialMessages(finalProject, media)) fresh.apply(m).destroy();
    REQUIRE(engine.describeForTest() == fresh.describe());
}
