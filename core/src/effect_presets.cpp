#include "lpc/effect_presets.h"

#include <algorithm>
#include <fstream>

#include "lpc/commands.h"
#include "lpc/effect_specs.h"
#include "lpc/match_eq.h"

namespace lpc {

namespace {

// Keeps what the spec knows, each value inside its range
std::map<std::string, double> usable(const std::string& processorId, const nlohmann::json& params) {
    std::map<std::string, double> out;
    const EffectSpec* spec = findEffectSpec(processorId);
    if (!spec || !params.is_object()) return out;
    for (auto it = params.begin(); it != params.end(); ++it) {
        const EffectParam* p = spec->find(it.key());
        if (!p || !it.value().is_number()) continue;
        out[it.key()] = std::clamp(it.value().get<double>(), p->min, p->max);
    }
    return out;
}

}  // namespace

EffectPresetLibrary EffectPresetLibrary::fromJson(const nlohmann::json& doc, const std::string& source) {
    EffectPresetLibrary lib;
    auto problem = [&](const std::string& m) { lib.problems_.push_back((source.empty() ? "" : source + ": ") + m); };
    if (!doc.is_object() || !doc.contains("plugins") || !doc["plugins"].is_object()) {
        problem("no plugins object");
        return lib;
    }
    for (auto it = doc["plugins"].begin(); it != doc["plugins"].end(); ++it) {
        const std::string& id = it.key();
        if (!findEffectSpec(id)) {
            problem("unknown effect " + id);
            continue;
        }
        Plugin plugin;
        if (it.value().contains("default") && it.value()["default"].is_object()) plugin.defaults = usable(id, it.value()["default"]);
        for (const auto& e : it.value().value("presets", nlohmann::json::array())) {
            if (!e.is_object() || !e.contains("name") || !e["name"].is_string()) {
                problem(id + ": a preset without a name");
                continue;
            }
            plugin.presets.push_back(EffectPreset{e.value("category", std::string()), e["name"].get<std::string>(), usable(id, e.value("params", nlohmann::json::object()))});
        }
        lib.plugins_[id] = std::move(plugin);
    }
    if (doc.contains("matchEq") && doc["matchEq"].is_object() && doc["matchEq"].contains("freqHz") && doc["matchEq"].contains("curves")) {
        const std::vector<double> freq = doc["matchEq"]["freqHz"].get<std::vector<double>>();
        for (auto it = doc["matchEq"]["curves"].begin(); it != doc["matchEq"]["curves"].end(); ++it) {
            // a curve is an array on the shared axis, or {freqHz, db} on its own
            const bool own = it.value().is_object();
            const std::vector<double> axis = own ? it.value().value("freqHz", std::vector<double>()) : freq;
            const std::vector<double> mag = own ? it.value().value("db", std::vector<double>()) : it.value().get<std::vector<double>>();
            if (mag.size() != axis.size() || mag.empty()) {
                problem("Match EQ curve " + it.key() + " does not fit its frequency axis");
                continue;
            }
            lib.matchEq_.emplace_back(it.key(), matchEqBands(axis, mag));
        }
    }
    return lib;
}

EffectPresetLibrary EffectPresetLibrary::fromFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        EffectPresetLibrary lib;
        lib.problems_.push_back(file.string() + ": cannot be opened");
        return lib;
    }
    try {
        return fromJson(nlohmann::json::parse(in), file.filename().string());
    } catch (const std::exception& e) {
        EffectPresetLibrary lib;
        lib.problems_.push_back(file.string() + ": " + e.what());
        return lib;
    }
}

const std::vector<EffectPreset>& EffectPresetLibrary::presets(const std::string& processorId) const {
    static const std::vector<EffectPreset> none;
    const auto it = plugins_.find(processorId);
    return it == plugins_.end() ? none : it->second.presets;
}

std::vector<std::string> EffectPresetLibrary::categories(const std::string& processorId) const {
    std::vector<std::string> out;
    for (const EffectPreset& p : presets(processorId))
        if (std::find(out.begin(), out.end(), p.category) == out.end()) out.push_back(p.category);
    return out;
}

const EffectPreset* EffectPresetLibrary::find(const std::string& processorId, const std::string& category, const std::string& name) const {
    for (const EffectPreset& p : presets(processorId))
        if (p.category == category && p.name == name) return &p;
    return nullptr;
}

std::map<std::string, double> EffectPresetLibrary::defaults(const std::string& processorId) const {
    const auto it = plugins_.find(processorId);
    return it == plugins_.end() ? std::map<std::string, double>{} : it->second.defaults;
}

std::vector<std::string> EffectPresetLibrary::matchEqNames() const {
    std::vector<std::string> out;
    for (const auto& c : matchEq_) out.push_back(c.first);
    return out;
}

const std::vector<double>* EffectPresetLibrary::matchEqCurve(const std::string& name) const {
    for (const auto& c : matchEq_)
        if (c.first == name) return &c.second;
    return nullptr;
}

CommandPtr EffectPresetLibrary::applyCommand(const Uuid& trackId, int insertIndex, const std::string& processorId, const std::map<std::string, double>& params) {
    const EffectSpec* spec = findEffectSpec(processorId);
    if (!spec) return nullptr;
    std::vector<CommandPtr> steps;
    for (const EffectParam& p : spec->params) {
        const auto it = params.find(p.name);
        steps.push_back(makeSetInsertParam(trackId, insertIndex, p.name, it == params.end() ? std::nullopt : std::optional<double>(it->second)));
    }
    return makeTransaction(std::move(steps));
}

}  // namespace lpc
