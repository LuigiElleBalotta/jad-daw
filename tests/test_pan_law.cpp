#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "lpc/commands.h"
#include "lpc/model.h"
#include "lpc/model_json.h"
#include "lpc/pan_law.h"

#include <cmath>

using namespace lpc;
using Catch::Approx;

// The numbers decompiled from Logic Pro 11.2 (CStereoMixer::SetPanForChannel, CMixer::SetCompensated): docs/logic-reference/mixer.md.
TEST_CASE("pan law: the quadratic -3 dB law", "[panlaw]") {
    const PanGains c = panGains(PanLaw::Minus3, 0.0f);
    CHECK(c.l == Approx(0.75f));
    CHECK(c.r == Approx(0.75f));
    CHECK(20.0 * std::log10(c.l) == Approx(-2.498).margin(0.001));
    const PanGains left = panGains(PanLaw::Minus3, -1.0f), right = panGains(PanLaw::Minus3, 1.0f);
    CHECK(left.l == Approx(1.0f));
    CHECK(left.r == Approx(0.0f).margin(1e-6));
    CHECK(right.l == Approx(0.0f).margin(1e-6));
    CHECK(right.r == Approx(1.0f));
    const PanGains q = panGains(PanLaw::Minus3, -0.5f);  // t = 0.25
    CHECK(q.l == Approx(1.0f - 0.0625f));
    CHECK(q.r == Approx(1.0f - 0.75f * 0.75f));
}

TEST_CASE("pan law: compensated is unity at the centre and +2.5 dB at the ends", "[panlaw]") {
    const PanGains c = panGains(PanLaw::Minus3Comp, 0.0f);
    CHECK(c.l == Approx(1.0f));
    CHECK(c.r == Approx(1.0f));
    CHECK(panGains(PanLaw::Minus3Comp, 1.0f).r == Approx(4.0f / 3.0f));
    CHECK(panGains(PanLaw::Minus6, 0.0f).l == Approx(0.5f));
    CHECK(panGains(PanLaw::Minus6Comp, 0.0f).l == Approx(1.0f));
    CHECK(panGains(PanLaw::Minus45Comp, 0.0f).l == Approx(1.0f));
    CHECK(panGains(PanLaw::Zero, 0.0f).l == Approx(1.0f));
    CHECK(panGains(PanLaw::Zero, 0.5f).l == Approx(0.5f));
    CHECK(balanceGains(0.5f).l == Approx(0.5f));
    CHECK(balanceGains(0.5f).r == Approx(1.0f));
}

TEST_CASE("pan law: names round trip and the project keeps its law", "[panlaw]") {
    for (PanLaw l : {PanLaw::Zero, PanLaw::Minus3, PanLaw::Minus3Comp, PanLaw::Minus45Comp, PanLaw::Minus6, PanLaw::Minus6Comp}) {
        PanLaw back = PanLaw::Zero;
        REQUIRE(panLawFromName(panLawName(l), back));
        CHECK(back == l);
    }
    PanLaw dummy;
    CHECK(!panLawFromName("nonsense", dummy));

    Project p;
    CHECK(p.panLaw == PanLaw::Minus3Comp);  // Logic's default
    CHECK(!toJson(p).contains("panLaw"));
    const ApplyResult r = commandFromJson({{"type", "set_pan_law"}, {"law", "minus6"}})->apply(p);
    REQUIRE(r.ok());
    CHECK(p.panLaw == PanLaw::Minus6);
    CHECK(projectFromJson(toJson(p)).panLaw == PanLaw::Minus6);
    REQUIRE(r.inverse->apply(p).ok());
    CHECK(p.panLaw == PanLaw::Minus3Comp);
}

TEST_CASE("pan law: a strip is stereo until it is made mono, and the flag is saved", "[panlaw]") {
    Project p;
    Track t;
    t.id = Uuid::random();
    t.kind = TrackKind::Audio;
    t.name = "A";
    t.color = "blue";
    p.tracks.push_back(t);
    CHECK(!p.tracks.back().strip.mono);
    REQUIRE(commandFromJson({{"type", "set_strip"}, {"trackId", t.id}, {"mono", true}})->apply(p).ok());
    CHECK(p.tracks.back().strip.mono);
    CHECK(projectFromJson(toJson(p)).tracks.back().strip.mono);
}

TEST_CASE("project settings: a choice is saved, undone and validated", "[panlaw][settings]") {
    Project p;
    CHECK(!toJson(p).contains("settings"));
    const ApplyResult r = commandFromJson({{"type", "set_project_setting"}, {"key", "tempoMode"}, {"value", "keep"}})->apply(p);
    REQUIRE(r.ok());
    CHECK(p.settings.at("tempoMode") == "keep");
    CHECK(projectFromJson(toJson(p)).settings.at("tempoMode") == "keep");
    REQUIRE(r.inverse->apply(p).ok());  // the key was not set: it goes away
    CHECK(p.settings.empty());
    CHECK(!commandFromJson({{"type", "set_project_setting"}, {"key", "bad key"}, {"value", "x"}})->apply(p).ok());
    CHECK(!commandFromJson({{"type", "set_project_setting"}, {"key", ""}, {"value", "x"}})->apply(p).ok());
}
