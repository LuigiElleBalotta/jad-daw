#include <catch2/catch_test_macros.hpp>
#include <random>
#include "lpc/commands.h"
#include "lpc/model_json.h"
#include "lpc/validation.h"

using namespace lpc;

namespace {

std::mt19937_64 gRng(777);

struct Edit {
    Project p{Uuid::random(gRng)};
    Track audio, inst;
    MediaItem media{Uuid::random(gRng), "audio/a.wav", "h", 48000, 2, 480000};
    MediaItem media2{Uuid::random(gRng), "audio/b.wav", "h", 48000, 2, 480000};

    static Track track(TrackKind kind, const char* name) {
        Track t;
        t.id = Uuid::random(gRng);
        t.kind = kind;
        t.name = name;
        if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{"builtin.sine", {}, ""};
        return t;
    }
    Edit() {
        audio = track(TrackKind::Audio, "Audio");
        inst = track(TrackKind::Instrument, "Keys");
        REQUIRE(makeAddMedia(media)->apply(p).ok());
        REQUIRE(makeAddMedia(media2)->apply(p).ok());
        REQUIRE(makeAddTrack(audio)->apply(p).ok());
        REQUIRE(makeAddTrack(inst)->apply(p).ok());
    }
    // 120 bpm: one beat is 960 ticks and 24000 frames
    Region audioRegion(Ticks start, Ticks length, std::int64_t offset = 0) {
        Region r;
        r.id = Uuid::random(gRng);
        r.start = start;
        r.length = length;
        r.mediaId = media.id;
        r.sourceOffsetFrames = offset;
        return r;
    }
    Region midiRegion(Ticks start, Ticks length, std::vector<MidiNote> notes) {
        Region r;
        r.id = Uuid::random(gRng);
        r.start = start;
        r.length = length;
        r.notes = std::move(notes);
        return r;
    }
    void add(const Track& t, const Region& r) { REQUIRE(makeAddRegion(t.id, r)->apply(p).ok()); }
    const Region& get(const Uuid& id) {
        const Track* t = p.findTrackOfRegion(id);
        REQUIRE(t != nullptr);
        for (const Region& r : t->regions)
            if (r.id == id) return r;
        FAIL("region not found");
        return t->regions.front();
    }
};

}  // namespace

TEST_CASE("resize_region: trimming the left edge keeps the audio where it is; undo restores", "[commands][edit]") {
    Edit e;
    const Region r = e.audioRegion(4 * kPPQ, 4 * kPPQ);
    e.add(e.audio, r);
    const Project before = e.p;
    auto res = makeResizeRegion(r.id, 5 * kPPQ, 3 * kPPQ)->apply(e.p);
    REQUIRE(res.ok());
    REQUIRE(e.get(r.id).start == 5 * kPPQ);
    REQUIRE(e.get(r.id).length == 3 * kPPQ);
    REQUIRE(e.get(r.id).sourceOffsetFrames == 24000);  // one beat at 120 bpm
    REQUIRE(res.inverse->apply(e.p).ok());
    REQUIRE(e.p == before);
}

TEST_CASE("resize_region: the start cannot move before the beginning of the media", "[commands][edit]") {
    Edit e;
    const Region r = e.audioRegion(4 * kPPQ, 4 * kPPQ, 0);
    e.add(e.audio, r);
    const Project before = e.p;
    auto res = makeResizeRegion(r.id, 3 * kPPQ, 5 * kPPQ)->apply(e.p);
    REQUIRE_FALSE(res.ok());
    REQUIRE(res.error->code == "bad_region");
    REQUIRE(e.p == before);
}

