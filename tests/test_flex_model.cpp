#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <random>
#include "lpc/audio/engine.h"
#include "lpc/commands.h"
#include "lpc/flex_model.h"
#include "lpc/model_json.h"

using namespace lpc;

namespace {

std::mt19937_64 gRng(4242);

FlexData sampleFlex(const Uuid& media) {
    FlexData f;
    f.sourceMediaId = media;
    f.sourceOffset = 1000;
    f.sourceLength = 10000;
    f.markers = {{4000, 6000}};
    FlexNoteData n;
    n.start = 3000;
    n.length = 2000;
    n.pitch = 60.2;
    n.semitones = 2;
    f.notes = {n};
    return f;
}

// one audio track with a region of 4 beats (2 s at 120 bpm and 48 kHz = 96000 frames) that plays a rendered file made from another one
struct Fixture {
    Project p{Uuid::random(gRng)};
    MediaItem recorded{Uuid::random(gRng), "audio/recorded.wav", "h1", 48000, 1, 480000};
    MediaItem baked{Uuid::random(gRng), "audio/baked.wav", "h2", 48000, 1, 96000};
    Uuid trackId, regionId;

    Fixture() {
        REQUIRE(makeAddMedia(recorded)->apply(p).ok());
        REQUIRE(makeAddMedia(baked)->apply(p).ok());
        Track t;
        t.id = Uuid::random(gRng);
        t.kind = TrackKind::Audio;
        t.name = "Voice";
        trackId = t.id;
        REQUIRE(makeAddTrack(t)->apply(p).ok());
        Region r;
        r.id = Uuid::random(gRng);
        r.length = 4 * kPPQ;
        r.mediaId = baked.id;
        FlexData f;
        f.sourceMediaId = recorded.id;
        f.sourceOffset = 0;
        f.sourceLength = 96000;
        f.markers = {{48000, 60000}};
        r.flex = f;
        regionId = r.id;
        REQUIRE(makeAddRegion(trackId, r, -1)->apply(p).ok());
    }
    const Region& region(const Uuid& id) const {
        for (const Track& t : p.tracks)
            for (const Region& r : t.regions)
                if (r.id == id) return r;
        FAIL("no such region");
        return p.tracks[0].regions[0];
    }
};

}  // namespace

TEST_CASE("flex model: a window of the flex data keeps what lies in it, and two windows join back to the same map", "[flex][model]") {
    const FlexData f = sampleFlex(Uuid::random(gRng));
    const auto head = flexWindow(f, 10000, 0, 6000, 100000);
    const auto tail = flexWindow(f, 10000, 6000, 4000, 100000);
    REQUIRE(head);
    REQUIRE(tail);
    REQUIRE(head->sourceOffset == 1000);
    REQUIRE(head->sourceLength == 4000);   // the first 6000 frames played come from the first 4000 of the source
    REQUIRE(head->markers.empty());
    REQUIRE(head->notes.size() == 1);
    REQUIRE(head->notes[0].length == 1000);  // the note was cut by the end of the window
    REQUIRE(tail->sourceOffset == 5000);
    REQUIRE(tail->sourceLength == 6000);
    REQUIRE(tail->markers.empty());
    REQUIRE(tail->notes.size() == 1);
    REQUIRE(tail->notes[0].start == 0);
    REQUIRE(tail->notes[0].length == 1000);
    const auto joined = flexJoin(*head, 6000, *tail);
    REQUIRE(joined);
    REQUIRE(joined->sourceOffset == 1000);
    REQUIRE(joined->sourceLength == 10000);
    REQUIRE(joined->notes.size() == 2);
    const TimeMap before(10000, 10000, flexAnchors(f)), after(10000, 10000, flexAnchors(*joined));
    for (double dst : {100.0, 3000.0, 5999.0, 6000.0, 8000.0, 9900.0}) REQUIRE(after.toSource(dst) == Catch::Approx(before.toSource(dst)).margin(1.0));
    // a window that reaches outside the source media is refused; one that grows adds the sound around it 1:1
    REQUIRE_FALSE(flexWindow(f, 10000, -2000, 10000, 100000));
    const auto grown = flexWindow(f, 10000, -500, 11000, 100000);
    REQUIRE(grown);
    REQUIRE(grown->sourceOffset == 500);
    REQUIRE(grown->markers.size() == 1);
    REQUIRE(grown->markers[0].dst == 6500);
}

