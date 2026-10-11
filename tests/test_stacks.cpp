#include <catch2/catch_test_macros.hpp>
#include <random>
#include "lpc/commands.h"
#include "lpc/model_json.h"
#include "lpc/validation.h"

using namespace lpc;

namespace {
std::mt19937_64 gRng(777);

Track makeTrack(TrackKind kind, const char* name) {
    Track t;
    t.id = Uuid::random(gRng);
    t.kind = kind;
    t.name = name;
    return t;
}
}  // namespace

TEST_CASE("track stacks: a bus becomes a stack, tracks join it, and it cannot go while it holds tracks", "[stacks]") {
    Project p{Uuid::random(gRng)};
    Track stack = makeTrack(TrackKind::Bus, "Drums"), a = makeTrack(TrackKind::Audio, "Kick"), b = makeTrack(TrackKind::Audio, "Snare"), inner = makeTrack(TrackKind::Bus, "Toms");
    REQUIRE(makeAddTrack(stack)->apply(p).ok());
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    REQUIRE(makeAddTrack(b)->apply(p).ok());
    REQUIRE(makeAddTrack(inner)->apply(p).ok());
    auto patch = [&](Uuid id, TrackPatch pp) { return makeSetTrackProps(id, std::move(pp))->apply(p); };
    // a track cannot join something that is not a stack
    TrackPatch join;
    join.parent = stack.id;
    CHECK_FALSE(patch(a.id, join).ok());
    // an audio track cannot be a stack; a bus can
    TrackPatch asStack;
    asStack.stack = "folder";
    CHECK_FALSE(patch(a.id, asStack).ok());
    TrackPatch bad;
    bad.stack = "rack";
    CHECK_FALSE(patch(stack.id, bad).ok());
    REQUIRE(patch(stack.id, asStack).ok());
    REQUIRE(patch(a.id, join).ok());
    REQUIRE(patch(b.id, join).ok());
    CHECK(!checkProject(p).has_value());
    // a stack can sit in another stack, but not in itself or in what it holds
    asStack.stack = "summing";
    REQUIRE(patch(inner.id, asStack).ok());
    TrackPatch into = join;
    REQUIRE(patch(inner.id, into).ok());                       // Toms are in Drums
    TrackPatch loop;
    loop.parent = inner.id;
    CHECK_FALSE(patch(stack.id, loop).ok());                    // Drums cannot go into Toms, which are in Drums
    TrackPatch self;
    self.parent = inner.id;
    CHECK_FALSE(patch(inner.id, self).ok());
    // it cannot stop being a stack or be removed while it holds tracks
    TrackPatch notStack;
    notStack.stack = "";
    CHECK_FALSE(patch(stack.id, notStack).ok());
    CHECK_FALSE(makeRemoveTrack(stack.id)->apply(p).ok());
    // the tracks leave, then it can go; undo brings everything back
    const Project before = p;
    TrackPatch leave;
    leave.parent = Uuid{};
    ApplyResult left = patch(a.id, leave);
    REQUIRE(left.ok());
    REQUIRE(left.inverse->apply(p).ok());
    CHECK(p == before);
    // JSON keeps the stacks
    const Project again = projectFromJson(toJson(p));
    CHECK(again.findTrack(a.id)->parent == stack.id);
    CHECK(again.findTrack(stack.id)->stack == "folder");
    TrackPatch closed;
    closed.stackOpen = false;
    REQUIRE(patch(stack.id, closed).ok());
    CHECK(projectFromJson(toJson(p)).findTrack(stack.id)->stackOpen == false);
}

