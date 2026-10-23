#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <random>

#include "lpc/automation.h"
#include "lpc/commands.h"
#include "lpc/model_json.h"

using namespace lpc;

namespace {
std::mt19937_64 gRng(4242);

struct Setup {
    Project p{Uuid::random(gRng)};
    Track track;
    Region region;
    Setup() {
        track.id = Uuid::random(gRng);
        track.kind = TrackKind::Instrument;
        track.name = "Keys";
        track.instrument = ProcessorRef{"builtin.sine", {}, ""};
        REQUIRE(makeAddTrack(track)->apply(p).ok());
        region.id = Uuid::random(gRng);
        region.start = 0;
        region.length = 3840;  // four beats
        REQUIRE(makeAddRegion(track.id, region)->apply(p).ok());
    }
    const Track& t() const { return *p.findTrack(track.id); }
};
}  // namespace

TEST_CASE("region automation: a lane is set on the region, saved and undone", "[region-automation]") {
    Setup s;
    auto r = makeSetRegionAutomation(s.region.id, "volume", {{0, -12.0, 0.0}, {3840, 0.0, 0.0}})->apply(s.p);
    REQUIRE(r.ok());
    REQUIRE(s.t().regions[0].automation.size() == 1);
    REQUIRE(s.t().automation.empty());  // the track's own lanes are untouched
    const Region saved = nlohmann::json(s.t().regions[0]).get<Region>();
    REQUIRE(saved == s.t().regions[0]);
    REQUIRE(r.inverse->apply(s.p).ok());
    REQUIRE(s.t().regions[0].automation.empty());
    REQUIRE(!makeSetRegionAutomation(s.region.id, "nonsense", {{0, 0.0, 0.0}})->apply(s.p).ok());
}

TEST_CASE("region automation: cutting a lane keeps the values", "[region-automation]") {
    const std::vector<AutomationPoint> lane{{0, -12.0, 0.0}, {3840, 0.0, 0.0}};
    const auto head = cutAutomation(lane, 0, 1920);
    REQUIRE(head.size() == 2);
    REQUIRE(head.back().tick == 1920);
    REQUIRE(head.back().value == Catch::Approx(-6.0));
    const auto tail = cutAutomation(lane, 1920, 3840);
    REQUIRE(tail.front().value == Catch::Approx(-6.0));
    REQUIRE(tail.back().value == Catch::Approx(0.0));
}

TEST_CASE("region automation: a split gives each part its span, a join puts them back", "[region-automation]") {
    Setup s;
    REQUIRE(makeSetRegionAutomation(s.region.id, "volume", {{0, -12.0, 0.0}, {3840, 0.0, 0.0}})->apply(s.p).ok());
    const Uuid right = Uuid::random(gRng);
    REQUIRE(makeSplitRegion(s.region.id, 1920, right)->apply(s.p).ok());
    REQUIRE(s.t().regions.size() == 2);
    REQUIRE(s.t().regions[0].automation[0].points.back().value == Catch::Approx(-6.0));
    REQUIRE(s.t().regions[1].automation[0].points.front().value == Catch::Approx(-6.0));
    REQUIRE(s.t().regions[1].automation[0].points.front().tick == 0);
    REQUIRE(makeJoinRegions({s.region.id, right})->apply(s.p).ok());
    REQUIRE(s.t().regions.size() == 1);
    const auto& pts = s.t().regions[0].automation[0].points;
    REQUIRE(pts.front().value == Catch::Approx(-12.0));
    REQUIRE(pts.back().value == Catch::Approx(0.0));
    REQUIRE(pts.back().tick == 3840);
}

TEST_CASE("region automation: trimming the start moves the lane with it", "[region-automation]") {
    Setup s;
    REQUIRE(makeSetRegionAutomation(s.region.id, "pan", {{0, -1.0, 0.0}, {3840, 1.0, 0.0}})->apply(s.p).ok());
    REQUIRE(makeResizeRegion(s.region.id, 1920, 1920)->apply(s.p).ok());
    const auto& pts = s.t().regions[0].automation[0].points;
    REQUIRE(pts.front().value == Catch::Approx(0.0));
    REQUIRE(pts.back().value == Catch::Approx(1.0));
}

