#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <random>

#include "lpc/commands.h"
#include "lpc/live_loops.h"
#include "lpc/model_json.h"
#include "lpc/offline_render.h"

using namespace lpc;

namespace {
std::mt19937_64 gRng(77);

Track instrumentTrack() {
    Track t;
    t.id = Uuid::random(gRng);
    t.kind = TrackKind::Instrument;
    t.name = "Keys";
    t.instrument = ProcessorRef{"builtin.sine", {}, "", "", false};
    return t;
}

Region noteRegion(Ticks start, Ticks length) {
    Region r;
    r.id = Uuid::random(gRng);
    r.start = start;
    r.length = length;
    r.notes.push_back(MidiNote{0, 240, 72, 100, false});
    return r;
}

LiveCell cellWith(int scene) {
    LiveCell c;
    c.id = Uuid::random(gRng);
    c.scene = scene;
    c.region = noteRegion(0, 960);
    c.region.loopLength = 960;
    return c;
}

float peak(const RenderResult& res, std::size_t fromFrame, std::size_t toFrame) {
    float p = 0.0f;
    for (std::size_t i = fromFrame * 2; i < toFrame * 2 && i < res.interleaved.size(); ++i) p = std::max(p, std::abs(res.interleaved[i]));
    return p;
}
}  // namespace

TEST_CASE("live loops: cells are kept in the project and saved", "[live]") {
    Project p{Uuid::random(gRng)};
    Track t = instrumentTrack();
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    const LiveCell a = cellWith(0), b = cellWith(2);
    auto result = makeSetLiveCells(t.id, {a, b})->apply(p);
    REQUIRE(result.ok());
    REQUIRE(p.findTrack(t.id)->cells.size() == 2);
    const Project back = projectFromJson(toJson(p));
    REQUIRE(back.findTrack(t.id)->cells == p.findTrack(t.id)->cells);
    REQUIRE(result.inverse->apply(p).ok());  // undo
    REQUIRE(p.findTrack(t.id)->cells.empty());
}

TEST_CASE("live loops: a cell needs its own scene, a start at tick 0 and a region id of its own", "[live]") {
    Project p{Uuid::random(gRng)};
    Track t = instrumentTrack();
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    LiveCell a = cellWith(1), b = cellWith(1);
    CHECK(!makeSetLiveCells(t.id, {a, b})->apply(p).ok());  // two cells in one scene
    b = cellWith(2);
    b.region.id = a.region.id;
    CHECK(!makeSetLiveCells(t.id, {a, b})->apply(p).ok());  // the same region id
    b = cellWith(2);
    b.region.start = 480;
    CHECK(!makeSetLiveCells(t.id, {a, b})->apply(p).ok());
    CHECK(makeSetLiveCells(t.id, {a, cellWith(2)})->apply(p).ok());
}

TEST_CASE("live loops: the first empty scene and the launch tick", "[live]") {
    Project p{Uuid::random(gRng)};
    Track t = instrumentTrack();
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    CHECK(firstEmptyScene(p) == 0);
    REQUIRE(makeSetLiveCells(t.id, {cellWith(0), cellWith(1), cellWith(3)})->apply(p).ok());
    CHECK(firstEmptyScene(p) == 2);
    CHECK(launchTick(0, 3840) == 0);
    CHECK(launchTick(1, 3840) == 3840);
    CHECK(launchTick(3840, 3840) == 3840);
    CHECK(launchTick(5000, 3840) == 7680);
}

TEST_CASE("live loops: a cell is made from the regions of a window", "[live]") {
    Project p{Uuid::random(gRng)};
    Track t = instrumentTrack();
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    const Region a = noteRegion(960, 960), b = noteRegion(1920, 960);
    const auto cell = cellFromRegions(p, t, {&a, &b}, 960, 2880, Uuid::random(gRng));
    REQUIRE(cell);
    CHECK(cell->start == 0);
    CHECK(cell->length == 1920);
    CHECK(cell->loopLength == 1920);
    REQUIRE(cell->notes.size() == 2);
    CHECK(cell->notes[0].start == 0);
    CHECK(cell->notes[1].start == 960);
    CHECK(!cellFromRegions(p, t, {&a}, 4000, 5000, Uuid::random(gRng)));  // nothing there
}

TEST_CASE("live loops: a cell that is launched plays and loops, the track's own regions are silent meanwhile", "[live]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "jad_live_test";
    std::filesystem::create_directories(dir);
    Project p{Uuid::random(gRng)};
    Track t = instrumentTrack();
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    const LiveCell cell = cellWith(0);
    REQUIRE(makeSetLiveCells(t.id, {cell})->apply(p).ok());
    const std::int64_t beat = 24000;  // 120 bpm at 48 kHz
    RenderOptions o;
    o.frames = beat * 16;
    o.tailSeconds = 0.0;
    MediaStore media(dir, false);
    {
        const RenderResult res = renderOffline(p, media, o);
        CHECK(peak(res, 0, o.frames) < 1e-6f);  // not launched: silence
    }
    Project q = p;
    q.findTrack(t.id)->liveRuns.push_back(LiveRun{cell.id, 4 * 960, -1});  // from beat 4, until stopped
    const RenderResult res = renderOffline(q, media, o);
    CHECK(peak(res, 0, beat * 4 - 100) < 1e-6f);                   // before the launch
    CHECK(peak(res, beat * 4, beat * 5) > 0.05f);                   // the first pass
    CHECK(peak(res, beat * 8, beat * 8 + 5000) > 0.05f);            // and it loops
    Project r = p;
    r.findTrack(t.id)->liveRuns.push_back(LiveRun{cell.id, 0, 4 * 960});  // one bar only
    const RenderResult stopped = renderOffline(r, media, o);
    CHECK(peak(stopped, 0, 5000) > 0.05f);
    CHECK(peak(stopped, beat * 5, beat * 16) < 1e-6f);
    std::filesystem::remove_all(dir);
}
