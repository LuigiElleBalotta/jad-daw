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

void to_json(nlohmann::json& j, const MediaItem& m) {
    j = {{"id", m.id}, {"path", m.path}, {"hash", m.hash}, {"sampleRate", m.sampleRate}, {"channels", m.channels}, {"frames", m.frames}};
    if (m.recordedAt >= 0) j["recordedAt"] = m.recordedAt;  // written only for a take: older projects and files stay as they were
}

void from_json(const nlohmann::json& j, MediaItem& m) {
    j.at("id").get_to(m.id);
    j.at("path").get_to(m.path);
    j.at("hash").get_to(m.hash);
    j.at("sampleRate").get_to(m.sampleRate);
    j.at("channels").get_to(m.channels);
    j.at("frames").get_to(m.frames);
    m.recordedAt = j.value("recordedAt", std::int64_t{-1});
}

void to_json(nlohmann::json& j, const Region& r) {
    j = {{"id", r.id}, {"timeBase", r.timeBase}, {"start", r.start}, {"length", r.length}, {"mediaId", r.mediaId},
         {"sourceOffsetFrames", r.sourceOffsetFrames}, {"gainDb", r.gainDb}, {"notes", r.notes}};
    if (r.fadeIn != 0) j["fadeIn"] = r.fadeIn;  // written only when set: older projects and files stay as they were
    if (r.fadeOut != 0) j["fadeOut"] = r.fadeOut;
    if (!r.controls.empty()) j["controls"] = r.controls;
    if (r.loopLength != 0) j["loopLength"] = r.loopLength;
    if (r.muted) j["muted"] = true;
    if (!r.takeGroup.empty()) j["takeGroup"] = r.takeGroup;
    if (r.transpose != 0) j["transpose"] = r.transpose;
    if (r.velocityOffset != 0) j["velocityOffset"] = r.velocityOffset;
    if (r.quantize != 0) j["quantize"] = r.quantize;
    if (!r.name.empty()) j["name"] = r.name;
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
    if (j.contains("controls")) j.at("controls").get_to(r.controls);
    r.loopLength = j.value("loopLength", std::int64_t{0});
    r.muted = j.value("muted", false);
    r.takeGroup = j.value("takeGroup", std::string());
    r.transpose = j.value("transpose", 0);
    r.velocityOffset = j.value("velocityOffset", 0);
    r.quantize = j.value("quantize", Ticks{0});
    r.name = j.value("name", std::string());
}

void to_json(nlohmann::json& j, const MidiControl& c) { j = {{"tick", c.tick}, {"status", c.status}, {"data1", c.data1}, {"data2", c.data2}}; }

void from_json(const nlohmann::json& j, MidiControl& c) {
    const int status = j.at("status").get<int>(), d1 = j.at("data1").get<int>(), d2 = j.at("data2").get<int>();
    if (status < 0 || status > 255 || d1 < 0 || d1 > 255 || d2 < 0 || d2 > 255) throw std::runtime_error("MIDI control out of range");
    j.at("tick").get_to(c.tick);
    c.status = static_cast<std::uint8_t>(status);
    c.data1 = static_cast<std::uint8_t>(d1);
    c.data2 = static_cast<std::uint8_t>(d2);
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
    if (t.automationMode != "read") j["automationMode"] = t.automationMode;  // written only when it is not the default
    if (t.delayMs != 0.0) j["delayMs"] = t.delayMs;
    if (t.freeze) j["freeze"] = {{"mediaId", t.freeze->mediaId}, {"startFrame", t.freeze->startFrame}};
    if (!(t.midi == MidiShaping{})) j["midi"] = {{"transpose", t.midi.transpose}, {"velocity", t.midi.velocity}, {"keyLow", t.midi.keyLow}, {"keyHigh", t.midi.keyHigh}, {"velocityLow", t.midi.velocityLow}, {"velocityHigh", t.midi.velocityHigh}};
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
    t.automationMode = j.value("automationMode", std::string("read"));
    t.delayMs = j.value("delayMs", 0.0);
    if (j.contains("freeze") && j["freeze"].is_object()) {
        Freeze f;
        j["freeze"].at("mediaId").get_to(f.mediaId);
        f.startFrame = j["freeze"].value("startFrame", std::int64_t{0});
        t.freeze = f;
    }
    if (j.contains("midi")) {
        const auto& m = j.at("midi");
        t.midi.transpose = m.value("transpose", 0);
        t.midi.velocity = m.value("velocity", 0);
        t.midi.keyLow = m.value("keyLow", 0);
        t.midi.keyHigh = m.value("keyHigh", 127);
        t.midi.velocityLow = m.value("velocityLow", 1);
        t.midi.velocityHigh = m.value("velocityHigh", 127);
    }
    if (j.contains("showInTracks")) {
        if (!j["showInTracks"].is_boolean()) throw std::runtime_error("showInTracks must be a boolean");
        t.showInTracks = j["showInTracks"].get<bool>();
    }
}

nlohmann::json toJson(const Project& p) {
    nlohmann::json j = {{"name", p.name},         {"sampleRate", p.sampleRate}, {"tempoMap", p.tempoMap},
                        {"markers", p.markers},   {"tracks", p.tracks},         {"mediaPool", p.mediaPool}};
    if (!p.groups.empty()) j["groups"] = p.groups;  // only when there are some: older files stay as they were
    return j;
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
    if (j.contains("groups")) j.at("groups").get_to(p.groups);
    const auto masters = std::count_if(p.tracks.begin(), p.tracks.end(),
                                       [](const Track& t) { return t.kind == TrackKind::Master; });
    if (masters != 1) throw std::runtime_error("project must contain exactly one master track");
    if (const auto problem = checkProject(p)) throw std::runtime_error("invalid project: " + problem->message);
    return p;
}

}  // namespace lpc
