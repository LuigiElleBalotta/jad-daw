#include "lpc/audio_decode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define DR_MP3_IMPLEMENTATION
#include "dr_libs/dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_libs/dr_flac.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace lpc {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<unsigned char> readAll(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open the file");
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::uint32_t be32(const unsigned char* p) { return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3]; }
std::uint16_t be16(const unsigned char* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

// the 80-bit extended float of an AIFF sample rate
double extended80(const unsigned char* p) {
    const int exponent = ((p[0] & 0x7f) << 8) | p[1];
    std::uint64_t mantissa = 0;
    for (int i = 0; i < 8; ++i) mantissa = (mantissa << 8) | p[2 + i];
    if (exponent == 0 && mantissa == 0) return 0.0;
    const double v = std::ldexp(static_cast<double>(mantissa), exponent - 16383 - 63);
    return (p[0] & 0x80) ? -v : v;
}

WavData decodeAiff(const std::filesystem::path& path) {
    const std::vector<unsigned char> d = readAll(path);
    if (d.size() < 12 || std::memcmp(d.data(), "FORM", 4) != 0) throw std::runtime_error("not an AIFF file");
    const bool aifc = std::memcmp(d.data() + 8, "AIFC", 4) == 0;
    if (!aifc && std::memcmp(d.data() + 8, "AIFF", 4) != 0) throw std::runtime_error("not an AIFF file");
    int channels = 0, bits = 0;
    std::uint32_t frames = 0;
    double rate = 0;
    bool littleEndian = false;
    const unsigned char* sound = nullptr;
    std::size_t soundBytes = 0;
    std::size_t pos = 12;
    while (pos + 8 <= d.size()) {
        const std::uint32_t size = be32(&d[pos + 4]);
        const unsigned char* body = &d[pos + 8];
        const std::size_t avail = std::min<std::size_t>(size, d.size() - pos - 8);
        if (std::memcmp(&d[pos], "COMM", 4) == 0 && avail >= 18) {
            channels = be16(body);
            frames = be32(body + 2);
            bits = be16(body + 6);
            rate = extended80(body + 8);
            if (aifc && avail >= 22) {
                if (std::memcmp(body + 18, "sowt", 4) == 0) littleEndian = true;
                else if (std::memcmp(body + 18, "NONE", 4) != 0 && std::memcmp(body + 18, "twos", 4) != 0) throw std::runtime_error("compressed AIFF-C files are not supported");
            }
        } else if (std::memcmp(&d[pos], "SSND", 4) == 0 && avail >= 8) {
            const std::uint32_t offset = be32(body);
            if (8 + offset <= avail) { sound = body + 8 + offset; soundBytes = avail - 8 - offset; }
        }
        pos += 8 + size + (size & 1);
    }
    if (!sound || channels < 1 || rate <= 0 || (bits != 8 && bits != 16 && bits != 24 && bits != 32)) throw std::runtime_error("damaged or unsupported AIFF file");
    const int bytes = bits / 8;
    const std::size_t total = std::min<std::size_t>(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels), soundBytes / static_cast<std::size_t>(bytes));
    WavData out;
    out.sampleRate = static_cast<int>(std::lround(rate));
    out.channels = channels;
    out.samples.resize(total - total % static_cast<std::size_t>(channels));
    for (std::size_t i = 0; i < out.samples.size(); ++i) {
        const unsigned char* s = sound + i * static_cast<std::size_t>(bytes);
        std::int32_t v = 0;
        if (littleEndian) for (int b = bytes - 1; b >= 0; --b) v = (v << 8) | s[b];
        else for (int b = 0; b < bytes; ++b) v = (v << 8) | s[b];
        if (bytes == 1) v = static_cast<std::int8_t>(v);
        else if (bytes == 2) v = static_cast<std::int16_t>(v);
        else if (bytes == 3) v = (v & 0x800000) ? (v | ~0xffffff) : v;
        out.samples[i] = static_cast<float>(static_cast<double>(v) / static_cast<double>(1u << (bits - 1)));
    }
    return out;
}

