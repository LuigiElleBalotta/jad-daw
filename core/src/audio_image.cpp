#include "lpc/audio_image.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <fstream>
#include <stdexcept>

#include "lpc/fft.h"

namespace lpc {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::uint32_t crc32(const std::uint8_t* data, std::size_t n, std::uint32_t crc = 0) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<std::uint8_t>(x >> s));
}

void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& body) {
    put32(out, static_cast<std::uint32_t>(body.size()));
    std::vector<std::uint8_t> t(type, type + 4);
    t.insert(t.end(), body.begin(), body.end());
    out.insert(out.end(), t.begin(), t.end());
    put32(out, crc32(t.data(), t.size()));
}

struct Canvas {
    Image img;
    Canvas(int w, int h) { img.width = w; img.height = h; img.rgb.assign(static_cast<std::size_t>(w) * h * 3, 0); }
    void set(int x, int y, int r, int g, int b) {
        if (x < 0 || y < 0 || x >= img.width || y >= img.height) return;
        std::uint8_t* p = &img.rgb[(static_cast<std::size_t>(y) * img.width + x) * 3];
        p[0] = static_cast<std::uint8_t>(r); p[1] = static_cast<std::uint8_t>(g); p[2] = static_cast<std::uint8_t>(b);
    }
    void hline(int y, int x0, int x1, int r, int g, int b) { for (int x = x0; x <= x1; ++x) set(x, y, r, g, b); }
};

// a dark-to-bright heat scale for 0..1
void heat(double v, int& r, int& g, int& b) {
    v = std::clamp(v, 0.0, 1.0);
    r = static_cast<int>(255 * std::clamp(v * 3.0, 0.0, 1.0));
    g = static_cast<int>(255 * std::clamp(v * 3.0 - 1.0, 0.0, 1.0));
    b = static_cast<int>(255 * std::clamp(v * 3.0 - 2.0, 0.0, 1.0) + 60 * (1.0 - v) * v * 4.0);
    b = std::min(b, 255);
}

std::vector<float> monoOf(const WavData& a) {
    const std::int64_t n = a.frames();
    std::vector<float> m(static_cast<std::size_t>(n));
    for (std::int64_t i = 0; i < n; ++i) {
        float s = 0.0f;
        for (int c = 0; c < a.channels; ++c) s += a.samples[static_cast<std::size_t>(i * a.channels + c)];
        m[static_cast<std::size_t>(i)] = s / static_cast<float>(std::max(1, a.channels));
    }
    return m;
}

}  // namespace

