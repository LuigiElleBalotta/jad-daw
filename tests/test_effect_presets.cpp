#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "lpc/audio/effects.h"
#include "lpc/commands.h"
#include "lpc/effect_presets.h"
#include "lpc/effect_specs.h"
#include "lpc/match_eq.h"
#include "lpc/model_json.h"
#include "lpc/processor_ids.h"
#include "lpc/validation.h"

using namespace lpc;
using Catch::Approx;

namespace {
const EffectPresetLibrary& shipped() {
    static const EffectPresetLibrary lib = EffectPresetLibrary::fromFile(LPC_EFFECT_PRESETS_JSON);
    return lib;
}
}  // namespace

TEST_CASE("effect presets: the shipped file holds Logic's factory presets and loads without problems", "[presets]") {
    const EffectPresetLibrary& lib = shipped();
    for (const std::string& p : lib.problems()) FAIL(p);
    CHECK(lib.presets(kProcCompressor).size() == 158);
    CHECK(lib.presets(kProcEq).size() == 148);
    CHECK(lib.presets(kProcGate).size() == 7);
    CHECK(lib.presets(kProcLimiter).size() == 2);
    CHECK(lib.presets(kProcAdaptiveLimiter).size() == 4);
    CHECK(lib.presets("builtin.nope").empty());
    CHECK(lib.matchEqNames().size() == 75);
    const auto cats = lib.categories(kProcCompressor);
    REQUIRE(cats.size() >= 6);
    CHECK(cats[0] == "01 Drums");
}

TEST_CASE("effect presets: a preset is what Logic stores, inside every parameter's range", "[presets]") {
    const EffectPresetLibrary& lib = shipped();
    const EffectPreset* classic = lib.find(kProcCompressor, "01 Drums", "Classic Drums");
    REQUIRE(classic != nullptr);
    CHECK(classic->params.at("threshold") == Approx(-23.0));
    CHECK(classic->params.at("ratio") == Approx(5.2));
    CHECK(classic->params.at("attack") == Approx(10.0));
    CHECK(classic->params.at("release") == Approx(48.0));
    CHECK(classic->params.at("gain") == Approx(3.5));
    CHECK(classic->params.at("circuit") == Approx(8.0));         // Classic VCA
    const EffectPreset* fet = lib.find(kProcCompressor, "01 Drums", "FET Live Drums");   // an old big-endian preset: it has no Mix
    REQUIRE(fet != nullptr);
    CHECK(fet->params.at("threshold") == Approx(-14.0));
    CHECK(fet->params.at("circuit") == Approx(5.0));             // Studio FET
    CHECK(fet->params.count("mix") == 0);
    for (const char* id : {kProcCompressor, kProcEq, kProcGate, kProcLimiter, kProcAdaptiveLimiter}) {
        const EffectSpec* spec = findEffectSpec(id);
        REQUIRE(spec != nullptr);
        for (const EffectPreset& p : lib.presets(id)) {
            for (const auto& [name, value] : p.params) {
                const EffectParam* prm = spec->find(name);
                REQUIRE(prm != nullptr);
                CHECK(value >= prm->min);
                CHECK(value <= prm->max);
            }
            ProcessorRef ref{id, p.params, "", "", false};
            CHECK(!checkInsert(ref).has_value());
            CHECK(audio::makeBuiltinEffect(ref, 48000.0) != nullptr);
        }
    }
    const auto def = lib.defaults(kProcCompressor);
    CHECK(def.at("threshold") == Approx(-20.0));
    CHECK(def.at("ratio") == Approx(2.1));
    CHECK(def.at("release") == Approx(51.0));
}

TEST_CASE("effect presets: the Channel EQ presets reach the band parameters", "[presets]") {
    const EffectPreset* p = shipped().find(kProcEq, "01 Drums", "Brighten Overheads");
    REQUIRE(p != nullptr);
    CHECK(p->params.at("hsGain") == Approx(7.5));
    CHECK(p->params.at("hsFreq") == Approx(11400.0).margin(1.0));
    ProcessorRef ref{kProcEq, p->params, "", "", false};
    CHECK(audio::eqResponseDb(ref, 48000.0, 18000.0) > 4.0);       // the shelf lifts the air
    CHECK(audio::eqResponseDb(ref, 48000.0, 100.0) < 1.0);
}

TEST_CASE("effect presets: the Match EQ target curves are on the third-octave bands", "[presets]") {
    const EffectPresetLibrary& lib = shipped();
    const auto names = lib.matchEqNames();
    REQUIRE(!names.empty());
    for (const std::string& n : names) {
        const std::vector<double>* c = lib.matchEqCurve(n);
        REQUIRE(c != nullptr);
        CHECK(c->size() == static_cast<std::size_t>(kMatchEqBands));
    }
    CHECK(lib.matchEqCurve("mastering/AM_Curve_1") != nullptr);
    CHECK(lib.matchEqCurve("stereo/Alternative 01") != nullptr);
    CHECK(lib.matchEqCurve("nope") == nullptr);
    // music is louder in the bass than in the air
    const auto* c = lib.matchEqCurve("stereo/Alternative 01");
    CHECK((*c)[8] > (*c)[28]);
}

TEST_CASE("effect presets: applying one is a single undo step that also resets the other parameters", "[presets]") {
    Project p;
    Track t;
    t.id = Uuid::random();
    t.kind = TrackKind::Audio;
    t.name = "A";
    t.color = "blue";
    t.strip.inserts.push_back(ProcessorRef{kProcCompressor, {{"mix", 40}, {"threshold", -5}}, "", "", false});
    p.tracks.push_back(t);
    const EffectPreset* classic = shipped().find(kProcCompressor, "01 Drums", "Classic Drums");
    REQUIRE(classic != nullptr);
    const ApplyResult r = EffectPresetLibrary::applyCommand(t.id, 0, kProcCompressor, classic->params)->apply(p);
    REQUIRE(r.ok());
    const ProcessorRef& after = p.tracks.back().strip.inserts[0];
    CHECK(after.params.at("threshold") == Approx(-23.0));
    CHECK(after.params.count("mix") == 1);        // Classic Drums stores a mix of 100
    CHECK(after.params.at("mix") == Approx(100.0));
    CHECK(!checkProject(p).has_value());
    REQUIRE(r.inverse->apply(p).ok());
    CHECK(p.tracks.back().strip.inserts[0].params.at("threshold") == Approx(-5.0));
    CHECK(p.tracks.back().strip.inserts[0].params.at("mix") == Approx(40.0));
    CHECK(EffectPresetLibrary::applyCommand(t.id, 0, "builtin.nope", {}) == nullptr);
}

TEST_CASE("effect presets: a broken file is reported, never thrown", "[presets]") {
    CHECK(!EffectPresetLibrary::fromFile("does/not/exist.json").problems().empty());
    CHECK(!EffectPresetLibrary::fromJson(nlohmann::json::array()).problems().empty());
    const auto lib = EffectPresetLibrary::fromJson({{"plugins", {{"builtin.nope", {{"presets", nlohmann::json::array()}}}}}});
    CHECK(lib.problems().size() == 1);
}
