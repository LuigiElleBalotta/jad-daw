#pragma once
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "lpc/model.h"

namespace lpc {

inline void to_json(nlohmann::json& j, const Uuid& u) { j = u.toString(); }
inline void from_json(const nlohmann::json& j, Uuid& u) {
    const auto parsed = Uuid::parse(j.get<std::string>());
    if (!parsed) throw std::runtime_error("invalid uuid: " + j.dump());
    u = *parsed;
}

void to_json(nlohmann::json& j, TrackKind k);
void from_json(const nlohmann::json& j, TrackKind& k);
void to_json(nlohmann::json& j, TimeBase b);
void from_json(const nlohmann::json& j, TimeBase& b);
void to_json(nlohmann::json& j, const TempoMap& m);
void from_json(const nlohmann::json& j, TempoMap& m);
void to_json(nlohmann::json& j, const Track& t);
void from_json(const nlohmann::json& j, Track& t);

void to_json(nlohmann::json& j, const MidiNote& n);
void from_json(const nlohmann::json& j, MidiNote& n);  // note and velocity must be in 0..255 before the model checks them
void to_json(nlohmann::json& j, const MidiControl& c);
void from_json(const nlohmann::json& j, MidiControl& c);
void to_json(nlohmann::json& j, const FlexData& f);
void from_json(const nlohmann::json& j, FlexData& f);
void to_json(nlohmann::json& j, const Region& r);
void from_json(const nlohmann::json& j, Region& r);
void to_json(nlohmann::json& j, const LiveCell& c);
void from_json(const nlohmann::json& j, LiveCell& c);
void to_json(nlohmann::json& j, const ProcessorRef& r);
void from_json(const nlohmann::json& j, ProcessorRef& r);  // `label` is optional
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Send, id, targetTrackId, levelDb, preFader)
void to_json(nlohmann::json& j, const Strip& s);
void from_json(const nlohmann::json& j, Strip& s);
void to_json(nlohmann::json& j, const AutomationPoint& p);
void from_json(const nlohmann::json& j, AutomationPoint& p);  // `curve` is optional
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AutomationLane, id, target, points)
inline void to_json(nlohmann::json& j, const Marker& m) {
    j = {{"id", m.id}, {"tick", m.tick}, {"name", m.name}};
    if (!m.text.empty()) j["text"] = m.text;  // written only when there is some: older files stay as they were
}
inline void from_json(const nlohmann::json& j, Marker& m) {
    j.at("id").get_to(m.id);
    j.at("tick").get_to(m.tick);
    j.at("name").get_to(m.name);
    m.text = j.value("text", std::string());
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Group, id, name, members, volume, pan, mute, solo, selection)
void to_json(nlohmann::json& j, const MediaItem& m);
void from_json(const nlohmann::json& j, MediaItem& m);  // `recordedAt` is optional

nlohmann::json toJson(const Project& p);
Project projectFromJson(const nlohmann::json& j);  // throws on any invalid or incomplete document

}  // namespace lpc
