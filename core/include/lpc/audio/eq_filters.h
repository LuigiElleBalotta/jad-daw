#pragma once
// The filters of the Channel EQ (eight bands as in Logic Pro 11.2: low cut, low shelf, four peaks, high shelf, high cut).
// Cut slopes are 6 dB/octave per step (1..8 = 6..48 dB/oct), built as Butterworth cascades; the Q of a cut is the resonance at its corner
// (0.71 = none) and goes to the section with the highest Q. Shelves and peaks are the RBJ cookbook filters.
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace lpc::audio {

struct BiquadCoeffs {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

namespace eq_detail {
inline constexpr double kPi = 3.14159265358979323846;
inline double clampFreq(double sr, double f) { return std::clamp(f, 10.0, 0.45 * sr); }
}  // namespace eq_detail

inline BiquadCoeffs lowShelf(double sr, double f, double gainDb, double q = 0.70710678) {
    using eq_detail::kPi;
    const double A = std::pow(10.0, gainDb / 40.0), w = 2 * kPi * eq_detail::clampFreq(sr, f) / sr, c = std::cos(w), s = std::sin(w);
    const double alpha = s / (2.0 * std::max(q, 0.05)), sq = 2.0 * std::sqrt(A) * alpha;
    const double a0 = (A + 1) + (A - 1) * c + sq;
    return {A * ((A + 1) - (A - 1) * c + sq) / a0, 2 * A * ((A - 1) - (A + 1) * c) / a0, A * ((A + 1) - (A - 1) * c - sq) / a0,
            -2 * ((A - 1) + (A + 1) * c) / a0, ((A + 1) + (A - 1) * c - sq) / a0};
}

inline BiquadCoeffs highShelf(double sr, double f, double gainDb, double q = 0.70710678) {
    using eq_detail::kPi;
    const double A = std::pow(10.0, gainDb / 40.0), w = 2 * kPi * eq_detail::clampFreq(sr, f) / sr, c = std::cos(w), s = std::sin(w);
    const double alpha = s / (2.0 * std::max(q, 0.05)), sq = 2.0 * std::sqrt(A) * alpha;
    const double a0 = (A + 1) - (A - 1) * c + sq;
    return {A * ((A + 1) + (A - 1) * c + sq) / a0, -2 * A * ((A - 1) + (A + 1) * c) / a0, A * ((A + 1) + (A - 1) * c - sq) / a0,
            2 * ((A - 1) - (A + 1) * c) / a0, ((A + 1) - (A - 1) * c - sq) / a0};
}

inline BiquadCoeffs peaking(double sr, double f, double gainDb, double q) {
    using eq_detail::kPi;
    const double A = std::pow(10.0, gainDb / 40.0), w = 2 * kPi * eq_detail::clampFreq(sr, f) / sr, c = std::cos(w), alpha = std::sin(w) / (2.0 * std::max(q, 0.05));
    const double a0 = 1 + alpha / A;
    return {(1 + alpha * A) / a0, -2 * c / a0, (1 - alpha * A) / a0, -2 * c / a0, (1 - alpha / A) / a0};
}

inline BiquadCoeffs secondOrderPass(double sr, double f, double q, bool high) {
    using eq_detail::kPi;
    const double w = 2 * kPi * eq_detail::clampFreq(sr, f) / sr, c = std::cos(w), alpha = std::sin(w) / (2.0 * std::max(q, 0.05));
    const double a0 = 1 + alpha;
    const double b = high ? (1 + c) / 2 : (1 - c) / 2;
    return {b / a0, (high ? -(1 + c) : (1 - c)) / a0, b / a0, -2 * c / a0, (1 - alpha) / a0};
}

inline BiquadCoeffs firstOrderPass(double sr, double f, bool high) {
    using eq_detail::kPi;
    const double K = std::tan(kPi * eq_detail::clampFreq(sr, f) / sr);
    const double b0 = (high ? 1.0 : K) / (1 + K);
    return {b0, high ? -b0 : b0, 0.0, (K - 1) / (1 + K), 0.0};
}

// A Butterworth high-pass (low cut) or low-pass (high cut) of order `order` (1..8); `resonanceQ` is 0.71 for a plain Butterworth.
inline std::vector<BiquadCoeffs> cutFilter(double sr, double f, int order, double resonanceQ, bool high) {
    using eq_detail::kPi;
    order = std::clamp(order, 1, 8);
    std::vector<BiquadCoeffs> out;
    if (order % 2) out.push_back(firstOrderPass(sr, f, high));
    std::vector<double> qs;
    for (int k = 1; k <= order / 2; ++k) qs.push_back(1.0 / (2.0 * std::cos((2.0 * k - 1.0) * kPi / (2.0 * order))));
    // the section with the highest Q carries the user's resonance (the last one: the poles closest to the unit circle)
    if (!qs.empty()) qs.back() *= std::max(resonanceQ, 0.05) / 0.70710678;
    for (const double q : qs) out.push_back(secondOrderPass(sr, f, q, high));
    return out;
}

inline std::complex<double> biquadResponse(const BiquadCoeffs& c, double w) {
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2 * w);
    return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
}

}  // namespace lpc::audio
