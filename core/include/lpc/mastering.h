#pragma once
#include <array>

#include "lpc/loudness.h"
#include "lpc/wav.h"

namespace lpc {

// Mastering Assistant, the analysis half: what the processor ("builtin.mastering") needs to know about the mix. The processor itself runs the
// correction EQ, the character, the loudness gain and a limiter at -1 dB; this finds the EQ curve and the gain.
inline constexpr int kMasteringBands = 8;
inline constexpr std::array<double, kMasteringBands> kMasteringCentres{40, 100, 250, 630, 1600, 4000, 8000, 14000};
inline constexpr double kMasteringTargetLufs = -14.0;  // the centre of the Loudness knob

struct MasteringAnalysis {
    std::array<double, kMasteringBands> eqDb{};  // the correction per band at Auto EQ 100 %
    double gainDb = 0.0;                          // brings the integrated loudness to kMasteringTargetLufs
    LoudnessStats before;
};

MasteringAnalysis analyseMastering(const WavData& mix);

}  // namespace lpc
