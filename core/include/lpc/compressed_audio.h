#pragma once
#include <filesystem>
#include <vector>

namespace lpc {

enum class CompressedFormat { Mp3, M4aAac };

// True where an encoder exists (Windows Media Foundation: MP3 and AAC; no encoder elsewhere).
bool compressedEncoderAvailable(CompressedFormat format);

// Encodes interleaved floats (-1..1), 1 or 2 channels, to an MP3 (256 kbit/s) or an M4A with AAC (192 kbit/s) file. The encoders take 44.1 and 48 kHz
// only: `sampleRate` must be one of them (see compressedTargetRate). Throws std::runtime_error with the reason.
void writeCompressed(const std::filesystem::path& path, CompressedFormat format, int sampleRate, int channels, const std::vector<float>& interleaved);

// The rate to encode at for a mix at `sampleRate`: the same when it is 44.1 or 48 kHz, else the nearest of the two in its family (88.2 -> 44.1, 96 -> 48).
int compressedTargetRate(int sampleRate);

}  // namespace lpc
