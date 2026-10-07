#include <catch2/catch_test_macros.hpp>
#include "lpc/commands.h"
#include "lpc/model_json.h"

using namespace lpc;

namespace {

std::mt19937_64 gRng(123);

struct Fixture {
    Project p{Uuid::random(gRng)};
    Track audio, midi, inst, bus, bus2;
    MediaItem media{Uuid::random(gRng), "audio/a.wav", "h", 48000, 2, 480000};

    static Track track(TrackKind kind, const char* name) {
        Track t;
        t.id = Uuid::random(gRng);
        t.kind = kind;
        t.name = name;
        if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{"builtin.sine", {}, ""};
        return t;
    }

    Fixture() {
        audio = track(TrackKind::Audio, "Audio");
        midi = track(TrackKind::Midi, "Midi");
        inst = track(TrackKind::Instrument, "Keys");
        bus = track(TrackKind::Bus, "Bus");
        bus2 = track(TrackKind::Aux, "Aux");
        REQUIRE(makeAddMedia(media)->apply(p).ok());
        for (const Track* t : {&audio, &midi, &inst, &bus, &bus2}) REQUIRE(makeAddTrack(*t)->apply(p).ok());
    }

    Region audioRegion() const {
        Region r;
        r.id = Uuid::random(gRng);
        r.start = 0;
        r.length = 4 * kPPQ;
        r.mediaId = media.id;
        return r;
    }
    static Region midiRegion() {
        Region r;
        r.id = Uuid::random(gRng);
        r.start = kPPQ;
        r.length = 4 * kPPQ;
        r.notes.push_back({0, kPPQ, 60, 100});
        return r;
    }
};

void requireRoundTrip(Project& p, const Command& cmd) {
    const Project before = p;
    ApplyResult r = cmd.apply(p);
    REQUIRE(r.ok());
    REQUIRE(r.inverse->apply(p).ok());
    REQUIRE(p == before);
    REQUIRE(cmd.apply(p).ok());
}

void requireRejected(Project& p, const Command& cmd, const char* code) {
    const Project before = p;
    const ApplyResult r = cmd.apply(p);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error->code == code);
    REQUIRE(p == before);
}

}  // namespace

TEST_CASE("regions: add, move and remove round-trip", "[commands][regions]") {
    Fixture f;
    const Region ar = f.audioRegion();
    const Region mr = Fixture::midiRegion();
    requireRoundTrip(f.p, *makeAddRegion(f.audio.id, ar));
    requireRoundTrip(f.p, *makeAddRegion(f.inst.id, mr));
    requireRoundTrip(f.p, *makeAddRegion(f.midi.id, Fixture::midiRegion()));
    requireRoundTrip(f.p, *makeMoveRegion(ar.id, 2 * kPPQ));
    REQUIRE(f.p.findTrack(f.audio.id)->regions[0].start == 2 * kPPQ);
    requireRoundTrip(f.p, *makeRemoveRegion(ar.id));
    REQUIRE(f.p.findTrack(f.audio.id)->regions.empty());
}

TEST_CASE("regions: removal restores the original position in the list", "[commands][regions]") {
    Fixture f;
    const Region a = f.audioRegion(), b = f.audioRegion(), c = f.audioRegion();
    for (const Region* r : {&a, &b, &c}) REQUIRE(makeAddRegion(f.audio.id, *r)->apply(f.p).ok());
    ApplyResult r = makeRemoveRegion(b.id)->apply(f.p);
    REQUIRE(r.ok());
    REQUIRE(r.inverse->apply(f.p).ok());
    const auto& regions = f.p.findTrack(f.audio.id)->regions;
    REQUIRE(regions[0].id == a.id);
    REQUIRE(regions[1].id == b.id);
    REQUIRE(regions[2].id == c.id);
}