WavData decodeMp3(const std::filesystem::path& path) {
    drmp3 mp3;
    if (!drmp3_init_file_w(&mp3, path.c_str(), nullptr)) throw std::runtime_error("cannot read this MP3 file");
    WavData out;
    out.sampleRate = static_cast<int>(mp3.sampleRate);
    out.channels = static_cast<int>(mp3.channels);
    std::vector<float> chunk(4096 * static_cast<std::size_t>(out.channels));
    for (;;) {
        const drmp3_uint64 got = drmp3_read_pcm_frames_f32(&mp3, 4096, chunk.data());
        if (got == 0) break;
        out.samples.insert(out.samples.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got) * out.channels);
    }
    drmp3_uninit(&mp3);
    if (out.samples.empty()) throw std::runtime_error("this MP3 file has no audio");
    return out;
}

WavData decodeFlac(const std::filesystem::path& path) {
    drflac* flac = drflac_open_file_w(path.c_str(), nullptr);
    if (!flac) throw std::runtime_error("cannot read this FLAC file");
    WavData out;
    out.sampleRate = static_cast<int>(flac->sampleRate);
    out.channels = static_cast<int>(flac->channels);
    std::vector<float> chunk(4096 * static_cast<std::size_t>(out.channels));
    for (;;) {
        const drflac_uint64 got = drflac_read_pcm_frames_f32(flac, 4096, chunk.data());
        if (got == 0) break;
        out.samples.insert(out.samples.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got) * out.channels);
    }
    drflac_close(flac);
    if (out.samples.empty()) throw std::runtime_error("this FLAC file has no audio");
    return out;
}

}  // namespace

bool isImportableAudioExtension(const std::string& extension) {
    const std::string e = lower(extension);
    return e == ".wav" || e == ".mp3" || e == ".flac" || e == ".aif" || e == ".aiff" || e == ".aifc";
}

WavData decodeAudioFile(const std::filesystem::path& path) {
    const std::string ext = lower(path.extension().string());
    if (ext == ".wav") return readWav(path);
    if (ext == ".mp3") return decodeMp3(path);
    if (ext == ".flac") return decodeFlac(path);
    if (ext == ".aif" || ext == ".aiff" || ext == ".aifc") return decodeAiff(path);
    throw std::runtime_error("this file type is not supported (WAV, MP3, FLAC and AIFF are)");
}

WavData convertForProject(const WavData& in, int targetRate, int maxChannels) {
    if (in.channels < 1 || in.sampleRate < 1 || targetRate < 1) throw std::runtime_error("invalid audio data");
    const int channels = std::min(in.channels, std::max(1, maxChannels));
    const std::int64_t frames = in.frames();
    WavData out;
    out.channels = channels;
    out.sampleRate = targetRate;
    if (in.sampleRate == targetRate) {
        out.samples.resize(static_cast<std::size_t>(frames * channels));
        for (std::int64_t i = 0; i < frames; ++i)
            for (int c = 0; c < channels; ++c) out.samples[static_cast<std::size_t>(i * channels + c)] = in.samples[static_cast<std::size_t>(i * in.channels + c)];
        return out;
    }
    const double ratio = static_cast<double>(in.sampleRate) / targetRate;  // input frames per output frame
    const std::int64_t outFrames = static_cast<std::int64_t>(static_cast<double>(frames) / ratio);
    out.samples.resize(static_cast<std::size_t>(outFrames * channels));
    auto at = [&](std::int64_t i, int c) {
        i = std::clamp<std::int64_t>(i, 0, frames - 1);
        return in.samples[static_cast<std::size_t>(i * in.channels + c)];
    };
    for (std::int64_t o = 0; o < outFrames; ++o) {
        const double pos = static_cast<double>(o) * ratio;
        const auto i = static_cast<std::int64_t>(pos);
        const float t = static_cast<float>(pos - static_cast<double>(i));
        for (int c = 0; c < channels; ++c) {  // Catmull-Rom between the two nearest input frames
            const float p0 = at(i - 1, c), p1 = at(i, c), p2 = at(i + 1, c), p3 = at(i + 2, c);
            out.samples[static_cast<std::size_t>(o * channels + c)] =
                p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
        }
    }
    return out;
}

}  // namespace lpc
