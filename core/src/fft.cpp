#include "lpc/fft.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace lpc {

int Fft::nextPowerOfTwo(int n) {
    int p = 2;
    while (p < n) p <<= 1;
    return p;
}

Fft::Fft(int size) : n_(size) {
    if (size < 2 || (size & (size - 1)) != 0) throw std::invalid_argument("Fft size must be a power of two");
    int bits = 0;
    while ((1 << bits) < n_) ++bits;
    rev_.resize(static_cast<std::size_t>(n_));
    for (int i = 0; i < n_; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        rev_[static_cast<std::size_t>(i)] = r;
    }
    twiddle_.resize(static_cast<std::size_t>(n_ / 2));
    const double pi = 3.14159265358979323846;
    for (int k = 0; k < n_ / 2; ++k) twiddle_[static_cast<std::size_t>(k)] = std::polar(1.0, -2.0 * pi * k / n_);
}

void Fft::run(std::complex<double>* a, bool inverse) const {
    for (int i = 0; i < n_; ++i) {
        const int j = rev_[static_cast<std::size_t>(i)];
        if (j > i) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n_; len <<= 1) {
        const int half = len / 2;
        const int stride = n_ / len;
        for (int start = 0; start < n_; start += len) {
            for (int k = 0; k < half; ++k) {
                std::complex<double> w = twiddle_[static_cast<std::size_t>(k * stride)];
                if (inverse) w = std::conj(w);
                const std::complex<double> u = a[start + k];
                const std::complex<double> v = a[start + k + half] * w;
                a[start + k] = u + v;
                a[start + k + half] = u - v;
            }
        }
    }
    if (inverse) {
        const double s = 1.0 / n_;
        for (int i = 0; i < n_; ++i) a[i] *= s;
    }
}

}  // namespace lpc
