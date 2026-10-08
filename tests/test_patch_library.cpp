#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>

#include "lpc/commands.h"
#include "lpc/model_json.h"
#include "lpc/patch_library.h"
#include "lpc/processor_ids.h"

using namespace lpc;
using nlohmann::json;

namespace {
std::mt19937_64 gRng(777);

Track track(TrackKind kind, const char* name) {
    Track t;
    t.id = Uuid::random(gRng);
    t.kind = kind;
    t.name = name;
    if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{kProcSine, {}, ""};
    return t;
}

const char* kDoc = R"({"patches":[
 {"id":"audio.a","category":"01 Clean","name":"Clean","kind":"audio","instrument":null,
  "strip":{"gainDb":-3.0,"pan":0.25},
  "inserts":[{"processorId":"builtin.gain","params":{"gainDb":1.0}}],
  "smartControls":[
   {"id":"level","label":"Level","group":"Main","min":-24,"max":6,"default":-3,"targets":[{"path":"strip.gainDb","from":-24,"to":6}]},
   {"id":"boost","label":"Boost","group":"Tone","min":0,"max":1,"default":0.1,
    "targets":[{"path":"insert.0.gainDb","from":0,"to":12},{"path":"strip.gainDb","from":-2,"to":-5}]}]},
 {"id":"inst.a","category":"01 Leads","name":"Lead","kind":"instrument","instrument":{"processorId":"builtin.sine"},
  "strip":{"gainDb":-6.0,"pan":0.0},"inserts":[],"smartControls":[]},
 {"id":"bus.a","category":"01 Returns","name":"Return","kind":"bus","instrument":null,"strip":{"gainDb":-6.0},"inserts":[],"smartControls":[]}
]})";
}  // namespace

TEST_CASE("PatchLibrary: the shipped catalogue loads without problems", "[patches]") {
    const PatchLibrary lib = PatchLibrary::fromFile(LPC_PATCHES_JSON);
    for (const auto& p : lib.problems()) FAIL(p.where + ": " + p.message);
    REQUIRE(lib.patches().size() >= 8);
    for (const TrackKind k : {TrackKind::Audio, TrackKind::Instrument, TrackKind::Bus, TrackKind::Aux}) {
        CAPTURE(static_cast<int>(k));
        REQUIRE_FALSE(lib.categories(k).empty());
    }
}

TEST_CASE("PatchLibrary: lists categories and patches by kind", "[patches]") {
    const PatchLibrary lib = PatchLibrary::fromJson(json::parse(kDoc));
    REQUIRE(lib.problems().empty());
    REQUIRE(lib.patches().size() == 3);
    REQUIRE(lib.categories(TrackKind::Audio) == std::vector<std::string>{"01 Clean"});
    REQUIRE(lib.categories(TrackKind::Midi).empty());
    REQUIRE(lib.inCategory(TrackKind::Audio, "01 Clean").size() == 1);
    REQUIRE(lib.inCategory(TrackKind::Audio, "01 Leads").empty());
    REQUIRE(lib.find("inst.a")->instrument->processorId == "builtin.sine");
    REQUIRE(lib.find("nope") == nullptr);
}

TEST_CASE("PatchLibrary: bad entries are skipped and reported, the rest loads", "[patches]") {
    json doc = json::parse(kDoc);
    auto bad = [&](auto&& edit) {
        json entry = doc["patches"][0];
        entry["id"] = "bad";
        edit(entry);
        return entry;
    };
    json patches = doc["patches"];
    patches.push_back(doc["patches"][0]);                                                               // duplicate id
    patches.push_back(bad([](json& e) { e["kind"] = "midi"; }));                                       // unsupported kind
    patches.push_back(bad([](json& e) { e["id"] = "bad id"; }));                                       // invalid id
    patches.push_back(bad([](json& e) { e["inserts"][0]["processorId"] = "vendor.x"; }));              // unknown processor
    patches.push_back(bad([](json& e) { e["strip"]["gainDb"] = 99; }));                                // out of range
    patches.push_back(bad([](json& e) { e["smartControls"][0]["targets"][0]["path"] = "strip.volume"; }));
    patches.push_back(bad([](json& e) { e["smartControls"][1]["targets"][0]["path"] = "insert.3.gainDb"; }));  // no such insert
    patches.push_back(bad([](json& e) { e["smartControls"][0]["min"] = 6; e["smartControls"][0]["max"] = 6; }));
    patches.push_back(bad([](json& e) { e["smartControls"][0]["default"] = 50; }));
    patches.push_back(bad([](json& e) { e["smartControls"][0]["targets"] = json::array(); }));
    patches.push_back(bad([](json& e) { e["instrument"] = json{{"processorId", "builtin.sine"}}; }));  // audio with an instrument
    patches.push_back(json::array());                                                                   // not an object
    doc["patches"] = patches;
    const PatchLibrary lib = PatchLibrary::fromJson(doc);
    REQUIRE(lib.patches().size() == 3);
    REQUIRE(lib.problems().size() == patches.size() - 3);
}

