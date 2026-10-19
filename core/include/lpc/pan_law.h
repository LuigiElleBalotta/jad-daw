#pragma once
#include <cmath>
#include <string>
#include <string_view>

namespace lpc {

// Project Settings > Audio > Pan Law. Logic Pro 11.2's engine was decompiled for the "-3 dB" law (docs/logic-reference/mixer.md):
//   t = pan / 200 + 0.5 (pan -100..100), gainL = 1 - t^2, gainR = 1 - (1 - t)^2: a quadratic law, centre 0.75 = -2.498 dB;
//   "Compensated" scales both gains by 4/3, the exact reciprocal of the centre dip, so the centre is unity and the ends are +2.5 dB.
// The other presets were not traced; they follow the same family (gain = 1 - t^k with the centre at the named level, k = 2 for -3 dB,
// k = 1 for -6 dB, k = 1.3066 for -4.5 dB), compensated ones scaled so that the centre is unity.
enum class PanLaw { Zero, Minus3, Minus3Comp, Minus45Comp, Minus6, Minus6Comp };

struct PanGains {
    float l = 1.0f, r = 1.0f;
};

inline const char* panLawName(PanLaw law) {
    switch (law) {
        case PanLaw::Zero: return "0db";
        case PanLaw::Minus3: return "minus3";
        case PanLaw::Minus3Comp: return "minus3comp";
        case PanLaw::Minus45Comp: return "minus45comp";
        case PanLaw::Minus6: return "minus6";
        case PanLaw::Minus6Comp: return "minus6comp";
    }
    return "minus3comp";
}

inline bool panLawFromName(std::string_view name, PanLaw& out) {
    for (PanLaw l : {PanLaw::Zero, PanLaw::Minus3, PanLaw::Minus3Comp, PanLaw::Minus45Comp, PanLaw::Minus6, PanLaw::Minus6Comp})
        if (name == panLawName(l)) {
            out = l;
            return true;
        }
    return false;
}

// The law as Logic shows it ("-3 dB Compensated").
inline const char* panLawLabel(PanLaw law) {
    switch (law) {
        case PanLaw::Zero: return "0 dB (equal gain)";
        case PanLaw::Minus3: return "-3 dB";
        case PanLaw::Minus3Comp: return "-3 dB Compensated";
        case PanLaw::Minus45Comp: return "-4.5 dB Compensated";
        case PanLaw::Minus6: return "-6 dB";
        case PanLaw::Minus6Comp: return "-6 dB Compensated";
    }
    return "-3 dB Compensated";
}

// Gains of a mono signal panned to `pan` (-1 left .. +1 right) under `law`.
inline PanGains panGains(PanLaw law, float pan) {
    const float p = pan < -1.0f ? -1.0f : pan > 1.0f ? 1.0f : pan;
    const float t = p / 2.0f + 0.5f;
    if (law == PanLaw::Zero) return {p > 0.0f ? 1.0f - p : 1.0f, p < 0.0f ? 1.0f + p : 1.0f};
    double k = 2.0, comp = 1.0;
    switch (law) {
        case PanLaw::Minus3: break;
        case PanLaw::Minus3Comp: comp = 4.0 / 3.0; break;
        case PanLaw::Minus45Comp: k = 1.3066; comp = 1.0 / (1.0 - std::pow(0.5, k)); break;
        case PanLaw::Minus6: k = 1.0; break;
        case PanLaw::Minus6Comp: k = 1.0; comp = 2.0; break;
        case PanLaw::Zero: break;
    }
    return {static_cast<float>(comp * (1.0 - std::pow(static_cast<double>(t), k))), static_cast<float>(comp * (1.0 - std::pow(1.0 - static_cast<double>(t), k)))};
}

// Gains of a stereo signal under the balancer: the far side is attenuated, the near side stays (no pan law).
inline PanGains balanceGains(float pan) {
    const float p = pan < -1.0f ? -1.0f : pan > 1.0f ? 1.0f : pan;
    return {p > 0.0f ? 1.0f - p : 1.0f, p < 0.0f ? 1.0f + p : 1.0f};
}

}  // namespace lpc