TEST_CASE("resize_region: extending the right edge is allowed, bad sizes are rejected", "[commands][edit]") {
    Edit e;
    const Region r = e.audioRegion(0, 4 * kPPQ);
    e.add(e.audio, r);
    REQUIRE(makeResizeRegion(r.id, 0, 100 * kPPQ)->apply(e.p).ok());  // beyond the 10 s file: silence
    REQUIRE(e.get(r.id).sourceOffsetFrames == 0);
    const Project before = e.p;
    for (auto [start, length] : {std::pair<std::int64_t, std::int64_t>{0, 0}, {0, -5}, {-1, kPPQ}, {kMaxPosition + 1, kPPQ}, {0, kMaxPosition + 1}}) {
        auto bad = makeResizeRegion(r.id, start, length)->apply(e.p);
        REQUIRE_FALSE(bad.ok());
        REQUIRE(bad.error->code == "bad_region");
    }
    REQUIRE(makeResizeRegion(Uuid::random(gRng), 0, kPPQ)->apply(e.p).error->code == "not_found");
    REQUIRE(e.p == before);
}

TEST_CASE("resize_region: MIDI notes keep their position; notes outside go, straddling notes are clipped", "[commands][edit]") {
    Edit e;
    // region 0..4 beats; notes: before the new start, straddling it, inside, after the new end
    const Region r = e.midiRegion(0, 4 * kPPQ, {{0, 480, 60, 100}, {600, 960, 62, 100}, {960, 960, 64, 100}, {3000, 480, 65, 100}});
    e.add(e.inst, r);
    const Project before = e.p;
    auto res = makeResizeRegion(r.id, 960, 1920)->apply(e.p);  // new region: beats 1..3
    REQUIRE(res.ok());
    const Region& after = e.get(r.id);
    REQUIRE(after.notes.size() == 2);
    REQUIRE(after.notes[0] == MidiNote{0, 600, 62, 100});  // straddling note clipped at the new start
    REQUIRE(after.notes[1] == MidiNote{0, 960, 64, 100});  // starts exactly at the new start
    REQUIRE(res.inverse->apply(e.p).ok());
    REQUIRE(e.p == before);
}

TEST_CASE("resize_region: a MIDI note crossing the new right edge is clipped to the region; undo restores", "[commands][edit]") {
    Edit e;
    // region 0..4 beats; notes: inside, crossing the new right edge (beat 2), after it
    const Region r = e.midiRegion(0, 4 * kPPQ, {{0, 480, 60, 100}, {1800, 960, 62, 100}, {3000, 480, 65, 100}});
    e.add(e.inst, r);
    const Project before = e.p;
    auto res = makeResizeRegion(r.id, 0, 2 * kPPQ)->apply(e.p);  // new region: beats 0..2
    REQUIRE(res.ok());
    const Region& after = e.get(r.id);
    REQUIRE(after.notes.size() == 2);
    for (const MidiNote& n : after.notes) {
        REQUIRE(n.start >= 0);
        REQUIRE(n.start + n.length <= after.length);
    }
    REQUIRE(after.notes[1] == MidiNote{1800, 120, 62, 100});  // clipped at the new end
    REQUIRE(res.inverse->apply(e.p).ok());
    REQUIRE(e.p == before);
}

TEST_CASE("split_region: audio splits into two continuous halves; undo restores", "[commands][edit]") {
    Edit e;
    const Region r = e.audioRegion(0, 8 * kPPQ);
    e.add(e.audio, r);
    const Project before = e.p;
    const Uuid right = Uuid::random(gRng);
    auto res = makeSplitRegion(r.id, 4 * kPPQ, right)->apply(e.p);
    REQUIRE(res.ok());
    REQUIRE(e.get(r.id).length == 4 * kPPQ);
    REQUIRE(e.get(right).start == 4 * kPPQ);
    REQUIRE(e.get(right).length == 4 * kPPQ);
    REQUIRE(e.get(right).sourceOffsetFrames == 96000);  // four beats
    const Track* t = e.p.findTrack(e.audio.id);
    REQUIRE(t->regions.size() == 2);
    REQUIRE(t->regions[0].id == r.id);  // the right part sits right after the left one
    REQUIRE(t->regions[1].id == right);
    REQUIRE(res.inverse->apply(e.p).ok());
    REQUIRE(e.p == before);
}

