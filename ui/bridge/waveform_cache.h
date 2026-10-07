#pragma once
#include <filesystem>
#include <vector>

namespace jad {

// Peak overview of a WAV file for drawing: `buckets` values in [0,1], the max absolute sample of the mono-mixed
// channels in each bucket. Stateless; safe to call from any thread.
class WaveformCache {
public:
    // Returns an empty vector when the file is missing, damaged or unsupported.
    std::vector<float> peaks(const std::filesystem::path& wav, int buckets) const;
};

}  // namespace jad
