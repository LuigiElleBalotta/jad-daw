#include "lpc/audio/spectrum.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace lpc::audio {

void SpectrumTap::arm(const Uuid& track, int insert, bool post) noexcept {
    armed_.store(false, std::memory_order_release);
    hi_.store(track.hi, std::memory_order_relaxed);
    lo_.store(track.lo, std::memory_order_relaxed);
    insert_.store(insert, std::memory_order_relaxed);
    post_.store(post, std::memory_order_relaxed);
    written_.store(0, std::memory_order_relaxed);
    armed_.store(true, std::memory_order_release);
}

void SpectrumTap::push(const float* l, const float* r, int frames) noexcept {
    std::uint64_t w = written_.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; ++i, ++w) {
        ring_[w & (kSize - 1)].store(0.5f * (l[i] + r[i]), std::memory_order_relaxed);
        ringL_[w & (kSize - 1)].store(l[i], std::memory_order_relaxed);
        ringR_[w & (kSize - 1)].store(r[i], std::memory_order_relaxed);
    }
    written_.store(w, std::memory_order_release);
}

bool SpectrumTap::latest(std::vector<float>& out) const {
    const std::uint64_t w = written_.load(std::memory_order_acquire);
    if (w < kWindow) return false;
    out.resize(kWindow);
    for (std::size_t i = 0; i < kWindow; ++i) out[i] = ring_[(w - kWindow + i) & (kSize - 1)].load(std::memory_order_relaxed);
    return true;
}

bool SpectrumTap::take(std::uint64_t& cursor, std::vector<float>& l, std::vector<float>& r) const {
    const std::uint64_t w = written_.load(std::memory_order_acquire);
    if (w < cursor) cursor = 0;                       // armed again: the count started over
    if (w - cursor > kSize - 512) cursor = w - (kSize - 512);  // too far behind: keep what the audio thread cannot be overwriting
    if (w == cursor) return false;
    const std::size_t n = static_cast<std::size_t>(w - cursor);
    l.resize(n);
    r.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        l[i] = ringL_[(cursor + i) & (kSize - 1)].load(std::memory_order_relaxed);
        r[i] = ringR_[(cursor + i) & (kSize - 1)].load(std::memory_order_relaxed);
    }
    cursor = w;
    return true;
}

namespace {
void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * 3.14159265358979323846 / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
}  // namespace

std::vector<float> spectrumBands(const std::vector<float>& x, double sampleRate, int bands, double fMin, double fMax) {
    std::vector<float> out(static_cast<std::size_t>(std::max(bands, 1)), -120.0f);
    const std::size_t n = x.size();
    if (n < 8 || (n & (n - 1)) != 0 || bands < 1) return out;
    std::vector<std::complex<double>> a(n);
    double windowPower = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(n - 1));
        a[i] = x[i] * w;
        windowPower += w * w;
    }
    fft(a);
    const double binHz = sampleRate / static_cast<double>(n);
    std::vector<double> power(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) power[k] = std::norm(a[k]) * 4.0 / (windowPower * static_cast<double>(n));  // a full-scale sine reads 0 dB per bin
    const double ratio = std::pow(fMax / fMin, 1.0 / bands);
    for (int b = 0; b < bands; ++b) {
        const double lo = fMin * std::pow(ratio, b), hi = lo * ratio;
        const std::size_t k0 = static_cast<std::size_t>(std::max(1.0, std::floor(lo / binHz))), k1 = std::min(n / 2 - 1, static_cast<std::size_t>(std::max<double>(k0, std::ceil(hi / binHz))));
        double peak = 0.0;
        for (std::size_t k = k0; k <= k1; ++k) peak = std::max(peak, power[k]);  // the strongest bin of the band: a tone reads its own level
        out[static_cast<std::size_t>(b)] = static_cast<float>(peak > 1e-12 ? 10.0 * std::log10(peak) : -120.0);
    }
    return out;
}

}  // namespace lpc::audio
