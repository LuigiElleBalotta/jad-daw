#pragma once
#include <vector>

#include "lpc/wav.h"

namespace lpc::ml {

// Logic's OnsetDetectionCNN (Schlueter and Boeck 2014 architecture, shipped as a Core ML model, here converted to ONNX): an onset probability for
// every 10 ms of the recording. The input is three log-magnitude mel spectrograms (window 1024, 2048 and 4096 at 44.1 kHz, 80 bands from 27.5 Hz
// to 16 kHz, unit-area triangles, log10(x + 1)) with 15 frames of context around each frame.
struct OnsetActivation {
    std::vector<double> values;  // probability per frame, frame t is centred on sample t * hopSamples of the 44.1 kHz audio
    double fps = 100.0;
    int hopSamples = 441;
};

// Empty when the model cannot be loaded (no onnxruntime, no OnsetDetectionCNN.espresso.onnx in the model folder) or the sound is too short.
OnsetActivation detectOnsetsCnn(const WavData& audio);

// The same input features, exposed for tests: 3 x 80 x frames, frame-major per channel (index (c * 80 + band) * frames + t)
std::vector<float> onsetFeatures(const WavData& audio, int& frames);

}  // namespace lpc::ml