TEST_CASE("flex model: bad flex data is refused", "[flex][model]") {
    FlexData f = sampleFlex(Uuid::random(gRng));
    REQUIRE(flexProblem(f, 100000).empty());
    REQUIRE_FALSE(flexProblem(f, 5000).empty());  // beyond the end of the source
    FlexData unsorted = f;
    unsorted.markers = {{5000, 6000}, {4000, 7000}};
    REQUIRE_FALSE(flexProblem(unsorted, 100000).empty());
    FlexData wild = f;
    wild.notes[0].semitones = 100;
    REQUIRE_FALSE(flexProblem(wild, 100000).empty());
    FlexData nan = f;
    nan.notes[0].vibrato = std::nan("");
    REQUIRE_FALSE(flexProblem(nan, 100000).empty());
}

TEST_CASE("flex commands: split, resize and join carry the flex edits along, and undo restores them", "[flex][model]") {
    Fixture fx;
    const Region original = fx.region(fx.regionId);
    const Uuid rightId = Uuid::random(gRng);
    // cut in the middle (48000 frames in)
    auto split = makeSplitRegion(fx.regionId, 2 * kPPQ, rightId);
    ApplyResult r = split->apply(fx.p);
    REQUIRE(r.ok());
    const Region left = fx.region(fx.regionId), right = fx.region(rightId);
    REQUIRE(left.flex);
    REQUIRE(right.flex);
    REQUIRE(left.flex->sourceLength == 38400);                // 48000 frames of sound come from 38400 of the source (the marker stretches the first half)
    REQUIRE(right.flex->sourceOffset == 38400);
    REQUIRE(right.flex->sourceLength == 57600);
    REQUIRE(right.flex->markers.size() == 1);
    REQUIRE(right.flex->markers[0].src == 9600);
    REQUIRE(right.flex->markers[0].dst == 12000);
    REQUIRE(right.sourceOffsetFrames == 48000);               // the rendered file is read from where the cut is
    // join them again: one region, the same sound
    auto join = makeJoinRegions({fx.regionId, rightId});
    REQUIRE(join->apply(fx.p).ok());
    const Region joined = fx.region(fx.regionId);
    REQUIRE(joined.flex);
    REQUIRE(joined.flex->sourceLength == 96000);
    const TimeMap a(96000, 96000, flexAnchors(*original.flex)), b(96000, 96000, flexAnchors(*joined.flex));
    for (double dst : {1000.0, 30000.0, 47999.0, 48000.0, 70000.0}) REQUIRE(b.toSource(dst) == Catch::Approx(a.toSource(dst)).margin(1.0));
    // undo
    Fixture fy;
    const Project start = fy.p;
    ApplyResult s2 = makeSplitRegion(fy.regionId, 2 * kPPQ, rightId)->apply(fy.p);
    REQUIRE(s2.ok());
    REQUIRE(s2.inverse->apply(fy.p).ok());
    REQUIRE(fy.p == start);
    // the right edge pulled in: the flex edits keep only what still plays
    ApplyResult shrink = makeResizeRegion(fy.regionId, 0, 2 * kPPQ)->apply(fy.p);
    REQUIRE(shrink.ok());
    REQUIRE(fy.region(fy.regionId).flex->sourceLength == 38400);
    REQUIRE(shrink.inverse->apply(fy.p).ok());
    REQUIRE(fy.p == start);
    // the left edge pushed in
    ApplyResult trim = makeResizeRegion(fy.regionId, 2 * kPPQ, 2 * kPPQ)->apply(fy.p);
    REQUIRE(trim.ok());
    REQUIRE(fy.region(fy.regionId).flex->sourceOffset == 38400);
    REQUIRE(fy.region(fy.regionId).sourceOffsetFrames == 48000);
}

TEST_CASE("flex commands: the source media is kept, other regions cannot carry flex, and the data survives JSON", "[flex][model]") {
    Fixture fx;
    // the recorded file is only used by the flex data: it still counts as used
    ApplyResult remove = makeRemoveMedia(fx.recorded.id)->apply(fx.p);
    REQUIRE_FALSE(remove.ok());
    REQUIRE(remove.error->code == "in_use");
    // a region whose flex data names a media that is not there
    Region bad = fx.region(fx.regionId);
    bad.id = Uuid::random(gRng);
    bad.flex->sourceMediaId = Uuid::random(gRng);
    REQUIRE_FALSE(makeAddRegion(fx.trackId, bad, -1)->apply(fx.p).ok());
    // flex data reaching beyond the source
    Region far = fx.region(fx.regionId);
    far.id = Uuid::random(gRng);
    far.flex->sourceLength = 480001;
    REQUIRE_FALSE(makeAddRegion(fx.trackId, far, -1)->apply(fx.p).ok());
    // JSON
    const Region r = fx.region(fx.regionId);
    const nlohmann::json j = r;
    REQUIRE(j.contains("flex"));
    REQUIRE(j.get<Region>() == r);
    Region plain = r;
    plain.flex.reset();
    REQUIRE_FALSE(nlohmann::json(plain).contains("flex"));
    const nlohmann::json tj = [&] {
        Track t = fx.p.tracks[0];
        t.flexMode = "monophonic";
        t.flexParams = {{"decay", 0.3}};
        return nlohmann::json(t);
    }();
    const Track back = tj.get<Track>();
    REQUIRE(back.flexMode == "monophonic");
    REQUIRE(back.flexParams.at("decay") == Catch::Approx(0.3));
}

