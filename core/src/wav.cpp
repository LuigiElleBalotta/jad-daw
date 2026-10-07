#include "lpc/wav.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace lpc {

namespace {

std::string pathText(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::uint32_t le32(const unsigned char* b) { return b[0] | (b[1] << 8) | (b[2] << 16) | (std::uint32_t(b[3]) << 24); }
std::uint16_t le16(const unsigned char* b) { return static_cast<std::uint16_t>(b[0] | (b[1] << 8)); }

void put16(std::ofstream& o, unsigned v) { const unsigned char b[2] = {static_cast<unsigned char>(v & 0xff), static_cast<unsigned char>((v >> 8) & 0xff)}; o.write(reinterpret_cast<const char*>(b), 2); }
void put32(std::ofstream& o, std::uint32_t v) { const unsigned char b[4] = {static_cast<unsigned char>(v & 0xff), static_cast<unsigned char>((v >> 8) & 0xff), static_cast<unsigned char>((v >> 16) & 0xff), static_cast<unsigned char>((v >> 24) & 0xff)}; o.write(reinterpret_cast<const char*>(b), 4); }

}  // namespace

WavFile::WavFile(const std::filesystem::path& path) : in_(path, std::ios::binary) {
    const std::string name = pathText(path);
    if (!in_) throw std::runtime_error("cannot open " + name);
    in_.seekg(0, std::ios::end);
    const std::int64_t fileSize = in_.tellg();
    in_.seekg(0, std::ios::beg);

    unsigned char head[12];
    in_.read(reinterpret_cast<char*>(head), 12);
    if (in_.gcount() != 12 || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0)
        throw std::runtime_error(name + " is not a RIFF/WAVE file");

    bool haveFmt = false;
    unsigned formatTag = 0;
    std::int64_t pos = 12;
    while (true) {
        unsigned char ch[8];
        in_.seekg(pos);
        in_.read(reinterpret_cast<char*>(ch), 8);
        if (in_.gcount() != 8) throw std::runtime_error(name + " has no data chunk");
        const std::int64_t size = le32(ch + 4);
        const std::int64_t body = pos + 8;
        if (std::memcmp(ch, "fmt ", 4) == 0) {
            if (size < 16) throw std::runtime_error(name + " has a damaged fmt chunk");
            std::vector<unsigned char> f(static_cast<std::size_t>(std::min<std::int64_t>(size, 40)));
            in_.read(reinterpret_cast<char*>(f.data()), static_cast<std::streamsize>(f.size()));
            if (static_cast<std::size_t>(in_.gcount()) != f.size()) throw std::runtime_error(name + " has a truncated fmt chunk");
            formatTag = le16(f.data());
            channels_ = le16(f.data() + 2);
            sampleRate_ = static_cast<int>(le32(f.data() + 4));
            blockAlign_ = le16(f.data() + 12);
            bits_ = le16(f.data() + 14);
            if (formatTag == 0xFFFE) {
                if (f.size() < 26) throw std::runtime_error(name + " has a damaged extensible fmt chunk");
                formatTag = le16(f.data() + 24);  // first two bytes of the sub-format GUID
            }
            haveFmt = true;
        } else if (std::memcmp(ch, "data", 4) == 0) {
            if (!haveFmt) throw std::runtime_error(name + ": data chunk before fmt chunk");
            dataOffset_ = body;
            const std::int64_t available = std::max<std::int64_t>(0, fileSize - body);
            const std::int64_t dataBytes = std::min(size, available);  // truncated files and 0xFFFFFFFF sizes
            // validate the format only now, so that a missing data chunk is reported as such
            if (channels_ < 1 || channels_ > 2) throw std::runtime_error(name + ": only mono and stereo files are supported");
            if (sampleRate_ < 1) throw std::runtime_error(name + ": invalid sample rate");
            isFloat_ = formatTag == 3;
            if (formatTag != 1 && formatTag != 3) throw std::runtime_error(name + ": unsupported encoding (only PCM and float)");
            if (isFloat_ ? bits_ != 32 : !(bits_ == 8 || bits_ == 16 || bits_ == 24 || bits_ == 32))
                throw std::runtime_error(name + ": unsupported bit depth " + std::to_string(bits_));
            bytesPerSample_ = bits_ / 8;
            blockAlign_ = bytesPerSample_ * channels_;
            frames_ = dataBytes / blockAlign_;
            return;
        }
        pos = body + size + (size & 1);  // chunks are padded to an even size
        if (pos > fileSize) throw std::runtime_error(name + " has no data chunk");
    }
}

void WavFile::readFrames(std::int64_t start, std::int64_t count, float* out) {
    const int ch = channels_;
    std::fill(out, out + count * ch, 0.0f);
    const std::int64_t from = std::max<std::int64_t>(start, 0);
    const std::int64_t to = std::min(start + count, frames_);
    if (from >= to) return;

    std::vector<unsigned char> raw(static_cast<std::size_t>((to - from) * blockAlign_));
    in_.clear();
    in_.seekg(dataOffset_ + from * blockAlign_);
    in_.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    const std::int64_t gotFrames = std::min<std::int64_t>(in_.gcount() / blockAlign_, to - from);  // short read: keep zeros

    float* dst = out + (from - start) * ch;
    const unsigned char* p = raw.data();
    for (std::int64_t i = 0; i < gotFrames * ch; ++i, p += bytesPerSample_) {
        float v;
        switch (bits_) {
            case 8: v = (static_cast<int>(p[0]) - 128) / 128.0f; break;
            case 16: v = static_cast<std::int16_t>(le16(p)) / 32768.0f; break;
            case 24: {
                std::int32_t s = p[0] | (p[1] << 8) | (p[2] << 16);
                if (s & 0x800000) s |= ~0xFFFFFF;
                v = static_cast<float>(s) / 8388608.0f;
                break;
            }
            default:
                if (isFloat_) {
                    const std::uint32_t u = le32(p);
                    std::memcpy(&v, &u, 4);
                } else {
                    v = static_cast<float>(static_cast<std::int32_t>(le32(p)) / 2147483648.0);
                }
        }
        dst[i] = v;
    }
}

WavData readWav(const std::filesystem::path& path) {
    WavFile f(path);
    WavData d;
    d.sampleRate = f.sampleRate();
    d.channels = f.channels();
    d.samples.resize(static_cast<std::size_t>(f.frames() * f.channels()));
    f.readFrames(0, f.frames(), d.samples.data());
    return d;
}

void writeWav(const std::filesystem::path& path, int sampleRate, int channels, const std::vector<float>& samples, WavFormat format) {
    if (channels < 1 || channels > 2) throw std::runtime_error("writeWav: only mono and stereo are supported");
    const int bytes = format == WavFormat::Pcm16 ? 2 : format == WavFormat::Pcm24 ? 3 : 4;
    const std::uint32_t dataSize = static_cast<std::uint32_t>(samples.size() * bytes);
    const bool isFloat = format == WavFormat::Float32;

    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) throw std::runtime_error("cannot write " + pathText(path));
    o.write("RIFF", 4);
    put32(o, 4 + (8 + 16) + (isFloat ? 12 : 0) + (8 + dataSize));
    o.write("WAVE", 4);
    o.write("fmt ", 4);
    put32(o, 16);
    put16(o, isFloat ? 3 : 1);
    put16(o, channels);
    put32(o, sampleRate);
    put32(o, sampleRate * channels * bytes);
    put16(o, channels * bytes);
    put16(o, bytes * 8);
    if (isFloat) {
        o.write("fact", 4);
        put32(o, 4);
        put32(o, static_cast<std::uint32_t>(samples.size() / channels));
    }
    o.write("data", 4);
    put32(o, dataSize);
    for (const float s : samples) {
        const float c = std::clamp(s, -1.0f, 1.0f);
        if (format == WavFormat::Float32) {
            std::uint32_t u;
            std::memcpy(&u, &c, 4);
            put32(o, u);
        } else if (format == WavFormat::Pcm16) {
            put16(o, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(c * 32767.0f))));
        } else {
            const std::int32_t v = static_cast<std::int32_t>(std::lround(c * 8388607.0f));
            const unsigned char b[3] = {static_cast<unsigned char>(v & 0xff), static_cast<unsigned char>((v >> 8) & 0xff), static_cast<unsigned char>((v >> 16) & 0xff)};
            o.write(reinterpret_cast<const char*>(b), 3);
        }
    }
    if (!o) throw std::runtime_error("write failed for " + pathText(path));
}

}  // namespace lpc
