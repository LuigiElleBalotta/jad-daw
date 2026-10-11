#include "lpc/mastering.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "lpc/fft.h"

namespace lpc {

namespace {

constexpr double kPi = 3.14159265358979323846;

// the long-term spectrum of the mix (mid channel) summed into one value per band, in dB
std::array<double, kMasteringBands> bandLevels(const WavData& d) {
    constexpr int N = 4096;
    std::array<double, kMasteringBands> power{};
    std::array<int, kMasteringBands> count{};
    const Fft fft(N);
    std::vector<std::complex<double>> buf(N);
    std::vector<double> window(N);
    for (int i = 0; i < N; ++i) window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / N);
    // band edges halfway (geometrically) between the centres
    std::array<double, kMasteringBands + 1> edge{};
    edge[0] = 20.0;
    for (int b = 1; b < kMasteringBands; ++b) edge[static_cast<std::size_t>(b)] = std::sqrt(kMasteringCentres[static_cast<std::size_t>(b - 1)] * kMasteringCentres[static_cast<std::size_t>(b)]);
    edge[kMasteringBands] = std::min(20000.0, 0.45 * d.sampleRate);
    const std::int64_t frames = d.frames();
    for (std::int64_t at = 0; at + N <= frames; at += N / 2) {
        double energy = 0.0;
        for (int i = 0; i < N; ++i) {
            double v = 0.0;
            for (int c = 0; c < d.channels; ++c) v += d.samples[static_cast<std::size_t>((at + i) * d.channels + c)];
            v /= d.channels;
            buf[static_cast<std::size_t>(i)] = v * window[static_cast<std::size_t>(i)];
            energy += v * v;
        }
        if (energy / N < 1e-9) continue;  // silence says nothing about the balance
        fft.forward(buf.data());
        for (int k = 1; k < N / 2; ++k) {
            const double f = static_cast<double>(k) * d.sampleRate / N;
            for (int b = 0; b < kMasteringBands; ++b)
                if (f >= edge[static_cast<std::size_t>(b)] && f < edge[static_cast<std::size_t>(b) + 1]) {
                    power[static_cast<std::size_t>(b)] += std::norm(buf[static_cast<std::size_t>(k)]);
                    break;
                }
        }
        for (int b = 0; b < kMasteringBands; ++b) ++count[static_cast<std::size_t>(b)];
    }
    std::array<double, kMasteringBands> db{};
    for (int b = 0; b < kMasteringBands; ++b) {
        // per-octave level: the band's power divided by the width of the band in octaves
        const double octaves = std::max(0.2, std::log2(edge[static_cast<std::size_t>(b) + 1] / edge[static_cast<std::size_t>(b)]));
        const double p = count[static_cast<std::size_t>(b)] ? power[static_cast<std::size_t>(b)] / count[static_cast<std::size_t>(b)] / octaves : 0.0;
        db[static_cast<std::size_t>(b)] = 10.0 * std::log10(std::max(p, 1e-20));
    }
    return db;
}

}  // namespace

MasteringAnalysis analyseMastering(const WavData& mix) {
    MasteringAnalysis a;
    if (mix.frames() < mix.sampleRate || mix.channels < 1) return a;
    a.before = measureLoudness(mix);
    if (a.before.integrated > -69.0) a.gainDb = std::clamp(kMasteringTargetLufs - a.before.integrated, -24.0, 36.0);
    // the target balance: a gentle tilt of -1.5 dB per octave in the power per octave (typical of a balanced mix: pink noise, flat per octave, sounds too bright), a little less at the extremes
    const std::array<double, kMasteringBands> level = bandLevels(mix);
    std::array<double, kMasteringBands> want{};
    for (int b = 0; b < kMasteringBands; ++b) {
        double w = -1.5 * std::log2(kMasteringCentres[static_cast<std::size_t>(b)] / 1000.0);
        if (b == 0) w -= 3.0;                     // the very low end carries less than a tilt would give
        if (b == kMasteringBands - 1) w -= 2.0;   // and so does the top
        want[static_cast<std::size_t>(b)] = w;
    }
    // compare relative to the middle of the spectrum (bands 250 Hz ... 4 kHz), so the overall level does not matter
    double refLevel = 0.0, refWant = 0.0;
    for (int b = 2; b <= 5; ++b) {
        refLevel += level[static_cast<std::size_t>(b)];
        refWant += want[static_cast<std::size_t>(b)];
    }
    refLevel /= 4.0;
    refWant /= 4.0;
    for (int b = 0; b < kMasteringBands; ++b) {
        const double diff = (want[static_cast<std::size_t>(b)] - refWant) - (level[static_cast<std::size_t>(b)] - refLevel);
        a.eqDb[static_cast<std::size_t>(b)] = std::clamp(0.4 * diff, -4.0, 4.0);  // a bit under half of the way, never more than 4 dB (on a real mix the first version asked for 6 dB on most bands)
    }
    return a;
}

}  // namespace lpc
