#pragma once
#include <filesystem>
#include <string>

#include "lpc/wav.h"

namespace lpc {

// Audio files the app can bring into a project: WAV, MP3, FLAC, AIFF and Ogg Vorbis. `extension` is lower case with the dot.
bool isImportableAudioExtension(const std::string& extension);

// Reads a whole file as interleaved floats at the file's own rate and channel count. Throws std::runtime_error with a message
// a user can read (unknown format, damaged file, ...).
WavData decodeAudioFile(const std::filesystem::path& path);

// What a project takes: at most `maxChannels` channels (the first ones) at `targetRate` (cubic interpolation).
WavData convertForProject(const WavData& in, int targetRate, int maxChannels = 2);

}  // namespace lpc
