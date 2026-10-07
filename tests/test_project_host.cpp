#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_set>
#include "lpc/audio/engine.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "lpc/project_host.h"
#include "lpc/project_io.h"
#include "random_commands.h"
#include "temp_dir.h"
#include "threaded_device.h"

using namespace lpc;
using namespace lpc::audio;

namespace {

Track track(std::mt19937_64& rng, TrackKind kind, const char* name) {
    Track t;
    t.id = Uuid::random(rng);
    t.kind = kind;
    t.name = name;
    if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{"builtin.sine", {}, ""};
    return t;
}

// Waits until the audio thread has applied everything the host has posted.
bool settle(AudioEngine& engine, const ProjectHost& host) {
    for (int i = 0; i < 3000; ++i) {
        if (engine.appliedSeq() >= host.lastPostedSeq()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool waitFor(const std::function<bool()>& cond) {
    for (int i = 0; i < 3000; ++i) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// What a graph built from scratch for `p` looks like.
nlohmann::json freshDescription(const Project& p, MediaStore& media) {
    RenderGraph g(static_cast<double>(p.sampleRate));
    for (AudioMsg& m : initialMessages(p, media)) g.apply(m).destroy();
    return g.describe();
}

struct Fixture {
    std::mt19937_64 rng{11};
    Project initial{Uuid::random(rng)};
    MediaStore media;
    MediaItem item{Uuid::random(rng), "audio/x.wav", "h", 48000, 2, 48000};
    AudioEngine engine{48000.0};

    Fixture() {
        media.registerSource(item.id, std::make_shared<MemorySource>(48000, 2, std::vector<float>(2 * 48000, 0.25f)));
        REQUIRE(makeAddMedia(item)->apply(initial).ok());
    }
};

}  // namespace

TEST_CASE("host: commands reach the audio graph and it matches a fresh build", "[host][threads]") {
    test::rt::reset();
    Fixture f;
    test::ThreadedDevice device(f.engine);
    Project finalProject;
    {
        ProjectHost host(f.initial, f.engine, f.media);
        const Track bus = track(f.rng, TrackKind::Bus, "Bus");
        Track audio = track(f.rng, TrackKind::Audio, "Audio");
        audio.strip.output = bus.id;
        Region r;
        r.id = Uuid::random(f.rng);
        r.length = 4 * kPPQ;
        r.mediaId = f.item.id;

        REQUIRE_FALSE(host.submit(makeAddTrack(bus)).get().has_value());
        REQUIRE_FALSE(host.submit(makeAddTrack(audio)).get().has_value());
        REQUIRE_FALSE(host.submit(makeAddRegion(audio.id, r)).get().has_value());
        StripPatch patch;
        patch.gainDb = -6.0f;
        REQUIRE_FALSE(host.submit(makeSetStrip(audio.id, patch)).get().has_value());
        REQUIRE_FALSE(host.submit(makeSetTempo(kPPQ, 90.0)).get().has_value());

        REQUIRE(settle(f.engine, host));
        device.stop();
        finalProject = host.read([](const Project& p) { return p; }).get();
    }
    REQUIRE(f.engine.describeForTest() == freshDescription(finalProject, f.media));
    REQUIRE(test::rt::violations() == 0);
}

TEST_CASE("host: rejected commands report their error and change nothing", "[host][threads]") {
    Fixture f;
    test::ThreadedDevice device(f.engine);
    ProjectHost host(f.initial, f.engine, f.media);
    const auto err = host.submit(makeRemoveTrack(Uuid::random(f.rng))).get();
    REQUIRE(err.has_value());
    REQUIRE(err->code == "not_found");
    REQUIRE(host.read([](const Project& p) { return p; }).get() == f.initial);
    REQUIRE(host.undo().get()->code == "nothing_to_undo");
}

TEST_CASE("host: undo and redo reach the audio graph", "[host][threads]") {
    Fixture f;
    test::ThreadedDevice device(f.engine);
    ProjectHost host(f.initial, f.engine, f.media);
    const Track a = track(f.rng, TrackKind::Audio, "A");
    REQUIRE_FALSE(host.submit(makeAddTrack(a)).get().has_value());
    REQUIRE(settle(f.engine, host));
    REQUIRE_FALSE(host.undo().get().has_value());
    REQUIRE(settle(f.engine, host));
    device.stop();
    REQUIRE(f.engine.describeForTest()["tracks"].size() == 1);  // only the master is left

    test::ThreadedDevice again(f.engine);  // restart the stand-in device for the redo
    REQUIRE_FALSE(host.redo().get().has_value());
    REQUIRE(settle(f.engine, host));
    again.stop();
    REQUIRE(f.engine.describeForTest()["tracks"].size() == 2);
}

TEST_CASE("host: transport commands move the playhead", "[host][threads]") {
    Fixture f;
    test::ThreadedDevice device(f.engine);
    ProjectHost host(f.initial, f.engine, f.media);
    host.locate(1000).get();
    host.play().get();
    REQUIRE(waitFor([&] { return f.engine.playing() && f.engine.positionFrames() > 1000; }));
    host.stop().get();
    REQUIRE(waitFor([&] { return !f.engine.playing(); }));
    const auto stoppedAt = f.engine.positionFrames();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(f.engine.positionFrames() == stoppedAt);
}

TEST_CASE("host: a snapshot can be saved while audio runs and reloads equal", "[host][threads][io]") {
    Fixture f;
    test::ThreadedDevice device(f.engine);
    ProjectHost host(f.initial, f.engine, f.media);
    REQUIRE_FALSE(host.submit(makeAddTrack(track(f.rng, TrackKind::Instrument, "Keys"))).get().has_value());
    const Project snapshot = host.read([](const Project& p) { return p; }).get();
    test::TempDir tmp;
    saveProject(snapshot, tmp.path / "live.lpc");  // runs on this thread: the project thread is not blocked by I/O
    REQUIRE(loadProject(tmp.path / "live.lpc") == snapshot);
}

TEST_CASE("host: many random commands under load stay consistent", "[host][threads][property]") {
    test::rt::reset();
    Fixture f;
    test::ThreadedDevice device(f.engine);
    Project finalProject;
    {
        ProjectHost host(f.initial, f.engine, f.media);
        std::mt19937_64 rng(77);
        std::unordered_set<Uuid> registered{f.item.id};  // register each media id once: a second registration would change the pointer
        int accepted = 0;
        for (int i = 0; i < 300; ++i) {
            const Project current = host.read([](const Project& p) { return p; }).get();
            for (const MediaItem& m : current.mediaPool)
                if (registered.insert(m.id).second) f.media.registerSource(m.id, std::make_shared<MemorySource>(48000, 2, std::vector<float>(2 * 4800, 0.1f)));
            CommandPtr c = test::randomCommand(current, rng);
            if (!c) continue;
            if (!host.submit(std::move(c)).get().has_value()) ++accepted;
            if (i % 9 == 0) host.undo().get();
        }
        REQUIRE(accepted > 40);
        REQUIRE(settle(f.engine, host));
        device.stop();
        finalProject = host.read([](const Project& p) { return p; }).get();
    }
    REQUIRE(f.engine.describeForTest() == freshDescription(finalProject, f.media));
    REQUIRE(test::rt::violations() == 0);
}

TEST_CASE("host: the change listener runs once per accepted change, with growing revisions", "[host][threads]") {
    Fixture f;
    test::ThreadedDevice device(f.engine);
    ProjectHost host(f.initial, f.engine, f.media);
    std::mutex m;
    std::vector<std::uint64_t> seen;
    host.setChangeListener([&](std::uint64_t rev) {
        std::lock_guard lock(m);
        seen.push_back(rev);
    }).get();

    const Track a = track(f.rng, TrackKind::Audio, "A");
    REQUIRE_FALSE(host.submit(makeAddTrack(a)).get().has_value());
    REQUIRE(host.submit(makeRemoveTrack(Uuid::random(f.rng))).get().has_value());  // rejected: no call
    REQUIRE_FALSE(host.undo().get().has_value());
    REQUIRE_FALSE(host.redo().get().has_value());
    host.read([](const Project&) { return 0; }).get();  // queue drained

    std::lock_guard lock(m);
    REQUIRE(seen == std::vector<std::uint64_t>{1, 2, 3});
    REQUIRE(host.revision() == 3);
}