TEST_CASE("flex commands: a track can be flexed with a known mode only, and undo takes it back", "[flex][model]") {
    Fixture fx;
    TrackPatch patch;
    patch.flexMode = "polyphonic";
    patch.flexParams = std::map<std::string, double>{{"decay", 0.7}};
    ApplyResult set = makeSetTrackProps(fx.trackId, patch)->apply(fx.p);
    REQUIRE(set.ok());
    REQUIRE(fx.p.findTrack(fx.trackId)->flexMode == "polyphonic");
    REQUIRE(fx.p.findTrack(fx.trackId)->flexParams.at("decay") == Catch::Approx(0.7));
    REQUIRE(set.inverse->apply(fx.p).ok());
    REQUIRE(fx.p.findTrack(fx.trackId)->flexMode.empty());
    REQUIRE(fx.p.findTrack(fx.trackId)->flexParams.empty());
    TrackPatch nonsense;
    nonsense.flexMode = "wobbly";
    REQUIRE_FALSE(makeSetTrackProps(fx.trackId, nonsense)->apply(fx.p).ok());
    TrackPatch badParam;
    badParam.flexParams = std::map<std::string, double>{{"nope", 1.0}};
    REQUIRE_FALSE(makeSetTrackProps(fx.trackId, badParam)->apply(fx.p).ok());
    // and through the JSON form the UI uses
    const nlohmann::json cmd = {{"type", "set_track_props"}, {"trackId", fx.trackId}, {"flexMode", "pitch"}};
    REQUIRE(commandFromJson(cmd)->apply(fx.p).ok());
    REQUIRE(fx.p.findTrack(fx.trackId)->flexMode == "pitch");
}

TEST_CASE("engine: an audition clip is heard over silence, once or repeating, from any point, until it is stopped", "[engine][flex]") {
    using namespace lpc::audio;
    AudioEngine e(48000);
    auto clip = [](int frames, float value) {
        auto* c = new AuditionClip;
        c->left.assign(static_cast<std::size_t>(frames), value);
        c->right.assign(static_cast<std::size_t>(frames), -value);
        return c;
    };
    std::vector<float> l(256), r(256);
    e.processBlock(l.data(), r.data(), 256);
    REQUIRE(l[10] == 0.0f);
    REQUIRE(e.auditionPosition() == -1);

    AudioMsg m;
    m.kind = MsgKind::SetAudition;
    m.obj = makeOwned(clip(300, 0.25f));
    e.applyDirect(m);
    e.processBlock(l.data(), r.data(), 256);
    REQUIRE(l[0] == 0.25f);
    REQUIRE(r[255] == -0.25f);
    REQUIRE(e.auditionPosition() == 256);
    e.processBlock(l.data(), r.data(), 256);   // 44 frames left, then silence again
    REQUIRE(l[43] == 0.25f);
    REQUIRE(l[44] == 0.0f);
    REQUIRE(e.auditionPosition() == -1);

    m.obj = makeOwned(clip(100, 0.5f));
    m.frame = 90;      // from the 90th frame of the clip
    m.frame2 = 1;      // and again and again
    e.applyDirect(m);
    e.processBlock(l.data(), r.data(), 256);
    REQUIRE(l[0] == 0.5f);
    REQUIRE(l[255] == 0.5f);                   // still sounding after the clip's end: it repeats
    m.obj = {};
    m.frame = 0;
    m.frame2 = 0;
    e.applyDirect(m);                          // a null clip stops it
    e.processBlock(l.data(), r.data(), 256);
    REQUIRE(l[0] == 0.0f);
    REQUIRE(e.auditionPosition() == -1);
    e.collectGarbage();                        // the clips that were replaced go back to the project thread
}