TEST_CASE("regions: invalid regions are rejected and change nothing", "[commands][regions]") {
    Fixture f;
    const Region good = f.audioRegion();
    REQUIRE(makeAddRegion(f.audio.id, good)->apply(f.p).ok());

    requireRejected(f.p, *makeAddRegion(Uuid::random(gRng), f.audioRegion()), "not_found");
    requireRejected(f.p, *makeAddRegion(f.audio.id, good), "duplicate_id");     // same region id
    requireRejected(f.p, *makeAddRegion(f.inst.id, good), "duplicate_id");      // id is global

    Region r = f.audioRegion();
    r.length = 0;
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_region");
    r = f.audioRegion();
    r.length = -5;
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_region");
    r = f.audioRegion();
    r.start = -1;
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_region");
    r = f.audioRegion();
    r.mediaId = Uuid{};
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_region");
    r = f.audioRegion();
    r.mediaId = Uuid::random(gRng);  // not in the pool
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_region");
    r = f.audioRegion();
    r.sourceOffsetFrames = -1;
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_region");
    r = f.audioRegion();
    r.gainDb = 500.0f;
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_value");

    r = Fixture::midiRegion();
    r.mediaId = f.media.id;
    requireRejected(f.p, *makeAddRegion(f.inst.id, r), "bad_region");
    r = Fixture::midiRegion();
    r.timeBase = TimeBase::Absolute;
    requireRejected(f.p, *makeAddRegion(f.inst.id, r), "bad_region");
    r = Fixture::midiRegion();
    r.notes[0].velocity = 0;
    requireRejected(f.p, *makeAddRegion(f.inst.id, r), "bad_region");
    r = Fixture::midiRegion();
    r.notes[0].note = 200;
    requireRejected(f.p, *makeAddRegion(f.inst.id, r), "bad_region");
    r = Fixture::midiRegion();
    r.notes[0].length = 0;
    requireRejected(f.p, *makeAddRegion(f.inst.id, r), "bad_region");
    requireRejected(f.p, *makeAddRegion(f.bus.id, Fixture::midiRegion()), "invalid_kind");
    requireRejected(f.p, *makeAddRegion(f.audio.id, f.audioRegion(), 7), "bad_index");

    requireRejected(f.p, *makeMoveRegion(good.id, -1), "bad_region");
    requireRejected(f.p, *makeMoveRegion(Uuid::random(gRng), 0), "not_found");
    requireRejected(f.p, *makeRemoveRegion(Uuid::random(gRng)), "not_found");
}

TEST_CASE("regions: media in use cannot be removed until the region is gone", "[commands][regions]") {
    Fixture f;
    const Region ar = f.audioRegion();
    REQUIRE(makeAddRegion(f.audio.id, ar)->apply(f.p).ok());
    requireRejected(f.p, *makeRemoveMedia(f.media.id), "in_use");
    REQUIRE(makeRemoveRegion(ar.id)->apply(f.p).ok());
    REQUIRE(makeRemoveMedia(f.media.id)->apply(f.p).ok());
}

TEST_CASE("sends: add and remove round-trip, and routing is validated", "[commands][sends]") {
    Fixture f;
    const Send s{Uuid::random(gRng), f.bus.id, -6.0f, false};
    requireRoundTrip(f.p, *makeAddSend(f.audio.id, s));
    requireRoundTrip(f.p, *makeRemoveSend(s.id));

    REQUIRE(makeAddSend(f.audio.id, s)->apply(f.p).ok());
    requireRejected(f.p, *makeAddSend(f.audio.id, s), "duplicate_id");
    requireRejected(f.p, *makeAddSend(f.midi.id, s), "duplicate_id");  // id is global
    requireRejected(f.p, *makeAddSend(Uuid::random(gRng), Send{Uuid::random(gRng), f.bus.id, 0, false}), "not_found");
    requireRejected(f.p, *makeAddSend(f.midi.id, Send{Uuid::random(gRng), f.audio.id, 0, false}), "bad_target");
    requireRejected(f.p, *makeAddSend(f.midi.id, Send{Uuid::random(gRng), Uuid::random(gRng), 0, false}), "bad_target");
    requireRejected(f.p, *makeAddSend(f.bus.id, Send{Uuid::random(gRng), f.bus.id, 0, false}), "bad_target");  // itself
    requireRejected(f.p, *makeAddSend(f.midi.id, Send{Uuid::random(gRng), f.bus.id, 50.0f, false}), "bad_value");
    requireRejected(f.p, *makeAddSend(f.p.master()->id, Send{Uuid::random(gRng), f.bus.id, 0, false}), "invalid_kind");
    requireRejected(f.p, *makeRemoveSend(Uuid::random(gRng)), "not_found");
}

