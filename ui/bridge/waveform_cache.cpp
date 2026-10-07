#include "bridge/waveform_cache.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <string>

#include "lpc/wav.h"

namespace jad {

namespace {

std::uint64_t fnv1a(const std::string& bytes) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace

std::filesystem::path WaveformCache::cacheFile(const std::filesystem::path& wav, int buckets) const {
    std::error_code ec;
    const auto size = std::filesystem::file_size(wav, ec);
    if (ec) return {};
    const auto mtime = std::filesystem::last_write_time(wav, ec);
    if (ec) return {};
    const std::u8string path = wav.generic_u8string();
    std::string key(path.begin(), path.end());
    key += '|' + std::to_string(size) + '|' + std::to_string(static_cast<long long>(mtime.time_since_epoch().count())) + '|' + std::to_string(buckets);
    char name[32];
    std::snprintf(name, sizeof name, "%016llx.peaks", static_cast<unsigned long long>(fnv1a(key)));
    return cacheDir_ / name;
}

std::vector<float> WaveformCache::peaks(const std::filesystem::path& wav, int buckets) const {
    if (buckets <= 0) return {};
    if (cacheDir_.empty()) return compute(wav, buckets);

    const std::filesystem::path file = cacheFile(wav, buckets);
    if (file.empty()) return compute(wav, buckets);  // unreadable wav: compute reports it

    const std::size_t bytes = static_cast<std::size_t>(buckets) * sizeof(float);
    {
        std::ifstream in(file, std::ios::binary);
        std::vector<float> cached(static_cast<std::size_t>(buckets));
        if (in && in.read(reinterpret_cast<char*>(cached.data()), static_cast<std::streamsize>(bytes)) &&
            in.gcount() == static_cast<std::streamsize>(bytes) && in.peek() == std::char_traits<char>::eof())
            return cached;  // exactly the expected size: anything else is a damaged entry
    }

    std::vector<float> result = compute(wav, buckets);
    if (result.empty()) return result;
    try {
        std::filesystem::create_directories(cacheDir_);
        std::filesystem::path tmp = file;
        tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(result.data()), static_cast<std::streamsize>(bytes));
            if (!out) return result;
        }
        std::filesystem::rename(tmp, file);
    } catch (const std::exception&) {
        // no cache is fine
    }
    return result;
}

std::vector<float> WaveformCache::compute(const std::filesystem::path& wav, int buckets) const {
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
