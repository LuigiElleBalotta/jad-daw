#include "lpc/aiff.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace lpc {

namespace {
void put32(std::ofstream& f, std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) f.put(static_cast<char>((v >> s) & 0xff)); }
void put16(std::ofstream& f, std::uint16_t v) { f.put(static_cast<char>(v >> 8)); f.put(static_cast<char>(v & 0xff)); }

// the 80-bit extended float of a sample rate
void putExtended(std::ofstream& f, double value) {
    int exponent = 0;
    const double fraction = std::frexp(value, &exponent);  // value = fraction * 2^exponent, fraction in [0.5, 1)
    const auto mantissa = static_cast<std::uint64_t>(std::ldexp(fraction, 64));
    put16(f, static_cast<std::uint16_t>(exponent - 1 + 16383));
    for (int s = 56; s >= 0; s -= 8) f.put(static_cast<char>((mantissa >> s) & 0xff));
}
}  // namespace

void writeAiff(const std::filesystem::path& path, int sampleRate, int channels, const std::vector<float>& samples, int bits) {
    if (channels < 1 || channels > 2) throw std::runtime_error("writeAiff: only mono and stereo are supported");
    if (bits != 16 && bits != 24) throw std::runtime_error("writeAiff: 16 or 24 bits");
    const int bytes = bits / 8;
    const std::uint32_t frames = static_cast<std::uint32_t>(samples.size() / static_cast<std::size_t>(channels));
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size()) * static_cast<std::uint32_t>(bytes);
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) throw std::runtime_error("cannot write the file");
    o.write("FORM", 4);
    put32(o, 4 + (8 + 18) + (8 + 8 + dataBytes));
    o.write("AIFF", 4);
    o.write("COMM", 4);
    put32(o, 18);
    put16(o, static_cast<std::uint16_t>(channels));
    put32(o, frames);
    put16(o, static_cast<std::uint16_t>(bits));
    putExtended(o, sampleRate);
    o.write("SSND", 4);
    put32(o, 8 + dataBytes);
    put32(o, 0);
    put32(o, 0);
    for (const float s : samples) {
        const float c = std::clamp(s, -1.0f, 1.0f);
        const std::int32_t v = static_cast<std::int32_t>(std::lround(c * (bits == 16 ? 32767.0f : 8388607.0f)));
        for (int b = bytes - 1; b >= 0; --b) o.put(static_cast<char>((v >> (8 * b)) & 0xff));
    }
    if (!o) throw std::runtime_error("write failed");
}

namespace {
void putBe32(std::ofstream& f, std::uint32_t v) { put32(f, v); }
void putBe64(std::ofstream& f, std::uint64_t v) { put32(f, static_cast<std::uint32_t>(v >> 32)); put32(f, static_cast<std::uint32_t>(v & 0xffffffffu)); }
void putBeDouble(std::ofstream& f, double d) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(d));
    std::memcpy(&bits, &d, sizeof bits);
    putBe64(f, bits);
}
}  // namespace

void writeCaf(const std::filesystem::path& path, int sampleRate, int channels, const std::vector<float>& samples, int bits) {
    if (channels < 1 || channels > 2) throw std::runtime_error("writeCaf: only mono and stereo are supported");
    if (bits != 16 && bits != 24 && bits != 32) throw std::runtime_error("writeCaf: 16, 24 or 32 (float) bits");
    const int bytes = bits / 8;
    const std::uint64_t dataBytes = static_cast<std::uint64_t>(samples.size()) * static_cast<std::uint64_t>(bytes);
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) throw std::runtime_error("cannot write the file");
    o.write("caff", 4);
    o.put(0); o.put(1);  // version 1
    o.put(0); o.put(0);  // flags
    o.write("desc", 4);
    putBe64(o, 32);
    putBeDouble(o, sampleRate);
    o.write("lpcm", 4);
    putBe32(o, bits == 32 ? 3u : 2u);  // bit 0: floating point, bit 1: little-endian samples
    putBe32(o, static_cast<std::uint32_t>(bytes * channels));  // bytes per packet (one frame)
    putBe32(o, 1);                                             // frames per packet
    putBe32(o, static_cast<std::uint32_t>(channels));
    putBe32(o, static_cast<std::uint32_t>(bits));
    o.write("data", 4);
    putBe64(o, dataBytes + 4);
    putBe32(o, 0);  // edit count
    for (const float s : samples) {
        if (bits == 32) {
            std::uint32_t raw = 0;
            std::memcpy(&raw, &s, sizeof raw);
            for (int b = 0; b < 4; ++b) o.put(static_cast<char>((raw >> (8 * b)) & 0xff));
            continue;
        }
        const float c = std::clamp(s, -1.0f, 1.0f);
        const std::int32_t v = static_cast<std::int32_t>(std::lround(c * (bits == 16 ? 32767.0f : 8388607.0f)));
        for (int b = 0; b < bytes; ++b) o.put(static_cast<char>((v >> (8 * b)) & 0xff));
    }
    if (!o) throw std::runtime_error("write failed");
}

}  // namespace lpc
