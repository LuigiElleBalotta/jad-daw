#pragma once
#include <cstddef>
#include <vector>

namespace lpc::ml {

// Logic's real-time neural pitch tracker (RTPT/RTPTM, Core ML converted to ONNX, wrapped in Logic as WrappedNeuralPitchTrackerRT): a CREPE-style network.
// Input: 1024 samples at 16 kHz, normalised to zero mean and unit variance. Output: 360 bins, 20 cents apart from 32.7032 Hz (C1), the probability of
// each pitch. (The origin was found with sine tones: it is 55 cents above CREPE's 31.77 Hz.)
struct PitchPeak {
    double midi = 0.0;  // fractional MIDI note number
    double prob = 0.0;
};

// For frame f the window is centred on sample f * hop of `x` (rate Hz, any rate: it is resampled to 16 kHz). Up to `maxPeaks` peaks above `minProb` per
// frame, strongest first; a frame with no sound has none. Empty vector when the model cannot be loaded.
std::vector<std::vector<PitchPeak>> neuralPitchPeaks(const std::vector<float>& x, int rate, int hop, std::size_t frameCount, double minProb = 0.05, int maxPeaks = 5);

}  // namespace lpc::ml