TEST_CASE("region automation: the lanes a track plays follow the two project choices", "[region-automation]") {
    Setup s;
    s.p.findTrack(s.track.id)->strip.gainDb = -3.0f;
    REQUIRE(makeSetRegionAutomation(s.region.id, "volume", {{0, -12.0, 0.0}, {3840, 0.0, 0.0}})->apply(s.p).ok());
    const auto valueOf = [&](bool priority, bool preset, Ticks tick) {
        for (const AutomationLane& lane : overlayRegionLanes(s.p, s.t(), priority, preset))
            if (lane.target == "volume") return automationValueAt(lane.points, tick, 99.0);
        return 99.0;
    };
    REQUIRE(valueOf(true, false, 1920) == Catch::Approx(-6.0).margin(0.01));  // inside the region the region's lane plays
    REQUIRE(valueOf(true, false, 6000) == Catch::Approx(0.0));   // after it the last value holds
    REQUIRE(valueOf(true, true, 6000) == Catch::Approx(-3.0));   // or the fader's own value with the preset choice

    REQUIRE(makeSetAutomation(s.track.id, "volume", {{0, -20.0, 0.0}})->apply(s.p).ok());
    REQUIRE(valueOf(true, false, 1920) == Catch::Approx(-6.0).margin(0.01));    // the region wins over the track's lane ...
    REQUIRE(valueOf(true, false, 6000) == Catch::Approx(-20.0));   // ... and the track's lane takes over after it
    REQUIRE(valueOf(false, false, 1920) == Catch::Approx(-20.0));  // with priority off the track's lane stays
}

TEST_CASE("region automation: converting between track and region lanes is one undo step", "[region-automation]") {
    Setup s;
    REQUIRE(makeSetAutomation(s.track.id, "volume", {{0, -12.0, 0.0}, {3840, 0.0, 0.0}})->apply(s.p).ok());
    auto toRegion = makeConvertAutomation(s.track.id, true, {})->apply(s.p);
    REQUIRE(toRegion.ok());
    REQUIRE(s.t().automation.empty());
    REQUIRE(s.t().regions[0].automation.size() == 1);
    REQUIRE(toRegion.inverse->apply(s.p).ok());
    REQUIRE(s.t().automation.size() == 1);
    REQUIRE(s.t().regions[0].automation.empty());

    REQUIRE(makeConvertAutomation(s.track.id, true, {})->apply(s.p).ok());
    auto toTrack = makeConvertAutomation(s.track.id, false, {})->apply(s.p);
    REQUIRE(toTrack.ok());
    REQUIRE(s.t().regions[0].automation.empty());
    REQUIRE(s.t().automation.size() == 1);
    REQUIRE(automationValueAt(s.t().automation[0].points, 1920, 99.0) == Catch::Approx(-6.0).margin(0.01));
    REQUIRE(!makeConvertAutomation(s.track.id, false, {})->apply(s.p).ok());  // nothing left to convert
}

TEST_CASE("track on/off and record in regions are kept and undone", "[region-automation]") {
    Setup s;
    TrackPatch patch;
    patch.on = false;
    patch.recordInRegions = true;
    auto r = makeSetTrackProps(s.track.id, patch)->apply(s.p);
    REQUIRE(r.ok());
    REQUIRE(!s.t().on);
    REQUIRE(s.t().recordInRegions);
    const Track saved = nlohmann::json(s.t()).get<Track>();
    REQUIRE(!saved.on);
    REQUIRE(saved.recordInRegions);
    REQUIRE(r.inverse->apply(s.p).ok());
    REQUIRE(s.t().on);
    REQUIRE(!s.t().recordInRegions);
}

#include "lpc/audio/frame_source.h"
#include "lpc/audio/sampler.h"
#include "lpc/media_store.h"
#include "lpc/sampler.h"
#include "lpc/processor_ids.h"

TEST_CASE("sampler: the zones are kept in the state and a note plays the zone that covers it", "[sampler]") {
    SamplerZone zone;
    zone.mediaId = Uuid::random(gRng);
    zone.name = "Kick";
    zone.root = 60;
    zone.lo = 60;
    zone.hi = 62;
    zone.oneShot = true;
    ProcessorRef ref{kProcSampler, {}, samplerStateOf({zone}), "", false};
    const auto back = samplerZonesOf(ref);
    REQUIRE(back.size() == 1);
    REQUIRE(back[0] == zone);
    REQUIRE(samplerMediaIds(ref).size() == 1);
    REQUIRE(checkSamplerZones(back).empty());
    zone.lo = 70;
    REQUIRE(!checkSamplerZones({zone}).empty());  // low above high
}

#include <filesystem>
#include "lpc/offline_render.h"
#include "lpc/wav.h"