TEST_CASE("sends: routing cycles are rejected", "[commands][sends]") {
    Fixture f;
    REQUIRE(makeAddSend(f.bus.id, Send{Uuid::random(gRng), f.bus2.id, 0, false})->apply(f.p).ok());  // bus -> aux
    requireRejected(f.p, *makeAddSend(f.bus2.id, Send{Uuid::random(gRng), f.bus.id, 0, false}), "cycle");

    // a cycle through an output route is also a cycle
    Track chained = Fixture::track(TrackKind::Bus, "Chained");
    chained.strip.output = f.bus.id;  // chained -> bus -> aux
    REQUIRE(makeAddTrack(chained)->apply(f.p).ok());
    requireRejected(f.p, *makeAddSend(f.bus2.id, Send{Uuid::random(gRng), chained.id, 0, false}), "cycle");
}

TEST_CASE("inserts: set, undo and validation", "[commands][inserts]") {
    Fixture f;
    const std::vector<ProcessorRef> chain = {{"builtin.gain", {{"gainDb", -3.0}}, ""}};
    requireRoundTrip(f.p, *makeSetInserts(f.audio.id, chain));
    REQUIRE(f.p.findTrack(f.audio.id)->strip.inserts == chain);
    requireRejected(f.p, *makeSetInserts(f.audio.id, {{"vendor.unknown", {}, ""}}), "bad_value");
    requireRejected(f.p, *makeSetInserts(f.audio.id, {{"builtin.sine", {}, ""}}), "bad_value");  // instrument, not effect
    requireRejected(f.p, *makeSetInserts(Uuid::random(gRng), chain), "not_found");
}

TEST_CASE("transaction: applies all commands as one undo step", "[commands][transaction]") {
    Fixture f;
    const Region ar = f.audioRegion();
    std::vector<CommandPtr> cmds;
    cmds.push_back(makeAddRegion(f.audio.id, ar));
    cmds.push_back(makeMoveRegion(ar.id, kPPQ));  // depends on the previous command
    StripPatch patch;
    patch.gainDb = -9.0f;
    cmds.push_back(makeSetStrip(f.audio.id, patch));
    requireRoundTrip(f.p, *makeTransaction(std::move(cmds)));
}

TEST_CASE("transaction: a failing command rolls everything back", "[commands][transaction]") {
    Fixture f;
    const Project before = f.p;
    std::vector<CommandPtr> cmds;
    cmds.push_back(makeAddRegion(f.audio.id, f.audioRegion()));
    StripPatch patch;
    patch.gainDb = -9.0f;
    cmds.push_back(makeSetStrip(f.audio.id, patch));
    cmds.push_back(makeRemoveTrack(Uuid::random(gRng)));  // fails
    const ApplyResult r = makeTransaction(std::move(cmds))->apply(f.p);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error->code == "not_found");
    REQUIRE(r.error->message.find("command 2") != std::string::npos);
    REQUIRE(f.p == before);
}

TEST_CASE("transaction: empty transaction is a valid no-op", "[commands][transaction]") {
    Fixture f;
    const Project before = f.p;
    ApplyResult r = makeTransaction({})->apply(f.p);
    REQUIRE(r.ok());
    REQUIRE(f.p == before);
    REQUIRE(r.inverse->apply(f.p).ok());
}

TEST_CASE("new commands survive a JSON round trip", "[commands][json]") {
    Fixture f;
    const Region ar = f.audioRegion();
    std::vector<CommandPtr> inner;
    inner.push_back(makeMoveRegion(ar.id, 5));
    std::vector<CommandPtr> cmds;
    cmds.push_back(makeAddRegion(f.audio.id, ar, 0));
    cmds.push_back(makeRemoveRegion(ar.id));
    cmds.push_back(makeMoveRegion(ar.id, 960));
    cmds.push_back(makeAddSend(f.audio.id, Send{Uuid::random(gRng), f.bus.id, -3.0f, true}, 0));
    cmds.push_back(makeRemoveSend(Uuid::random(gRng)));
    cmds.push_back(makeSetInserts(f.audio.id, {{"builtin.gain", {{"gainDb", 1.5}}, ""}}));
    cmds.push_back(makeTransaction(std::move(inner)));
    for (const auto& c : cmds) {
        const nlohmann::json j = c->toJson();
        REQUIRE(commandFromJson(j)->toJson() == j);
    }
}
