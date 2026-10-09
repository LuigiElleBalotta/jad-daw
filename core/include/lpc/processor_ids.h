#pragma once
#include <string_view>

namespace lpc {

inline constexpr const char* kProcGain = "builtin.gain";
inline constexpr const char* kProcSine = "builtin.sine";

// A hosted plug-in: "vst3:" and 32 lowercase hex digits.
inline bool isVst3Id(std::string_view id) {
    constexpr std::string_view prefix = "vst3:";
    if (id.size() != prefix.size() + 32 || id.substr(0, prefix.size()) != prefix) return false;
    for (const char c : id.substr(prefix.size()))
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

inline bool isKnownEffect(std::string_view id) { return id == kProcGain; }
inline bool isKnownInstrument(std::string_view id) { return id == kProcSine; }

}  // namespace lpc
