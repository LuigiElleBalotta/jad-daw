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

TEST_CASE("live loops: where a cell is after it has played for a while, with an intro, a loop and a one-shot", "[live]") {
    LiveCell c = cellWith(0);
    c.region.length = 3840;  // four beats of 960
    c.startPos = 0;
    c.loopStart = 960;
    c.loopLength = 1920;  // the loop is [960, 2880)
    const CellSpan s = cellSpan(c);
    CHECK(s.loopStart == 960);
    CHECK(s.loopEnd == 2880);
    CHECK(cellPositionAt(c, 0, 500) == 500);
    CHECK(cellPositionAt(c, 0, 2880) == 960);        // the first pass has run through the intro and the loop: back at the loop start
    CHECK(cellPositionAt(c, 0, 2880 + 480) == 1440);
    CHECK(cellPositionAt(c, 1920, 960) == 960);      // starting inside the loop, it wraps at the loop end
    CHECK(ticksToCellEnd(c, 0, 1000) == 1880);
    c.loop = false;
    bool finished = false;
    CHECK(cellPositionAt(c, 0, 1000, &finished) == 1000);
    CHECK(!finished);
    CHECK(cellPositionAt(c, 0, 5000, &finished) == 3840);
    CHECK(finished);
}

TEST_CASE("live loops: a run plays the intro once, then loops", "[live]") {
    Project p{Uuid::random(gRng)};
    Track t = instrumentTrack();
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    LiveCell c = cellWith(0);
    c.region.length = 3840;
    c.region.notes = {MidiNote{0, 240, 60, 100, false}, MidiNote{1000, 240, 64, 100, false}};
    c.loopStart = 960;
    c.loopLength = 1920;
    REQUIRE(makeSetLiveCells(t.id, {c})->apply(p).ok());
    LiveRun run{c.id, 7680, -1, 0};
    const std::vector<Region> pieces = playedRegions(p, *p.findTrack(t.id), run, c);
    REQUIRE(pieces.size() == 2);
    CHECK(pieces[0].start == 7680);  // the intro, once
    CHECK(pieces[0].length == 960);
    CHECK(pieces[0].loopLength == 0);
    CHECK(pieces[1].start == 7680 + 960);  // the loop follows
    CHECK(pieces[1].loopLength == 1920);
    CHECK(pieces[1].notes.size() == 1);    // only the note inside the loop window
    CHECK(pieces[1].notes[0].start == 40);
    run.end = 7680 + 960 + 500;  // stopped inside the first pass of the loop
    const std::vector<Region> cut = playedRegions(p, *p.findTrack(t.id), run, c);
    REQUIRE(cut.size() == 2);
    CHECK(cut[1].length == 500);
    LiveRun late{c.id, 0, -1, 2000};  // starting inside the loop: the rest of it, then the loop
    const std::vector<Region> rest = playedRegions(p, *p.findTrack(t.id), late, c);
    REQUIRE(rest.size() == 2);
    CHECK(rest[0].length == 880);
    CHECK(rest[1].start == 880);
}

TEST_CASE("live loops: the settings and the takes of a cell are checked and saved", "[live]") {
    Project p{Uuid::random(gRng)};
    Track t = instrumentTrack();
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    LiveCell c = cellWith(0);
    c.trigger = "retrigger";
    c.playFrom = "playhead";
    c.quantize = "2";
    c.qLoopStart = "onPreserve";
    c.loop = false;
    c.recording = "takes";
    c.takes.push_back({"Take 1", c.region});
    LiveTake second{"Take 2", c.region};
    second.region.id = Uuid::random(gRng);
    c.takes.push_back(second);
    c.activeTake = 1;
    REQUIRE(makeSetLiveCells(t.id, {c})->apply(p).ok());
    const Project back = projectFromJson(toJson(p));
    REQUIRE(back.findTrack(t.id)->cells.size() == 1);
    CHECK(back.findTrack(t.id)->cells[0] == c);
    LiveCell bad = c;
    bad.trigger = "bounce";
    CHECK(!makeSetLiveCells(t.id, {bad})->apply(p).ok());
    bad = c;
    bad.quantize = "0.001";
    CHECK(!makeSetLiveCells(t.id, {bad})->apply(p).ok());
    bad = c;
    bad.loopStart = 5000;
    CHECK(!makeSetLiveCells(t.id, {bad})->apply(p).ok());
    bad = c;
    bad.activeTake = 5;
    CHECK(!makeSetLiveCells(t.id, {bad})->apply(p).ok());
}

