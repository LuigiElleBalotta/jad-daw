#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lpc/command.h"
#include "lpc/uuid.h"

namespace lpc {

struct EffectPreset {
    std::string category, name;
    std::map<std::string, double> params;  // only the parameters the preset sets; the rest go to their defaults
};

// Logic Pro 11.2's factory plug-in presets (core/data/effect_presets.json, made by tools/presets/export_presets.py): Compressor, Channel EQ,
// Noise Gate, Limiter, Adaptive Limiter, and the Match EQ target curves (the 50 stereo and 20 surround genre curves, the Mastering Assistant's four).
class EffectPresetLibrary {
public:
    // Never throws; entries that cannot be used are skipped and reported in problems().
    static EffectPresetLibrary fromJson(const nlohmann::json& doc, const std::string& source = {});
    static EffectPresetLibrary fromFile(const std::filesystem::path& file);

    const std::vector<EffectPreset>& presets(const std::string& processorId) const;
    std::vector<std::string> categories(const std::string& processorId) const;  // in file order, without repeats ("" = presets that have none)
    const EffectPreset* find(const std::string& processorId, const std::string& category, const std::string& name) const;
    std::map<std::string, double> defaults(const std::string& processorId) const;  // Logic's own #default preset (empty when there is none)

    std::vector<std::string> matchEqNames() const;                    // "stereo/Alternative 01", "mastering/AM_Curve_1", ...
    const std::vector<double>* matchEqCurve(const std::string& name) const;  // the target on the 31 third-octave bands, in dB
    const std::vector<std::string>& problems() const { return problems_; }

    // One undo step that sets every parameter of the insert to the preset's value (and the others back to their default).
    static CommandPtr applyCommand(const Uuid& trackId, int insertIndex, const std::string& processorId, const std::map<std::string, double>& params);

private:
    struct Plugin {
        std::map<std::string, double> defaults;
        std::vector<EffectPreset> presets;
    };
    std::map<std::string, Plugin> plugins_;
    std::vector<std::pair<std::string, std::vector<double>>> matchEq_;
    std::vector<std::string> problems_;
};

}  // namespace lpc
