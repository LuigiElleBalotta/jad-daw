#pragma once
#include <filesystem>
#include <utility>
#include <vector>

namespace jad {

// Peak overview of a WAV file for drawing: `buckets` values in [0,1], the max absolute sample of the mono-mixed
// channels in each bucket. Safe to call from any thread.
//
// With a cache folder the result is stored there as raw float32 (`<key>.peaks`, the key covers the file's path, size,
// modification time and the bucket count) and read back on the next call. A missing, unwritable or damaged cache
// entry is never an error: the peaks are simply computed again.
class WaveformCache {
public:
    explicit WaveformCache(std::filesystem::path cacheDir = {}) : cacheDir_(std::move(cacheDir)) {}

    // Returns an empty vector when the file is missing, damaged or unsupported.
    std::vector<float> peaks(const std::filesystem::path& wav, int buckets) const;

private:
    std::vector<float> compute(const std::filesystem::path& wav, int buckets) const;
    std::filesystem::path cacheFile(const std::filesystem::path& wav, int buckets) const;

    std::filesystem::path cacheDir_;
};

}  // namespace jad
