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
