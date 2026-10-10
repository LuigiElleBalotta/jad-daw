#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace lpc {

inline constexpr const char* kProcGain = "builtin.gain";
inline constexpr const char* kProcSine = "builtin.sine";
inline constexpr const char* kProcSynth = "builtin.synth";
inline constexpr const char* kProcEq = "builtin.eq";
inline constexpr const char* kProcCompressor = "builtin.compressor";
inline constexpr const char* kProcLimiter = "builtin.limiter";
inline constexpr const char* kProcGate = "builtin.gate";
inline constexpr const char* kProcDelay = "builtin.delay";
inline constexpr const char* kProcReverb = "builtin.reverb";

// A hosted plug-in: "vst3:" and 32 lowercase hex digits.
inline bool isVst3Id(std::string_view id) {
    constexpr std::string_view prefix = "vst3:";
    if (id.size() != prefix.size() + 32 || id.substr(0, prefix.size()) != prefix) return false;
    for (const char c : id.substr(prefix.size()))
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

// The target of an automation lane that drives one parameter of a hosted plug-in insert: "param/<vst3 id>/<n>/<parameter index>", where n counts the
// inserts of that plug-in on the track from 0 (so moving other plug-ins around does not change what a lane drives). Values are normalised 0..1.
struct ParamTarget {
    std::string processorId;
    int ordinal = 0;
    int index = 0;
};
inline std::string makeParamTarget(const std::string& processorId, int ordinal, int index) {
    return "param/" + processorId + "/" + std::to_string(ordinal) + "/" + std::to_string(index);
}
inline std::optional<ParamTarget> parseParamTarget(std::string_view target) {
    constexpr std::string_view prefix = "param/";
    if (target.substr(0, prefix.size()) != prefix) return std::nullopt;
    target.remove_prefix(prefix.size());
    const std::size_t a = target.find('/');
    if (a == std::string_view::npos) return std::nullopt;
    const std::size_t b = target.find('/', a + 1);
    if (b == std::string_view::npos) return std::nullopt;
    ParamTarget out;
    out.processorId = std::string(target.substr(0, a));
    if (!isVst3Id(out.processorId)) return std::nullopt;
    const auto number = [](std::string_view s, int& v) {
        if (s.empty() || s.size() > 5) return false;
        v = 0;
        for (const char c : s) {
            if (c < '0' || c > '9') return false;
            v = v * 10 + (c - '0');
        }
        return true;
    };
    if (!number(target.substr(a + 1, b - a - 1), out.ordinal) || !number(target.substr(b + 1), out.index)) return std::nullopt;
    if (out.ordinal > 63 || out.index > 8191) return std::nullopt;
    return out;
}

bool isKnownEffect(std::string_view id);  // a built-in effect (see effect_specs.h)
bool isKnownInstrument(std::string_view id);  // a built-in instrument (see effect_specs.h)

}  // namespace lpc