TEST_CASE("sampler: a note plays the sound of its zone, a key outside every zone is silent", "[sampler]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "jad_sampler_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "audio");
    WavData wav;
    wav.sampleRate = 48000;
    wav.channels = 2;
    for (int i = 0; i < 24000; ++i) {
        const float v = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48000.0f);
        wav.samples.push_back(v);
        wav.samples.push_back(v);
    }
    writeWav(dir / "audio" / "tone.wav", wav.sampleRate, wav.channels, wav.samples);
    Project p{Uuid::random(gRng)};
    MediaItem item{Uuid::random(gRng), "audio/tone.wav", "h", 48000, 2, 24000};
    REQUIRE(makeAddMedia(item)->apply(p).ok());
    SamplerZone zone;
    zone.mediaId = item.id;
    zone.name = "Tone";
    zone.root = 60;
    zone.lo = 60;
    zone.hi = 60;
    Track t;
    t.id = Uuid::random(gRng);
    t.kind = TrackKind::Instrument;
    t.name = "Sampler";
    t.instrument = ProcessorRef{kProcSampler, {}, samplerStateOf({zone}), "", false};
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    auto peakOf = [&](int note) {
        Project q = p;
        Region r;
        r.id = Uuid::random(gRng);
        r.start = 0;
        r.length = 960;
        r.notes.push_back(MidiNote{0, 480, static_cast<std::uint8_t>(note), 100, false});
        REQUIRE(makeAddRegion(t.id, r)->apply(q).ok());
        MediaStore media(dir, false);
        RenderOptions o;
        o.tailSeconds = 0.3;
        const RenderResult res = renderOffline(q, media, o);
        float peak = 0.0f;
        for (const float v : res.interleaved) peak = std::max(peak, std::abs(v));
        return peak;
    };
    CHECK(peakOf(60) > 0.05f);
    CHECK(peakOf(72) < 1e-6f);
    std::filesystem::remove_all(dir);
}

#include "lpc/validation.h"

TEST_CASE("media paths: relative inside the project or the absolute path of a file kept elsewhere", "[media-path]") {
    CHECK(validMediaPath("audio/kick.wav"));
    CHECK(validMediaPath("C:/Samples/kick.wav"));
    CHECK(validMediaPath("/Users/me/Music/kick.wav"));
    CHECK(!validMediaPath("../kick.wav"));
    CHECK(!validMediaPath("audio/../../kick.wav"));
    CHECK(!validMediaPath("C:/Samples/../kick.wav"));
    CHECK(!validMediaPath("C:\Samples\kick.wav"));
    CHECK(!validMediaPath("//server/share/kick.wav"));
    CHECK(!validMediaPath(""));
    CHECK(isExternalMediaPath("C:/a.wav"));
    CHECK(isExternalMediaPath("/a.wav"));
    CHECK(!isExternalMediaPath("audio/a.wav"));
}

TEST_CASE("media paths: Locate points a media item at another file and undo puts it back", "[media-path]") {
    Project p{Uuid::random(gRng)};
    MediaItem item{Uuid::random(gRng), "audio/a.wav", "h", 48000, 2, 480000};
    REQUIRE(makeAddMedia(item)->apply(p).ok());
    auto r = makeSetMediaPath(item.id, "D:/Moved/a.wav")->apply(p);
    REQUIRE(r.ok());
    REQUIRE(p.findMedia(item.id)->path == "D:/Moved/a.wav");
    REQUIRE(r.inverse->apply(p).ok());
    REQUIRE(p.findMedia(item.id)->path == "audio/a.wav");
    REQUIRE(!makeSetMediaPath(item.id, "../x.wav")->apply(p).ok());
}

TEST_CASE("side chain: an insert can listen to another track, the project keeps it and undo takes it away", "[side-chain]") {
    Project p{Uuid::random(gRng)};
    Track a, b;
    a.id = Uuid::random(gRng); a.kind = TrackKind::Audio; a.name = "Drums";
    b.id = Uuid::random(gRng); b.kind = TrackKind::Audio; b.name = "Bass";
    b.strip.inserts.push_back(ProcessorRef{"builtin.compressor", {}, "", "", false});
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    REQUIRE(makeAddTrack(b)->apply(p).ok());
    auto r = makeSetInsertSideChain(b.id, 0, a.id)->apply(p);
    REQUIRE(r.ok());
    REQUIRE(p.findTrack(b.id)->strip.inserts[0].sideChain == a.id);
    const Track saved = nlohmann::json(*p.findTrack(b.id)).get<Track>();
    REQUIRE(saved.strip.inserts[0].sideChain == a.id);
    REQUIRE(!makeSetInsertSideChain(b.id, 0, b.id)->apply(p).ok());  // not itself
    REQUIRE(r.inverse->apply(p).ok());
    REQUIRE(p.findTrack(b.id)->strip.inserts[0].sideChain.isNull());
}