std::vector<std::uint8_t> encodePng(const Image& image) {
    if (image.width <= 0 || image.height <= 0 || image.rgb.size() != static_cast<std::size_t>(image.width) * image.height * 3) throw std::runtime_error("bad image");
    std::vector<std::uint8_t> raw;
    raw.reserve(image.rgb.size() + static_cast<std::size_t>(image.height));
    for (int y = 0; y < image.height; ++y) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), image.rgb.begin() + static_cast<std::ptrdiff_t>(y) * image.width * 3, image.rgb.begin() + static_cast<std::ptrdiff_t>(y + 1) * image.width * 3);
    }
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    for (std::size_t pos = 0; pos < raw.size(); pos += 65535) {
        const std::size_t len = std::min<std::size_t>(65535, raw.size() - pos);
        z.push_back(pos + len >= raw.size() ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(len & 0xFF)); z.push_back(static_cast<std::uint8_t>(len >> 8));
        z.push_back(static_cast<std::uint8_t>(~len & 0xFF)); z.push_back(static_cast<std::uint8_t>((~len >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
    }
    put32(z, (b << 16) | a);
    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, static_cast<std::uint32_t>(image.width));
    put32(ihdr, static_cast<std::uint32_t>(image.height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

void writePng(const std::filesystem::path& path, const Image& image) {
    const std::vector<std::uint8_t> bytes = encodePng(image);
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path.string());
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

Image spectrogramImage(const WavData& audio, int width, int height) {
    Canvas c(width, height);
    const std::vector<float> m = monoOf(audio);
    constexpr int N = 2048;
    if (static_cast<int>(m.size()) < N || audio.sampleRate <= 0) return c.img;
    const Fft fft(N);
    std::vector<double> window(N);
    for (int i = 0; i < N; ++i) window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / N);
    std::vector<std::complex<double>> buf(N);
    const double binHz = static_cast<double>(audio.sampleRate) / N;
    const double fMin = 20.0, fMax = std::min(20000.0, audio.sampleRate / 2.0);
    for (int x = 0; x < width; ++x) {
        const std::int64_t start = static_cast<std::int64_t>((static_cast<double>(m.size() - N) * x) / std::max(1, width - 1));
        for (int i = 0; i < N; ++i) buf[static_cast<std::size_t>(i)] = m[static_cast<std::size_t>(start + i)] * window[static_cast<std::size_t>(i)];
        fft.forward(buf.data());
        for (int y = 0; y < height; ++y) {
            const double f0 = fMin * std::pow(fMax / fMin, static_cast<double>(height - 1 - y) / height);
            const double f1 = fMin * std::pow(fMax / fMin, static_cast<double>(height - y) / height);
            const int k0 = std::clamp(static_cast<int>(f0 / binHz), 1, N / 2 - 1), k1 = std::clamp(static_cast<int>(f1 / binHz), k0, N / 2 - 1);
            double peak = 0.0;
            for (int k = k0; k <= k1; ++k) peak = std::max(peak, std::norm(buf[static_cast<std::size_t>(k)]));
            const double db = 10.0 * std::log10(peak / (N * N / 16.0) + 1e-12);  // a full-scale sine is about 0 dB
            int r, g, b;
            heat((db + 90.0) / 90.0, r, g, b);
            c.set(x, y, r, g, b);
        }
    }
    for (double tick : {100.0, 1000.0, 10000.0}) {  // frequency guides
        if (tick > fMax) continue;
        const int y = height - 1 - static_cast<int>(std::log(tick / fMin) / std::log(fMax / fMin) * height);
        for (int x = 0; x < 12; ++x) c.set(x, y, 255, 255, 255);
    }
    return c.img;
}

Image waveformImage(const WavData& audio, int width, int height) {
    Canvas c(width, height);
    const std::vector<float> m = monoOf(audio);
    if (m.empty()) return c.img;
    const int mid = height / 2;
    for (double db : {-6.0, 6.0}) {  // not drawn: the -6 dB guides are at +-half height
        (void)db;
    }
    c.hline(mid, 0, width - 1, 60, 60, 60);
    c.hline(mid - height / 4, 0, width - 1, 35, 35, 35);
    c.hline(mid + height / 4, 0, width - 1, 35, 35, 35);
    for (int x = 0; x < width; ++x) {
        const std::size_t a = m.size() * static_cast<std::size_t>(x) / width, b = std::max(a + 1, m.size() * static_cast<std::size_t>(x + 1) / width);
        float lo = 0.0f, hi = 0.0f;
        bool clip = false;
        for (std::size_t i = a; i < std::min(b, m.size()); ++i) { lo = std::min(lo, m[i]); hi = std::max(hi, m[i]); }
        for (std::int64_t i = static_cast<std::int64_t>(a); i < static_cast<std::int64_t>(std::min(b, m.size())); ++i)
            for (int ch = 0; ch < audio.channels; ++ch)
                if (std::abs(audio.samples[static_cast<std::size_t>(i * audio.channels + ch)]) >= 0.999f) clip = true;
        const int y0 = mid - static_cast<int>(std::clamp(hi, 0.0f, 1.0f) * (height / 2 - 1)), y1 = mid - static_cast<int>(std::clamp(lo, -1.0f, 0.0f) * (height / 2 - 1));
        for (int y = y0; y <= y1; ++y) clip ? c.set(x, y, 255, 60, 60) : c.set(x, y, 90, 190, 255);
    }
    return c.img;
}

Image spectrumImage(const WavData& audio, int width, int height) {
    Canvas c(width, height);
    const std::vector<float> m = monoOf(audio);
    constexpr int N = 8192;
    if (static_cast<int>(m.size()) < N || audio.sampleRate <= 0) return c.img;
    const Fft fft(N);
    std::vector<double> window(N), power(N / 2, 0.0);
    for (int i = 0; i < N; ++i) window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / N);
    std::vector<std::complex<double>> buf(N);
    int count = 0;
    const std::int64_t step = std::max<std::int64_t>(N / 2, static_cast<std::int64_t>(m.size()) / 300);
    for (std::int64_t at = 0; at + N <= static_cast<std::int64_t>(m.size()); at += step) {
        for (int i = 0; i < N; ++i) buf[static_cast<std::size_t>(i)] = m[static_cast<std::size_t>(at + i)] * window[static_cast<std::size_t>(i)];
        fft.forward(buf.data());
        for (int k = 1; k < N / 2; ++k) power[static_cast<std::size_t>(k)] += std::norm(buf[static_cast<std::size_t>(k)]);
        ++count;
    }
    const double binHz = static_cast<double>(audio.sampleRate) / N;
    const int bands = 31;
    const int bw = std::max(1, width / bands);
    for (int band = 0; band < bands; ++band) {
        const double centre = 10.0 * std::pow(10.0, band / 10.0), lo = centre / std::pow(2.0, 1.0 / 6.0), hi = centre * std::pow(2.0, 1.0 / 6.0);
        double sum = 0.0;
        for (int k = std::max(1, static_cast<int>(lo / binHz)); k <= std::min(N / 2 - 1, static_cast<int>(hi / binHz)); ++k) sum += power[static_cast<std::size_t>(k)];
        const double db = 10.0 * std::log10(sum / std::max(1, count) / (N * N / 16.0) + 1e-12);
        const int h = static_cast<int>(std::clamp((db + 90.0) / 90.0, 0.0, 1.0) * (height - 1));
        int r, g, b;
        heat((db + 90.0) / 90.0, r, g, b);
        for (int x = band * bw; x < band * bw + bw - 1; ++x)
            for (int y = height - 1 - h; y < height; ++y) c.set(x, y, r, g, b);
    }
    return c.img;
}

}  // namespace lpc
