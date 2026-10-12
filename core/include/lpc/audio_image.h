#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

#include "lpc/wav.h"

namespace lpc {

// A picture a model with vision can read: the closest thing to listening for one that has no audio input.
struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgb;  // width * height * 3
};

// Time runs left to right, frequency bottom (20 Hz) to top (20 kHz, logarithmic), colour is level (dB, black = -90, white = 0). Axis ticks at 100, 1k, 10k Hz.
Image spectrogramImage(const WavData& audio, int width = 900, int height = 360);
// Peak envelope of each channel mix over time, with a centre line and -6 dB guides; red where a column reaches full scale (clipping).
Image waveformImage(const WavData& audio, int width = 900, int height = 240);
// The long-term spectrum in third-octave bars (dBFS, -90 to 0).
Image spectrumImage(const WavData& audio, int width = 700, int height = 300);

// PNG (8-bit RGB, stored deflate blocks: no compression library needed).
void writePng(const std::filesystem::path& path, const Image& image);
std::vector<std::uint8_t> encodePng(const Image& image);

}  // namespace lpc