#include "lpc/validation.h"
#include "lpc/wav.h"

TEST_CASE("live loops: the grid of another project comes in with its audio and new ids", "[live]") {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "jad_live_import";
    std::filesystem::remove_all(root);
    const std::filesystem::path other = root / "other", mine = root / "mine";
    std::filesystem::create_directories(other / "audio");
    std::filesystem::create_directories(mine);
    std::vector<float> tone(4800, 0.25f);
    writeWav(other / "audio" / "loop.wav", 48000, 1, tone, WavFormat::Pcm16);
    Project src{Uuid::random(gRng)};
    MediaItem item{Uuid::random(gRng), "audio/loop.wav", "h", 48000, 1, 4800};
    REQUIRE(makeAddMedia(item)->apply(src).ok());
    Track audio;
    audio.id = Uuid::random(gRng);
    audio.kind = TrackKind::Audio;
    audio.name = "Loops";
    REQUIRE(makeAddTrack(audio)->apply(src).ok());
    LiveCell ac;
    ac.id = Uuid::random(gRng);
    ac.scene = 2;
    ac.region.id = Uuid::random(gRng);
    ac.region.length = 960;
    ac.region.mediaId = item.id;
    Track keys = instrumentTrack();
    REQUIRE(makeAddTrack(keys)->apply(src).ok());
    REQUIRE(makeSetLiveCells(audio.id, {ac})->apply(src).ok());
    REQUIRE(makeSetLiveCells(keys.id, {cellWith(0)})->apply(src).ok());
    Project dst{Uuid::random(gRng)};
    const ImportedGrid grid = importLiveLoopsGrid(dst, mine, src, other);
    REQUIRE(grid.tracks.size() == 2);
    REQUIRE(grid.media.size() == 1);
    CHECK(grid.media[0].id != item.id);
    CHECK(std::filesystem::exists(mine / "audio" / "loop.wav"));
    for (const MediaItem& m : grid.media) REQUIRE(makeAddMedia(m)->apply(dst).ok());
    for (const Track& t : grid.tracks) {
        CHECK(t.id != audio.id);
        REQUIRE(makeAddTrack(t)->apply(dst).ok());
    }
    CHECK(!checkProject(dst));
    CHECK(dst.tracks.size() == 3);  // the master and the two tracks
    std::filesystem::remove_all(root);
}

TEST_CASE("live loops: the loop of an audio cell is found from its beats", "[live]") {
    const int rate = 48000;
    std::vector<float> clicks(static_cast<std::size_t>(rate) * 17, 0.0f);  // 120 bpm for 17 s: a click every half second
    unsigned seed = 7;
    for (int beat = 0; beat < 34; ++beat)
        for (int i = 0; i < 600; ++i) {
            seed = seed * 1664525u + 1013904223u;
            clicks[static_cast<std::size_t>(beat) * rate / 2 + static_cast<std::size_t>(i)] = 0.8f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f) * std::exp(-static_cast<float>(i) / 150.0f);
        }
    WavData wav;
    wav.sampleRate = rate;
    wav.channels = 1;
    wav.samples = clicks;
    Project p{Uuid::random(gRng)};
    const auto loop = suggestLoop(wav, p);
    REQUIRE(loop);
    CHECK((loop->bars == 1 || loop->bars == 2 || loop->bars == 4 || loop->bars == 8));
    CHECK(loop->length == static_cast<Ticks>(loop->bars) * 4 * 960);
    CHECK(loop->start >= 0);
    WavData silent;
    silent.sampleRate = rate;
    silent.channels = 1;
    silent.samples.assign(static_cast<std::size_t>(rate) * 4, 0.0f);
    CHECK(!suggestLoop(silent, p));
}