TEST_CASE("split_region: positions outside the region and reused ids are rejected", "[commands][edit]") {
    Edit e;
    const Region r = e.audioRegion(kPPQ, 4 * kPPQ);
    e.add(e.audio, r);
    const Project before = e.p;
    const Uuid fresh = Uuid::random(gRng);
    for (Ticks at : {Ticks{0}, r.start, r.start + r.length, r.start + r.length + 5, Ticks{-7}, kMaxPosition * 4}) {
        auto bad = makeSplitRegion(r.id, at, fresh)->apply(e.p);
        REQUIRE_FALSE(bad.ok());
        REQUIRE(bad.error->code == "bad_region");
    }
    REQUIRE(makeSplitRegion(r.id, 2 * kPPQ, r.id)->apply(e.p).error->code == "duplicate_id");
    REQUIRE(makeSplitRegion(r.id, 2 * kPPQ, Uuid{})->apply(e.p).error->code == "duplicate_id");
    REQUIRE(makeSplitRegion(Uuid::random(gRng), 2 * kPPQ, fresh)->apply(e.p).error->code == "not_found");
    REQUIRE(e.p == before);
}

TEST_CASE("split_region: MIDI notes are partitioned at the cut", "[commands][edit]") {
    Edit e;
    const Region r = e.midiRegion(0, 4 * kPPQ, {{0, 1920, 60, 100}, {960, 480, 62, 100}, {2000, 100, 64, 100}});
    e.add(e.inst, r);
    const Project before = e.p;
    const Uuid right = Uuid::random(gRng);
    auto res = makeSplitRegion(r.id, 960, right)->apply(e.p);
    REQUIRE(res.ok());
    REQUIRE(e.get(r.id).notes == std::vector<MidiNote>{{0, 960, 60, 100}});  // crossing note cut at the split
    REQUIRE(e.get(right).notes == std::vector<MidiNote>{{0, 480, 62, 100}, {1040, 100, 64, 100}});
    REQUIRE(res.inverse->apply(e.p).ok());
    REQUIRE(e.p == before);
}

TEST_CASE("join_regions: split then join gives back the original; undo walks back", "[commands][edit]") {
    Edit e;
    const Region r = e.audioRegion(0, 8 * kPPQ);
    e.add(e.audio, r);
    const Project original = e.p;
    const Uuid right = Uuid::random(gRng);
    REQUIRE(makeSplitRegion(r.id, 4 * kPPQ, right)->apply(e.p).ok());
    const Project split = e.p;
    auto joined = makeJoinRegions({right, r.id})->apply(e.p);  // order of the ids does not matter
    REQUIRE(joined.ok());
    REQUIRE(e.p == original);
    REQUIRE(joined.inverse->apply(e.p).ok());
    REQUIRE(e.p == split);
}

