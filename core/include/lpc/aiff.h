#pragma once
#include <filesystem>
#include <vector>

namespace lpc {

// Writes interleaved floats (-1..1) as a 16- or 24-bit big-endian PCM AIFF file, mono or stereo. Throws std::runtime_error.
void writeAiff(const std::filesystem::path& path, int sampleRate, int channels, const std::vector<float>& interleaved, int bits);

// Writes interleaved floats (-1..1) as a little-endian linear PCM Core Audio Format (CAF) file: 16- or 24-bit integers, or 32-bit floats; mono or stereo.
void writeCaf(const std::filesystem::path& path, int sampleRate, int channels, const std::vector<float>& interleaved, int bits);

}  // namespace lpc
