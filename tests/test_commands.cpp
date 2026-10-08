#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include "lpc/commands.h"
#include "lpc/model_json.h"

using namespace lpc;

namespace {

std::mt19937_64 gRng(99);

Track makeTrack(TrackKind kind, const char* name) {
    Track t;
    t.id = Uuid::random(gRng);
    t.kind = kind;
    t.name = name;
    if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{"builtin.sine", {}, ""};
    return t;
}

// Applies cmd, then its inverse, and checks the project is back to the original; leaves cmd applied.
void requireRoundTrip(Project& p, const Command& cmd) {
    const Project before = p;
    ApplyResult r = cmd.apply(p);
    REQUIRE(r.ok());
    REQUIRE(r.inverse != nullptr);
    ApplyResult back = r.inverse->apply(p);
    REQUIRE(back.ok());
    REQUIRE(p == before);
    REQUIRE(cmd.apply(p).ok());
}

void requireRejected(Project& p, const Command& cmd, const char* code) {
    const Project before = p;
    const ApplyResult r = cmd.apply(p);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error->code == code);
    REQUIRE(r.inverse == nullptr);
    REQUIRE(p == before);
}

}  // namespace

TEST_CASE("commands: add and remove track", "[commands]") {
    Project p(Uuid::random(gRng));
    const Track bus = makeTrack(TrackKind::Bus, "Bus");
    requireRoundTrip(p, *makeAddTrack(bus));
    REQUIRE(p.tracks.size() == 2);
    requireRoundTrip(p, *makeRemoveTrack(bus.id));
    REQUIRE(p.tracks.size() == 1);
}

TEST_CASE("commands: add track keeps the requested index", "[commands]") {
    Project p(Uuid::random(gRng));
    const Track a = makeTrack(TrackKind::Audio, "A");
    const Track b = makeTrack(TrackKind::Audio, "B");
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    REQUIRE(makeAddTrack(b, 0)->apply(p).ok());
    REQUIRE(p.tracks[0].id == b.id);
}

TEST_CASE("commands: add track rejects bad input", "[commands]") {
    Project p(Uuid::random(gRng));
    const Track a = makeTrack(TrackKind::Audio, "A");
    REQUIRE(makeAddTrack(a)->apply(p).ok());

    requireRejected(p, *makeAddTrack(a), "duplicate_id");
    Track nullId = makeTrack(TrackKind::Audio, "N");
    nullId.id = Uuid{};
    requireRejected(p, *makeAddTrack(nullId), "duplicate_id");
    requireRejected(p, *makeAddTrack(makeTrack(TrackKind::Master, "M2")), "invalid_kind");
    requireRejected(p, *makeAddTrack(makeTrack(TrackKind::Audio, "X"), 99), "bad_index");
    requireRejected(p, *makeAddTrack(makeTrack(TrackKind::Audio, "X"), -2), "bad_index");

    Track badOut = makeTrack(TrackKind::Audio, "Out");
    badOut.strip.output = a.id;  // an audio track is not a valid destination
    requireRejected(p, *makeAddTrack(badOut), "bad_output");
    badOut.strip.output = Uuid::random(gRng);  // does not exist
    requireRejected(p, *makeAddTrack(badOut), "bad_output");

    Track noInstrument = makeTrack(TrackKind::Instrument, "I");
    noInstrument.instrument.reset();
    requireRejected(p, *makeAddTrack(noInstrument), "invalid_kind");
    Track strayInstrument = makeTrack(TrackKind::Audio, "S");
    strayInstrument.instrument = ProcessorRef{"builtin.sine", {}, ""};
    requireRejected(p, *makeAddTrack(strayInstrument), "invalid_kind");
    Track badValue = makeTrack(TrackKind::Audio, "V");
    badValue.strip.pan = 5.0f;
    requireRejected(p, *makeAddTrack(badValue), "bad_value");
}

TEST_CASE("commands: remove track rejects master, unknown and referenced tracks", "[commands]") {
    Project p(Uuid::random(gRng));
    const Track bus = makeTrack(TrackKind::Bus, "Bus");
    Track audio = makeTrack(TrackKind::Audio, "A");
    audio.strip.output = bus.id;
    REQUIRE(makeAddTrack(bus)->apply(p).ok());
    REQUIRE(makeAddTrack(audio)->apply(p).ok());

    requireRejected(p, *makeRemoveTrack(p.master()->id), "invalid_kind");
    requireRejected(p, *makeRemoveTrack(Uuid::random(gRng)), "not_found");
    requireRejected(p, *makeRemoveTrack(bus.id), "in_use");  // audio routes to it
    REQUIRE(makeRemoveTrack(audio.id)->apply(p).ok());
    REQUIRE(makeRemoveTrack(bus.id)->apply(p).ok());
}

TEST_CASE("transaction: a bus and the tracks sending to it are removed together in either order", "[commands][transaction]") {
    for (const bool busFirst : {true, false}) {
        Project p(Uuid::random(gRng));
        const Track bus = makeTrack(TrackKind::Bus, "Bus");
        Track byOutput = makeTrack(TrackKind::Audio, "A");
        byOutput.strip.output = bus.id;
        Track bySend = makeTrack(TrackKind::Audio, "B");
        Send send;
        send.id = Uuid::random(gRng);
        send.targetTrackId = bus.id;
        bySend.strip.sends.push_back(send);
        REQUIRE(makeAddTrack(bus)->apply(p).ok());
        REQUIRE(makeAddTrack(byOutput)->apply(p).ok());
        REQUIRE(makeAddTrack(bySend)->apply(p).ok());

        std::vector<CommandPtr> cmds;
        if (busFirst) {
            cmds.push_back(makeRemoveTrack(bus.id));
            cmds.push_back(makeRemoveTrack(byOutput.id));
            cmds.push_back(makeRemoveTrack(bySend.id));
        } else {
            cmds.push_back(makeRemoveTrack(byOutput.id));
            cmds.push_back(makeRemoveTrack(bySend.id));
            cmds.push_back(makeRemoveTrack(bus.id));
        }
        requireRoundTrip(p, *makeTransaction(std::move(cmds)));
        REQUIRE(p.tracks.size() == 1);  // only the master is left
    }
}

