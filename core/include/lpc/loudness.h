#pragma once
#include <cstdint>
#include <vector>

#include "lpc/wav.h"

namespace lpc {

// Loudness as ITU-R BS.1770 / EBU R128 measure it: K-weighting, 400 ms blocks (momentary), 3 s windows (short-term), gated integration, loudness range.
struct LoudnessStats {
    double momentaryMax = -200.0;   // LUFS
    double shortTermMax = -200.0;   // LUFS
    double integrated = -200.0;     // LUFS-I (absolute gate -70, relative gate -10 LU)
    double range = 0.0;             // LU (10th to 95th percentile of the short-term values, relative gate -20 LU)
    double truePeakDb = -200.0;     // dBTP, 4x oversampled
};

LoudnessStats measureLoudness(const float* interleaved, int channels, std::int64_t frames, int sampleRate);
inline LoudnessStats measureLoudness(const WavData& d) { return measureLoudness(d.samples.data(), d.channels, d.frames(), d.sampleRate); }

// The K-weighted mean square per 400 ms block (hop 100 ms) summed over the channels, for callers that want to gate themselves.
std::vector<double> kWeightedBlocks(const float* interleaved, int channels, std::int64_t frames, int sampleRate, double blockSeconds = 0.4);
double blockLufs(double meanSquare);

}  // namespace lpc
