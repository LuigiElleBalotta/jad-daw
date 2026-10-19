#pragma once
#include <vector>

#include "lpc/wav.h"

namespace lpc::ml {

// Logic's DownbeatTrackerCondMPS (a TensorFlow beat / downbeat / tempo / meter network, Core ML converted to ONNX). Input: a log-mel spectrogram, 4096 frames
// x 131 bands; outputs per frame: beat and downbeat activations, a 251-bin tempo histogram (bin = bpm) and a 13-class beats-per-bar distribution
// (class = beats). The exact feature extraction of Logic is not known: this one (22.05 kHz, 2048 FFT, 50 frames per second, 131 Slaney-mel bands
// 30 Hz - 11 kHz, log(1 + magnitude)) makes the network report the right tempo (78 for a 78 bpm recording), 4 beats per bar and beats on the pulse.
// The raw downbeat activation is noisy: Logic decodes it with an HMM; here the beat tracker of tempo_analysis does the decoding.
struct BeatActivation {
    std::vector<double> beats, downbeats;  // per frame, 0..1
    std::vector<double> meter;             // 13 values: the mean probability of 0..12 beats per bar over the recording
    std::vector<double> tempo;             // 251 values: the mean tempo histogram (bin = bpm)
    double fps = 50.0;
    int hopSamples = 441;                  // at `rate`
    double rate = 22050.0;
};

// Empty (no `beats`) when the model cannot be loaded or the sound is shorter than ~3 seconds. Long recordings are processed in overlapping windows.
BeatActivation detectBeatsCnn(const WavData& audio);

}  // namespace lpc::ml
