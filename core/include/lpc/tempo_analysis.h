#pragma once
#include <cstdint>
#include <vector>

#include "lpc/wav.h"

namespace lpc {

// Smart Tempo analysis: the tempo of a recording and where its beats fall. It follows the beat tracker of Ellis (2007): an onset strength curve
// (log-compressed spectral flux), a tempo from its autocorrelation weighted towards 120 bpm, then dynamic programming that picks the beats
// that are strong onsets and are about one period apart. The beats are then snapped to the sharpest attack near them.
struct TempoAnalysis {
    double bpm = 0.0;                  // 0 when the sound has no pulse that could be found
    double confidence = 0.0;           // 0..1: how much the pulse stands out
    std::vector<std::int64_t> beats;   // frames of the sound
    int firstDownbeat = 0;             // index (0..beatsPerBar-1) in `beats` of the first beat that opens a bar
    int beatsPerBar = 4;
};

// `beatsPerBar`: 0 finds out whether the bars have three or four beats; 3 or 4 sets it.
// `neural`: the onset curve comes from Logic's OnsetDetectionCNN (lpc::ml, converted to ONNX) when the model can be loaded, as Logic's own Smart Tempo does;
// otherwise, or when false, from the spectral flux.
TempoAnalysis analyseTempo(const WavData& audio, double minBpm = 60.0, double maxBpm = 200.0, int beatsPerBar = 0, bool neural = false);

}  // namespace lpc