TEST_CASE("PatchLibrary: a missing file, garbage and a wrong top level give a problem and an empty catalogue", "[patches]") {
    REQUIRE(PatchLibrary::fromFile("does/not/exist.json").problems().size() == 1);
    REQUIRE(PatchLibrary::fromJson(json::parse("[]")).problems().size() == 1);
    REQUIRE(PatchLibrary::fromJson(json::parse(R"({"patches":5})")).problems().size() == 1);
    REQUIRE(PatchLibrary::fromJson(json::parse("{}")).patches().empty());
}

TEST_CASE("PatchLibrary: applying a patch is one exact undo step", "[patches]") {
    const PatchLibrary lib = PatchLibrary::fromJson(json::parse(kDoc));
    const Track a = track(TrackKind::Audio, "Audio");
    const Track k = track(TrackKind::Instrument, "Keys");
    Project p{Uuid::random(gRng)};
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    REQUIRE(makeAddTrack(k)->apply(p).ok());
    const Project before = p;
    auto r = lib.applyCommand(a.id, TrackKind::Audio, "audio.a")->apply(p);
    REQUIRE(r.ok());
    const Track* t = p.findTrack(a.id);
    REQUIRE(t->patchId == "audio.a");
    REQUIRE(t->strip.gainDb == -3.0f);
    REQUIRE(t->strip.pan == 0.25f);
    REQUIRE(t->strip.inserts.size() == 1);
    REQUIRE(t->strip.inserts[0].params.at("gainDb") == 1.0);
    REQUIRE(r.inverse->apply(p).ok());
    REQUIRE(p == before);
    REQUIRE(lib.applyCommand(k.id, TrackKind::Instrument, "inst.a")->apply(p).ok());
    REQUIRE(p.findTrack(k.id)->instrument->processorId == "builtin.sine");
}

TEST_CASE("PatchLibrary: a patch of another kind or an unknown id gives no command", "[patches]") {
    const PatchLibrary lib = PatchLibrary::fromJson(json::parse(kDoc));
    const Uuid id = Uuid::random(gRng);
    REQUIRE(lib.applyCommand(id, TrackKind::Instrument, "audio.a") == nullptr);
    REQUIRE(lib.applyCommand(id, TrackKind::Audio, "inst.a") == nullptr);
    REQUIRE(lib.applyCommand(id, TrackKind::Audio, "gone.patch") == nullptr);
    REQUIRE(lib.applyCommand(id, TrackKind::Master, "audio.a") == nullptr);
}

TEST_CASE("PatchLibrary: a Smart Control moves every target, clamps, and undoes exactly", "[patches][smart]") {
    const PatchLibrary lib = PatchLibrary::fromJson(json::parse(kDoc));
    const Track a = track(TrackKind::Audio, "Audio");
    Project p{Uuid::random(gRng)};
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    REQUIRE(lib.applyCommand(a.id, TrackKind::Audio, "audio.a")->apply(p).ok());
    const SmartControl& level = lib.find("audio.a")->smartControls[0];
    const SmartControl& boost = lib.find("audio.a")->smartControls[1];

    Project before = p;
    auto r = PatchLibrary::smartControlCommand(a.id, level, 6.0)->apply(p);
    REQUIRE(r.ok());
    REQUIRE(p.findTrack(a.id)->strip.gainDb == 6.0f);
    REQUIRE(r.inverse->apply(p).ok());
    REQUIRE(p == before);

    REQUIRE(PatchLibrary::smartControlCommand(a.id, level, 1000.0)->apply(p).ok());  // clamped to the range
    REQUIRE(p.findTrack(a.id)->strip.gainDb == 6.0f);
    REQUIRE(PatchLibrary::smartControlCommand(a.id, level, -1000.0)->apply(p).ok());
    REQUIRE(p.findTrack(a.id)->strip.gainDb == -24.0f);
    REQUIRE(PatchLibrary::smartControlCommand(a.id, level, std::nan("")) == nullptr);

    before = p;
    r = PatchLibrary::smartControlCommand(a.id, boost, 1.0)->apply(p);  // two targets at once
    REQUIRE(r.ok());
    REQUIRE(p.findTrack(a.id)->strip.inserts[0].params.at("gainDb") == 12.0);
    REQUIRE(p.findTrack(a.id)->strip.gainDb == -5.0f);
    REQUIRE(r.inverse->apply(p).ok());
    REQUIRE(p == before);
}

TEST_CASE("PatchLibrary: a Smart Control reads its value back from the track", "[patches][smart]") {
    const PatchLibrary lib = PatchLibrary::fromJson(json::parse(kDoc));
    Track a = track(TrackKind::Audio, "Audio");
    a.strip.gainDb = -9.0f;  // -24..6 is 50% at -9
    a.strip.inserts.push_back(ProcessorRef{kProcGain, {{"gainDb", 6.0}}, ""});
    const SmartControl& level = lib.find("audio.a")->smartControls[0];
    const SmartControl& boost = lib.find("audio.a")->smartControls[1];
    REQUIRE(*PatchLibrary::smartControlValue(a, level) == -9.0);
    REQUIRE(*PatchLibrary::smartControlValue(a, boost) == 0.5);  // insert gain 6 of 0..12
    a.strip.inserts.clear();
    REQUIRE_FALSE(PatchLibrary::smartControlValue(a, boost).has_value());  // the insert it drives is gone
    a.strip.gainDb = 24.0f;                                                // outside the control range: clamped
    REQUIRE(*PatchLibrary::smartControlValue(a, level) == 6.0);
}