TEST_CASE("marker text: it is kept with the marker, in JSON, with a size limit and undo", "[markers]") {
    Project p{Uuid::random(gRng)};
    Marker m;
    m.id = Uuid::random(gRng);
    m.tick = kPPQ;
    m.name = "Verse";
    m.text = "La la la\nsecond line";
    ApplyResult r = makeSetMarkers({m})->apply(p);
    REQUIRE(r.ok());
    CHECK(projectFromJson(toJson(p)).markers[0].text == m.text);
    nlohmann::json j = toJson(p);
    j["markers"][0].erase("text");
    CHECK(projectFromJson(j).markers[0].text.empty());          // an old file without text
    Marker big = m;
    big.text.assign(4001, 'x');
    CHECK_FALSE(makeSetMarkers({big})->apply(p).ok());
    REQUIRE(r.inverse->apply(p).ok());
    CHECK(p.markers.empty());
}

TEST_CASE("track alternatives: another set of regions replaces the one that plays, and undo brings it back", "[alternatives]") {
    Project p{Uuid::random(gRng)};
    Track t = makeTrack(TrackKind::Instrument, "Keys");
    t.instrument = ProcessorRef{"builtin.sine", {}, ""};
    Region a;
    a.id = Uuid::random(gRng);
    a.start = 0;
    a.length = 4 * kPPQ;
    a.notes.push_back({0, kPPQ, 60, 90});
    t.regions.push_back(a);
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    const Project original = p;
    // a new, empty alternative: the old regions are kept aside
    ApplyResult made = makeSetTrackAlternatives(t.id, "Take 2", {TrackAlternative{"Main", {a}}}, {})->apply(p);
    REQUIRE(made.ok());
    CHECK(p.findTrack(t.id)->regions.empty());
    CHECK(p.findTrack(t.id)->alternative == "Take 2");
    REQUIRE(p.findTrack(t.id)->alternatives.size() == 1);
    CHECK(!checkProject(p).has_value());
    // the kept regions do not count as playing, but their ids stay taken
    Region clash = a;
    CHECK_FALSE(makeAddRegion(t.id, clash, -1)->apply(p).ok());
    // JSON keeps the alternatives
    const Project again = projectFromJson(toJson(p));
    CHECK(again.findTrack(t.id)->alternatives[0].regions[0].id == a.id);
    CHECK(again.findTrack(t.id)->alternative == "Take 2");
    // switching back: the two sets trade places
    ApplyResult back = makeSetTrackAlternatives(t.id, "Main", {TrackAlternative{"Take 2", {}}}, {a})->apply(p);
    REQUIRE(back.ok());
    CHECK(p.findTrack(t.id)->regions.size() == 1);
    REQUIRE(back.inverse->apply(p).ok());
    REQUIRE(made.inverse->apply(p).ok());
    CHECK(p == original);
    // media of a region in an alternative that does not play is still in use
    MediaItem m{Uuid::random(gRng), "audio/x.wav", "h", 48000, 1, 48000};
    REQUIRE(makeAddMedia(m)->apply(p).ok());
    Track au = makeTrack(TrackKind::Audio, "Voice");
    Region ar;
    ar.id = Uuid::random(gRng);
    ar.mediaId = m.id;
    ar.length = kPPQ;
    au.regions.push_back(ar);
    REQUIRE(makeAddTrack(au)->apply(p).ok());
    REQUIRE(makeSetTrackAlternatives(au.id, "Second", {TrackAlternative{"Main", {ar}}}, {})->apply(p).ok());
    CHECK_FALSE(makeRemoveMedia(m.id)->apply(p).ok());
    // a duplicate region id between alternatives is refused
    Region twin = ar;
    CHECK_FALSE(makeSetTrackAlternatives(au.id, "Second", {TrackAlternative{"Main", {ar}}}, {twin})->apply(p).ok());
    // bad names
    CHECK_FALSE(makeSetTrackAlternatives(au.id, "x", {TrackAlternative{"", {}}}, {})->apply(p).ok());
    // a bus has no alternatives
    Track bus = makeTrack(TrackKind::Bus, "Bus");
    REQUIRE(makeAddTrack(bus)->apply(p).ok());
    CHECK_FALSE(makeSetTrackAlternatives(bus.id, "x", {}, {})->apply(p).ok());
}