TEST_CASE("commands: set strip patches only the given fields and undoes them", "[commands]") {
    Project p(Uuid::random(gRng));
    const Track a = makeTrack(TrackKind::Audio, "A");
    REQUIRE(makeAddTrack(a)->apply(p).ok());

    StripPatch patch;
    patch.gainDb = -6.0f;
    patch.mute = true;
    requireRoundTrip(p, *makeSetStrip(a.id, patch));
    const Track* t = p.findTrack(a.id);
    REQUIRE(t->strip.gainDb == -6.0f);
    REQUIRE(t->strip.mute);
    REQUIRE(t->strip.pan == 0.0f);  // untouched
}

TEST_CASE("commands: set strip validates values", "[commands]") {
    Project p(Uuid::random(gRng));
    const Track a = makeTrack(TrackKind::Audio, "A");
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    StripPatch s;
    s.pan = 1.5f;
    requireRejected(p, *makeSetStrip(a.id, s), "bad_value");
    s = {};
    s.gainDb = nan;
    requireRejected(p, *makeSetStrip(a.id, s), "bad_value");
    s = {};
    s.gainDb = inf;
    requireRejected(p, *makeSetStrip(a.id, s), "bad_value");
    s = {};
    s.gainDb = 100.0f;
    requireRejected(p, *makeSetStrip(a.id, s), "bad_value");
    requireRejected(p, *makeSetStrip(Uuid::random(gRng), StripPatch{}), "not_found");
}

TEST_CASE("commands: tempo", "[commands]") {
    Project p(Uuid::random(gRng));
    requireRoundTrip(p, *makeSetTempo(4 * kPPQ, 90.0));   // new event: inverse removes it
    requireRoundTrip(p, *makeSetTempo(0, 100.0));         // existing event: inverse restores 120
    REQUIRE(p.tempoMap.bpmAt(0) == 100.0);
    requireRoundTrip(p, *makeRemoveTempo(4 * kPPQ));
    requireRejected(p, *makeSetTempo(0, 5.0), "bad_tempo");
    requireRejected(p, *makeSetTempo(-5, 100.0), "bad_tempo");
    requireRejected(p, *makeRemoveTempo(0), "bad_tempo");
    requireRejected(p, *makeRemoveTempo(777), "bad_tempo");
}

TEST_CASE("commands: media pool", "[commands]") {
    Project p(Uuid::random(gRng));
    MediaItem m{Uuid::random(gRng), "audio/a.wav", "h", 48000, 2, 96000};
    requireRoundTrip(p, *makeAddMedia(m));
    requireRoundTrip(p, *makeRemoveMedia(m.id));

    MediaItem wrongRate = m;
    wrongRate.id = Uuid::random(gRng);
    wrongRate.sampleRate = 44100;
    requireRejected(p, *makeAddMedia(wrongRate), "bad_media");
    for (const char* bad : {"", "/abs/a.wav", "C:/a.wav", "../escape.wav", "audio/../../x.wav", "a\\b.wav"}) {
        MediaItem bp = m;
        bp.id = Uuid::random(gRng);
        bp.path = bad;
        requireRejected(p, *makeAddMedia(bp), "bad_media");
    }
    MediaItem sixCh = m;
    sixCh.id = Uuid::random(gRng);
    sixCh.channels = 6;
    requireRejected(p, *makeAddMedia(sixCh), "bad_media");
    REQUIRE(makeAddMedia(m)->apply(p).ok());
    requireRejected(p, *makeAddMedia(m), "duplicate_id");
    requireRejected(p, *makeRemoveMedia(Uuid::random(gRng)), "not_found");
}

TEST_CASE("commands: every command survives a JSON round trip", "[commands][json]") {
    Project p(Uuid::random(gRng));
    const Track t = makeTrack(TrackKind::Instrument, "Keys");
    StripPatch patch;
    patch.gainDb = -3.0f;
    patch.solo = true;
    std::vector<CommandPtr> cmds;
    cmds.push_back(makeAddTrack(t, 0));
    cmds.push_back(makeRemoveTrack(t.id));
    cmds.push_back(makeSetStrip(t.id, patch));
    cmds.push_back(makeSetTempo(kPPQ, 100.0));
    cmds.push_back(makeRemoveTempo(kPPQ));
    cmds.push_back(makeAddMedia({Uuid::random(gRng), "audio/x.wav", "h", 48000, 1, 10}));
    cmds.push_back(makeRemoveMedia(Uuid::random(gRng)));
    for (const auto& c : cmds) {
        const nlohmann::json j = c->toJson();
        REQUIRE(j["type"] == c->type());
        REQUIRE(commandFromJson(j)->toJson() == j);
    }
    REQUIRE_THROWS_AS(commandFromJson({{"type", "no_such_command"}}), std::runtime_error);
    REQUIRE_THROWS(commandFromJson(nlohmann::json::array()));
    REQUIRE_THROWS(commandFromJson({{"type", "remove_track"}}));  // missing trackId
}
