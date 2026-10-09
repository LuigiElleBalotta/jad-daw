#include <catch2/catch_test_macros.hpp>
#include <random>
#include <nlohmann/json.hpp>
#include "lpc/commands.h"
#include "lpc/model_json.h"
#include "lpc/processor_ids.h"
#include "lpc/validation.h"

using namespace lpc;

namespace {
const char* kId = "vst3:00112233445566778899aabbccddeeff";

ProcessorRef plugin(const std::string& state = "") {
    ProcessorRef r;
    r.processorId = kId;
    r.state = state;
    r.label = "Test Reverb";
    return r;
}
}  // namespace

TEST_CASE("plugin refs: id format", "[plugin]") {
    REQUIRE(isVst3Id(kId));
    REQUIRE_FALSE(isVst3Id("vst3:00112233445566778899AABBCCDDEEFF"));  // upper case
    REQUIRE_FALSE(isVst3Id("vst3:0011"));
    REQUIRE_FALSE(isVst3Id("vst3:00112233445566778899aabbccddeeffaa"));
    REQUIRE_FALSE(isVst3Id("builtin.gain"));
    REQUIRE_FALSE(isVst3Id(""));
}

TEST_CASE("plugin refs: base64 check", "[plugin]") {
    REQUIRE(validBase64(""));
    REQUIRE(validBase64("AAAA"));
    REQUIRE(validBase64("AAA="));
    REQUIRE(validBase64("AA=="));
    REQUIRE_FALSE(validBase64("AAA"));      // length not a multiple of 4
    REQUIRE_FALSE(validBase64("AA=A"));     // padding in the middle
    REQUIRE_FALSE(validBase64("AAA*"));
}

TEST_CASE("plugin refs: label round trip and old files", "[plugin][json]") {
    const nlohmann::json j = plugin("AAAA");
    REQUIRE(j.at("label") == "Test Reverb");
    REQUIRE(j.at("processorId") == kId);
    REQUIRE(j.get<ProcessorRef>() == plugin("AAAA"));

    ProcessorRef gain;
    gain.processorId = "builtin.gain";
    const nlohmann::json g = gain;
    REQUIRE_FALSE(g.contains("label"));  // not written when empty: old files stay byte-identical

    const nlohmann::json old = {{"processorId", "builtin.gain"}, {"params", {{"gainDb", 3.0}}}, {"state", ""}};
    const ProcessorRef loaded = old.get<ProcessorRef>();
    REQUIRE(loaded.label.empty());
    REQUIRE(loaded.params.at("gainDb") == 3.0);
}

TEST_CASE("plugin refs: checkInsert", "[plugin]") {
    REQUIRE_FALSE(checkInsert(plugin("AAAA")).has_value());
    REQUIRE_FALSE(checkInsert(plugin("")).has_value());

    ProcessorRef withParams = plugin();
    withParams.params["x"] = 1.0;
    REQUIRE(checkInsert(withParams).has_value());

    REQUIRE(checkInsert(plugin("not base64!")).has_value());

    ProcessorRef badId = plugin();
    badId.processorId = "vst3:XYZ";
    REQUIRE(checkInsert(badId).has_value());

    ProcessorRef longLabel = plugin();
    longLabel.label = std::string(129, 'a');
    REQUIRE(checkInsert(longLabel).has_value());

    ProcessorRef huge = plugin(std::string(kMaxPluginStateChars + 4, 'A'));
    REQUIRE(checkInsert(huge).has_value());

    // built-ins are unchanged
    ProcessorRef gain;
    gain.processorId = "builtin.gain";
    gain.params["gainDb"] = 6.0;
    REQUIRE_FALSE(checkInsert(gain).has_value());
}

TEST_CASE("plugin refs: a plug-in insert is accepted by add_insert", "[plugin]") {
    std::mt19937_64 rng(5);
    Project p;
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Audio;
    t.name = "A";
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    REQUIRE(makeAddInsert(t.id, plugin("AAAA"))->apply(p).ok());
    REQUIRE(p.findTrack(t.id)->strip.inserts.size() == 1);
    REQUIRE(checkProject(p) == std::nullopt);
}