TEST_CASE("join_regions: only adjacent, compatible regions of one track join", "[commands][edit]") {
    Edit e;
    Region a = e.audioRegion(0, 4 * kPPQ, 0);  // non-const: the loop below mixes pointers to these regions
    Region gap = e.audioRegion(5 * kPPQ, 4 * kPPQ, 120000);  // a beat of gap after a
    Region otherMedia = e.audioRegion(4 * kPPQ, 2 * kPPQ, 96000);
    otherMedia.mediaId = e.media2.id;
    Region discontinuous = e.audioRegion(6 * kPPQ, 2 * kPPQ, 5);  // adjacent to otherMedia but not continuous
    Region loud = e.audioRegion(8 * kPPQ, 2 * kPPQ, 1);
    loud.gainDb = 3.0f;
    for (const Region* r : {&a, &gap, &otherMedia, &discontinuous, &loud}) e.add(e.audio, *r);
    const Region onOtherTrack = e.midiRegion(4 * kPPQ, kPPQ, {});
    e.add(e.inst, onOtherTrack);
    const Project before = e.p;

    REQUIRE(makeJoinRegions({a.id})->apply(e.p).error->code == "bad_value");
    REQUIRE(makeJoinRegions({a.id, a.id})->apply(e.p).error->code == "duplicate_id");
    REQUIRE(makeJoinRegions({a.id, gap.id})->apply(e.p).error->code == "bad_region");         // gap
    REQUIRE(makeJoinRegions({a.id, otherMedia.id})->apply(e.p).error->code == "bad_region");  // different media
    REQUIRE(makeJoinRegions({otherMedia.id, discontinuous.id})->apply(e.p).error->code == "bad_region");
    REQUIRE(makeJoinRegions({discontinuous.id, loud.id})->apply(e.p).error->code == "bad_region");  // different gain
    REQUIRE(makeJoinRegions({a.id, onOtherTrack.id})->apply(e.p).error->code == "bad_region");       // other track
    REQUIRE(makeJoinRegions({a.id, Uuid::random(gRng)})->apply(e.p).error->code == "not_found");
    REQUIRE(e.p == before);
}

TEST_CASE("join_regions: MIDI regions merge their notes", "[commands][edit]") {
    Edit e;
    const Region a = e.midiRegion(0, 2 * kPPQ, {{0, 480, 60, 100}});
    const Region b = e.midiRegion(2 * kPPQ, 2 * kPPQ, {{480, 480, 62, 100}});
    e.add(e.inst, a);
    e.add(e.inst, b);
    const Project before = e.p;
    auto res = makeJoinRegions({a.id, b.id})->apply(e.p);
    REQUIRE(res.ok());
    REQUIRE(e.get(a.id).length == 4 * kPPQ);
    REQUIRE(e.get(a.id).notes == std::vector<MidiNote>{{0, 480, 60, 100}, {2 * kPPQ + 480, 480, 62, 100}});
    REQUIRE(e.p.findTrack(e.inst.id)->regions.size() == 1);
    REQUIRE(res.inverse->apply(e.p).ok());
    REQUIRE(e.p == before);
}

TEST_CASE("replace_region: swaps a region by id after validating it", "[commands][edit]") {
    Edit e;
    const Region r = e.audioRegion(0, 4 * kPPQ);
    e.add(e.audio, r);
    const Project before = e.p;
    Region changed = r;
    changed.gainDb = -6.0f;
    auto res = makeReplaceRegion(changed)->apply(e.p);
    REQUIRE(res.ok());
    REQUIRE(e.get(r.id).gainDb == -6.0f);
    REQUIRE(res.inverse->apply(e.p).ok());
    REQUIRE(e.p == before);
    changed.gainDb = 500.0f;
    REQUIRE(makeReplaceRegion(changed)->apply(e.p).error->code == "bad_value");
    changed = r;
    changed.id = Uuid::random(gRng);
    REQUIRE(makeReplaceRegion(changed)->apply(e.p).error->code == "not_found");
    REQUIRE(e.p == before);
}

TEST_CASE("the new region commands survive a JSON round trip", "[commands][edit][json]") {
    std::vector<CommandPtr> cmds;
    Region r;
    r.id = Uuid::random(gRng);
    r.length = kPPQ;
    cmds.push_back(makeReplaceRegion(r));
    cmds.push_back(makeResizeRegion(Uuid::random(gRng), 10, 20));
    cmds.push_back(makeSplitRegion(Uuid::random(gRng), 30, Uuid::random(gRng)));
    cmds.push_back(makeJoinRegions({Uuid::random(gRng), Uuid::random(gRng)}));
    for (const CommandPtr& c : cmds) {
        const nlohmann::json j = c->toJson();
        REQUIRE(commandFromJson(j)->toJson() == j);
    }
}
