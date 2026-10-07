#pragma once
#include <string_view>

namespace lpc {

inline constexpr const char* kProcGain = "builtin.gain";
inline constexpr const char* kProcSine = "builtin.sine";

inline bool isKnownEffect(std::string_view id) { return id == kProcGain; }
inline bool isKnownInstrument(std::string_view id) { return id == kProcSine; }

}  // namespace lpc
