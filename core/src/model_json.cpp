#include "lpc/model_json.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace lpc {

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
    if (p.sampleRate < 8000 || p.sampleRate > 384000) throw std::runtime_error("unsupported sample rate");
    return p;
}

}  // namespace lpc
