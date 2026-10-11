#include "lpc/loudness.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace lpc {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    double run(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// the two K-weighting stages of BS.1770 for any sample rate
std::array<Biquad, 2> kFilters(int rate) {
    std::array<Biquad, 2> f;
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(kPi * f0 / rate), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        f[0].b0 = (Vh + Vb * K / Q + K * K) / a0;
        f[0].b1 = 2.0 * (K * K - Vh) / a0;
        f[0].b2 = (Vh - Vb * K / Q + K * K) / a0;
        f[0].a1 = 2.0 * (K * K - 1.0) / a0;
        f[0].a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(kPi * f0 / rate);
        const double a0 = 1.0 + K / Q + K * K;
        f[1].b0 = 1.0;
        f[1].b1 = -2.0;
        f[1].b2 = 1.0;
        f[1].a1 = 2.0 * (K * K - 1.0) / a0;
        f[1].a2 = (1.0 - K / Q + K * K) / a0;
    }
    return f;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double at = p * static_cast<double>(v.size() - 1);
    const std::size_t i = static_cast<std::size_t>(at);
    const double f = at - static_cast<double>(i);
    return i + 1 < v.size() ? v[i] * (1 - f) + v[i + 1] * f : v[i];
}

// Catmull-Rom between four samples
double cubic(double a, double b, double c, double d, double t) {
    return b + 0.5 * t * (c - a + t * (2.0 * a - 5.0 * b + 4.0 * c - d + t * (3.0 * (b - c) + d - a)));
}

}  // namespace

double blockLufs(double meanSquare) { return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10(meanSquare) : -200.0; }

// the energy (mean square, channels summed) of every window of `window` frames, hopping a quarter of 400 ms
static std::vector<double> windows(const std::vector<double>& energyPerHop, std::size_t hopsPerWindow) {
    std::vector<double> out;
    if (energyPerHop.size() < hopsPerWindow) return out;
    double sum = 0.0;
    for (std::size_t i = 0; i < energyPerHop.size(); ++i) {
        sum += energyPerHop[i];
        if (i >= hopsPerWindow) sum -= energyPerHop[i - hopsPerWindow];
        if (i + 1 >= hopsPerWindow) out.push_back(sum / static_cast<double>(hopsPerWindow));
    }
    return out;
}

// mean square of K-weighted audio per 100 ms hop (channels summed)
static std::vector<double> hopEnergy(const float* x, int channels, std::int64_t frames, int rate) {
    const int nc = std::min(channels, 2);
    std::vector<std::array<Biquad, 2>> filters(static_cast<std::size_t>(nc), kFilters(rate));
    const std::int64_t hop = std::max<std::int64_t>(1, rate / 10);
    std::vector<double> out;
    std::vector<double> acc(static_cast<std::size_t>(nc), 0.0);
    std::int64_t n = 0;
    for (std::int64_t i = 0; i < frames; ++i) {
        for (int c = 0; c < nc; ++c) {
            double v = x[i * channels + c];
            v = filters[static_cast<std::size_t>(c)][0].run(v);
            v = filters[static_cast<std::size_t>(c)][1].run(v);
            acc[static_cast<std::size_t>(c)] += v * v;
        }
        if (++n == hop) {
            double e = 0.0;
            for (double a : acc) e += a / static_cast<double>(hop);
            out.push_back(e);
            std::fill(acc.begin(), acc.end(), 0.0);
            n = 0;
        }
    }
    return out;
}

std::vector<double> kWeightedBlocks(const float* x, int channels, std::int64_t frames, int rate, double blockSeconds) {
    const std::size_t hops = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(blockSeconds * 10.0)));
    return windows(hopEnergy(x, channels, frames, rate), hops);
}

LoudnessStats measureLoudness(const float* x, int channels, std::int64_t frames, int rate) {
    LoudnessStats s;
    if (!x || channels < 1 || frames <= 0 || rate <= 0) return s;
    const std::vector<double> hops = hopEnergy(x, channels, frames, rate);
    const std::vector<double> momentary = windows(hops, 4), shortTerm = windows(hops, 30);
    for (double e : momentary) s.momentaryMax = std::max(s.momentaryMax, blockLufs(e));
    for (double e : shortTerm) s.shortTermMax = std::max(s.shortTermMax, blockLufs(e));
    // integrated: absolute gate, then a gate 10 LU under the mean of what passed
    auto gatedMean = [](const std::vector<double>& blocks, double relative) {
        double sum = 0.0;
        std::size_t n = 0;
        for (double e : blocks)
            if (blockLufs(e) > -70.0) { sum += e; ++n; }
        if (n == 0) return 0.0;
        const double gate = blockLufs(sum / static_cast<double>(n)) + relative;
        double sum2 = 0.0;
        std::size_t n2 = 0;
        for (double e : blocks)
            if (blockLufs(e) > -70.0 && blockLufs(e) > gate) { sum2 += e; ++n2; }
        return n2 ? sum2 / static_cast<double>(n2) : 0.0;
    };
    s.integrated = blockLufs(gatedMean(momentary, -10.0));
    // loudness range from the short-term values
    const double rangeEnergy = gatedMean(shortTerm, -20.0);
    if (rangeEnergy > 0.0) {
        const double gate = blockLufs(rangeEnergy) - 0.0;
        std::vector<double> values;
        double sum = 0.0;
        std::size_t n = 0;
        for (double e : shortTerm)
            if (blockLufs(e) > -70.0) { sum += e; ++n; }
        const double relGate = n ? blockLufs(sum / static_cast<double>(n)) - 20.0 : -70.0;
        (void)gate;
        for (double e : shortTerm)
            if (blockLufs(e) > -70.0 && blockLufs(e) > relGate) values.push_back(blockLufs(e));
        if (values.size() > 1) s.range = percentile(values, 0.95) - percentile(values, 0.10);
    }
    // true peak: the sample peaks and, between samples, three more points of a cubic through the neighbours
    double peak = 0.0;
    for (int c = 0; c < channels; ++c) {
        for (std::int64_t i = 0; i < frames; ++i) {
            auto at = [&](std::int64_t k) { return static_cast<double>(x[std::clamp<std::int64_t>(k, 0, frames - 1) * channels + c]); };
            const double b = at(i);
            peak = std::max(peak, std::abs(b));
            if (i + 1 < frames)
                for (double t : {0.25, 0.5, 0.75}) peak = std::max(peak, std::abs(cubic(at(i - 1), b, at(i + 1), at(i + 2), t)));
        }
    }
    s.truePeakDb = peak > 0.0 ? 20.0 * std::log10(peak) : -200.0;
    return s;
}

}  // namespace lpc
