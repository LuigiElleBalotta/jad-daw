#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace lpc {

class WavFile {
public:
    // Throws std::runtime_error when the file is missing, damaged or in an unsupported format.
    explicit WavFile(const std::filesystem::path& path);

    int sampleRate() const { return sampleRate_; }
    int channels() const { return channels_; }
    std::int64_t frames() const { return frames_; }

    // Reads `count` frames starting at `start` as interleaved floats. Anything outside the file is zero.
    // Not thread-safe: one reader at a time.
    void readFrames(std::int64_t start, std::int64_t count, float* interleaved);

private:
    std::ifstream in_;
    int sampleRate_ = 0;
    int channels_ = 0;
    int bits_ = 0;
    bool isFloat_ = false;
    int bytesPerSample_ = 0;
    int blockAlign_ = 0;
    std::int64_t dataOffset_ = 0;
    std::int64_t frames_ = 0;
};

struct WavData {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples;  // interleaved
    std::int64_t frames() const { return channels ? static_cast<std::int64_t>(samples.size()) / channels : 0; }
};

WavData readWav(const std::filesystem::path& path);

enum class WavFormat { Pcm16, Pcm24, Float32 };
void writeWav(const std::filesystem::path& path, int sampleRate, int channels, const std::vector<float>& interleaved,
              WavFormat format = WavFormat::Float32);

}  // namespace lpc
