#include "bridge/waveform_cache.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "lpc/wav.h"

namespace jad {

std::vector<float> WaveformCache::peaks(const std::filesystem::path& wav, int buckets) const {
    if (buckets <= 0) return {};
    try {
        lpc::WavFile file(wav);
        const std::int64_t frames = file.frames();
        const int channels = file.channels();
        if (frames <= 0 || channels <= 0) return {};

        std::vector<float> out(static_cast<std::size_t>(buckets), 0.0f);
        constexpr std::int64_t kChunk = 16384;
        std::vector<float> buffer(static_cast<std::size_t>(kChunk) * static_cast<std::size_t>(channels));
        for (std::int64_t pos = 0; pos < frames; pos += kChunk) {
            const std::int64_t n = std::min(kChunk, frames - pos);
            file.readFrames(pos, n, buffer.data());
            for (std::int64_t i = 0; i < n; ++i) {
                float mono = 0.0f;
                for (int c = 0; c < channels; ++c) mono += buffer[static_cast<std::size_t>(i * channels + c)];
                mono = std::fabs(mono / static_cast<float>(channels));
                const std::int64_t frame = pos + i;
                const auto bucket = static_cast<std::size_t>(std::min<std::int64_t>(frame * buckets / frames, buckets - 1));
                out[bucket] = std::max(out[bucket], mono);
            }
        }
        for (float& v : out) v = std::min(v, 1.0f);
        return out;
    } catch (const std::exception&) {
        return {};
    }
}

}  // namespace jad
