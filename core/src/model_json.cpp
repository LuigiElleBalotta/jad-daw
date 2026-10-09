#include "lpc/model_json.h"
#include "lpc/validation.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace lpc {

void to_json(nlohmann::json& j, const ProcessorRef& r) {
    j = {{"processorId", r.processorId}, {"params", r.params}, {"state", r.state}};
    if (!r.label.empty()) j["label"] = r.label;
    if (r.bypass) j["bypass"] = true;
}

void from_json(const nlohmann::json& j, ProcessorRef& r) {
    j.at("processorId").get_to(r.processorId);
    j.at("params").get_to(r.params);
    j.at("state").get_to(r.state);
    r.label = j.value("label", std::string());
    if (j.contains("bypass")) {
        if (!j["bypass"].is_boolean()) throw std::runtime_error("bypass must be a boolean");
        r.bypass = j["bypass"].get<bool>();
    }
}

namespace {

template <typename E>
void enumToJson(nlohmann::json& j, E value, const std::pair<E, const char*>* table, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (table[i].first == value) {
            j = table[i].second;
            return;
        }
    }
    throw std::runtime_error("unknown enum value");
}

template <typename E>
void enumFromJson(const nlohmann::json& j, E& value, const std::pair<E, const char*>* table, std::size_t n) {
    const std::string s = j.get<std::string>();
    for (std::size_t i = 0; i < n; ++i) {
        if (s == table[i].second) {
            value = table[i].first;
            return;
        }
    }
    throw std::runtime_error("unknown enum string: " + s);
}

const std::pair<TrackKind, const char*> kKinds[] = {
    {TrackKind::Audio, "audio"}, {TrackKind::Midi, "midi"}, {TrackKind::Instrument, "instrument"},
    {TrackKind::Aux, "aux"},     {TrackKind::Bus, "bus"},   {TrackKind::Master, "master"}};
const std::pair<TimeBase, const char*> kBases[] = {{TimeBase::Musical, "musical"}, {TimeBase::Absolute, "absolute"}};

}  // namespace

void to_json(nlohmann::json& j, TrackKind k) { enumToJson(j, k, kKinds, std::size(kKinds)); }
void from_json(const nlohmann::json& j, TrackKind& k) { enumFromJson(j, k, kKinds, std::size(kKinds)); }
void to_json(nlohmann::json& j, TimeBase b) { enumToJson(j, b, kBases, std::size(kBases)); }
void from_json(const nlohmann::json& j, TimeBase& b) { enumFromJson(j, b, kBases, std::size(kBases)); }

void to_json(nlohmann::json& j, const Strip& s) {
    j = {{"gainDb", s.gainDb}, {"pan", s.pan}, {"mute", s.mute}, {"solo", s.solo}, {"inserts", s.inserts}, {"sends", s.sends}, {"output", s.output}};
    if (s.input != 0) j["input"] = s.input;  // written only when set: older projects and files stay as they were
}

void from_json(const nlohmann::json& j, Strip& s) {
    j.at("gainDb").get_to(s.gainDb);
    j.at("pan").get_to(s.pan);
    j.at("mute").get_to(s.mute);
    j.at("solo").get_to(s.solo);
    j.at("inserts").get_to(s.inserts);
    j.at("sends").get_to(s.sends);
    j.at("output").get_to(s.output);
    s.input = j.value("input", 0);
}

void to_json(nlohmann::json& j, const Region& r) {
    j = {{"id", r.id}, {"timeBase", r.timeBase}, {"start", r.start}, {"length", r.length}, {"mediaId", r.mediaId},
         {"sourceOffsetFrames", r.sourceOffsetFrames}, {"gainDb", r.gainDb}, {"notes", r.notes}};
    if (r.fadeIn != 0) j["fadeIn"] = r.fadeIn;  // written only when set: older projects and files stay as they were
    if (r.fadeOut != 0) j["fadeOut"] = r.fadeOut;
}

void from_json(const nlohmann::json& j, Region& r) {
    j.at("id").get_to(r.id);
    j.at("timeBase").get_to(r.timeBase);
    j.at("start").get_to(r.start);
    j.at("length").get_to(r.length);
    j.at("mediaId").get_to(r.mediaId);
    j.at("sourceOffsetFrames").get_to(r.sourceOffsetFrames);
    j.at("gainDb").get_to(r.gainDb);
    j.at("notes").get_to(r.notes);
    r.fadeIn = j.value("fadeIn", std::int64_t{0});
    r.fadeOut = j.value("fadeOut", std::int64_t{0});
}

