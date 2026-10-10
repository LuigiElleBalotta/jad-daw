#pragma once
#include <filesystem>
#include <vector>

namespace lpc {

// Writes interleaved floats (-1..1) as a lossless FLAC file, 16 or 24 bits, 1 to 8 channels. Frames of 4096 samples with fixed
// predictors of order 0 to 4 and Rice-coded residuals (a constant or verbatim subframe where that is smaller). Throws std::runtime_error.
void writeFlac(const std::filesystem::path& path, int sampleRate, int channels, const std::vector<float>& interleaved, int bits);

}  // namespace lpc
