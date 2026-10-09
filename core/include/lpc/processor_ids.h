#pragma once
#include <string_view>

namespace lpc {

inline constexpr const char* kProcGain = "builtin.gain";
inline constexpr const char* kProcSine = "builtin.sine";
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

bool isKnownEffect(std::string_view id);  // a built-in effect (see effect_specs.h)
inline bool isKnownInstrument(std::string_view id) { return id == kProcSine; }

}  // namespace lpc