void to_json(nlohmann::json& j, const MidiNote& n) {
    j = {{"start", n.start}, {"length", n.length}, {"note", n.note}, {"velocity", n.velocity}};
    if (n.muted) j["muted"] = true;  // written only when set, so older projects and files stay as they were
}

void from_json(const nlohmann::json& j, MidiNote& n) {
    // read as int: a plain uint8_t conversion would wrap 300 to 44 without complaint
    const int note = j.at("note").get<int>();
    const int velocity = j.at("velocity").get<int>();
    if (note < 0 || note > 255 || velocity < 0 || velocity > 255) throw std::runtime_error("MIDI note or velocity out of range");
    j.at("start").get_to(n.start);
    j.at("length").get_to(n.length);
    n.note = static_cast<std::uint8_t>(note);
    n.velocity = static_cast<std::uint8_t>(velocity);
    n.muted = j.value("muted", false);
}

void to_json(nlohmann::json& j, const TempoMap& m) {
    j = nlohmann::json::object();
    j["tempos"] = nlohmann::json::array();
    for (const auto& e : m.tempos()) j["tempos"].push_back({{"tick", e.tick}, {"bpm", e.bpm}});
    j["signatures"] = nlohmann::json::array();
    for (const auto& e : m.signatures())
        j["signatures"].push_back({{"tick", e.tick}, {"numerator", e.numerator}, {"denominator", e.denominator}});
}

void from_json(const nlohmann::json& j, TempoMap& m) {
    std::vector<TempoMap::TempoEvent> tempos;
    for (const auto& e : j.at("tempos")) tempos.push_back({e.at("tick").get<Ticks>(), e.at("bpm").get<double>()});
    std::vector<TempoMap::SigEvent> sigs;
    for (const auto& e : j.at("signatures"))
        sigs.push_back({e.at("tick").get<Ticks>(), e.at("numerator").get<int>(), e.at("denominator").get<int>()});
    m = TempoMap::fromEvents(std::move(tempos), std::move(sigs));  // throws std::invalid_argument when invalid
}

void to_json(nlohmann::json& j, const Track& t) {
    j = {{"id", t.id},           {"kind", t.kind},       {"name", t.name},       {"color", t.color},
         {"strip", t.strip},     {"regions", t.regions}, {"automation", t.automation}};
    j["instrument"] = t.instrument ? nlohmann::json(*t.instrument) : nlohmann::json(nullptr);
    if (!t.patchId.empty()) j["patchId"] = t.patchId;
    if (!t.showInTracks) j["showInTracks"] = false;
}

void from_json(const nlohmann::json& j, Track& t) {
    j.at("id").get_to(t.id);
    j.at("kind").get_to(t.kind);
    j.at("name").get_to(t.name);
    j.at("color").get_to(t.color);
    j.at("strip").get_to(t.strip);
    j.at("regions").get_to(t.regions);
    j.at("automation").get_to(t.automation);
    const auto& inst = j.at("instrument");
    t.instrument = inst.is_null() ? std::nullopt : std::optional<ProcessorRef>(inst.get<ProcessorRef>());
    t.patchId = j.value("patchId", std::string());
    if (j.contains("showInTracks")) {
        if (!j["showInTracks"].is_boolean()) throw std::runtime_error("showInTracks must be a boolean");
        t.showInTracks = j["showInTracks"].get<bool>();
    }
}

nlohmann::json toJson(const Project& p) {
    return {{"name", p.name},         {"sampleRate", p.sampleRate}, {"tempoMap", p.tempoMap},
            {"markers", p.markers},   {"tracks", p.tracks},         {"mediaPool", p.mediaPool}};
}

Project projectFromJson(const nlohmann::json& j) {
    if (!j.is_object()) throw std::runtime_error("project document must be a JSON object");
    Project p;
    p.tracks.clear();
    j.at("name").get_to(p.name);
    j.at("sampleRate").get_to(p.sampleRate);
    j.at("tempoMap").get_to(p.tempoMap);
    j.at("markers").get_to(p.markers);
    j.at("tracks").get_to(p.tracks);
    j.at("mediaPool").get_to(p.mediaPool);
    const auto masters = std::count_if(p.tracks.begin(), p.tracks.end(),
                                       [](const Track& t) { return t.kind == TrackKind::Master; });
    if (masters != 1) throw std::runtime_error("project must contain exactly one master track");
    if (const auto problem = checkProject(p)) throw std::runtime_error("invalid project: " + problem->message);
    return p;
}

}  // namespace lpc
