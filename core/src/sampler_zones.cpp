#include "lpc/sampler.h"

#include <nlohmann/json.hpp>

namespace lpc {

std::vector<SamplerZone> samplerZonesOf(const ProcessorRef& instrument) {
    std::vector<SamplerZone> out;
    if (instrument.state.empty()) return out;
    const nlohmann::json j = nlohmann::json::parse(instrument.state, nullptr, false);
    if (!j.is_object() || !j.contains("zones") || !j["zones"].is_array()) return out;
    for (const auto& z : j["zones"]) {
        if (!z.is_object() || !z.contains("mediaId") || !z["mediaId"].is_string()) continue;
        const auto id = Uuid::parse(z["mediaId"].get<std::string>());
        if (!id) continue;
        SamplerZone zone;
        zone.mediaId = *id;
        zone.name = z.value("name", std::string());
        zone.root = z.value("root", 60);
        zone.lo = z.value("lo", 0);
        zone.hi = z.value("hi", 127);
        zone.gainDb = z.value("gainDb", 0.0);
        zone.start = z.value("start", std::int64_t{0});
        zone.end = z.value("end", std::int64_t{0});
        zone.oneShot = z.value("oneShot", false);
        zone.loop = z.value("loop", false);
        out.push_back(std::move(zone));
    }
    return out;
}

std::string samplerStateOf(const std::vector<SamplerZone>& zones) {
    nlohmann::json list = nlohmann::json::array();
    for (const SamplerZone& z : zones) {
        list.push_back({{"mediaId", z.mediaId.toString()}, {"name", z.name}, {"root", z.root}, {"lo", z.lo}, {"hi", z.hi}, {"gainDb", z.gainDb},
                        {"start", z.start}, {"end", z.end}, {"oneShot", z.oneShot}, {"loop", z.loop}});
    }
    return nlohmann::json{{"zones", list}}.dump();
}

std::vector<Uuid> samplerMediaIds(const ProcessorRef& instrument) {
    std::vector<Uuid> out;
    if (instrument.processorId != "builtin.sampler") return out;
    for (const SamplerZone& z : samplerZonesOf(instrument)) out.push_back(z.mediaId);
    return out;
}

std::string checkSamplerZones(const std::vector<SamplerZone>& zones) {
    if (zones.size() > 256) return "a sampler has at most 256 zones";
    for (const SamplerZone& z : zones) {
        if (z.root < 0 || z.root > 127 || z.lo < 0 || z.hi > 127 || z.lo > z.hi) return "the keys of a zone are 0..127, the lowest not above the highest";
        if (!(z.gainDb >= -60.0 && z.gainDb <= 24.0)) return "the gain of a zone is -60..24 dB";
        if (z.start < 0 || z.end < 0 || (z.end > 0 && z.end <= z.start)) return "the start of a zone must come before its end";
    }
    return {};
}

}  // namespace lpc
