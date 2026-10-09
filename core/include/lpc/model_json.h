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
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Region, id, timeBase, start, length, mediaId, sourceOffsetFrames, gainDb, notes)
void to_json(nlohmann::json& j, const ProcessorRef& r);
void from_json(const nlohmann::json& j, ProcessorRef& r);  // `label` is optional
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Send, id, targetTrackId, levelDb, preFader)
void to_json(nlohmann::json& j, const Strip& s);
void from_json(const nlohmann::json& j, Strip& s);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AutomationPoint, tick, value)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AutomationLane, id, target, points)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Marker, id, tick, name)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MediaItem, id, path, hash, sampleRate, channels, frames)

nlohmann::json toJson(const Project& p);
Project projectFromJson(const nlohmann::json& j);  // throws on any invalid or incomplete document

}  // namespace lpc
