#include "lpc/match_eq.h"

#include <algorithm>
#include <cmath>

namespace lpc {

double matchEqBandFreq(int band) { return 1000.0 * std::pow(10.0, (band - 17) / 10.0); }

std::vector<double> matchEqBands(const std::vector<double>& freqHz, const std::vector<double>& magDb) {
    std::vector<double> out(kMatchEqBands, -120.0);
    const std::size_t n = std::min(freqHz.size(), magDb.size());
    for (int b = 0; b < kMatchEqBands; ++b) {
        const double lo = matchEqBandFreq(b) / std::pow(10.0, 0.05), hi = matchEqBandFreq(b) * std::pow(10.0, 0.05);  // +-1/6 octave
        double power = 0.0;
        int count = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (freqHz[i] < lo || freqHz[i] >= hi) continue;
            power += std::pow(10.0, magDb[i] / 10.0);
            ++count;
        }
        if (count > 0) {
            out[static_cast<std::size_t>(b)] = 10.0 * std::log10(power / count);
            continue;
        }
        // a band with no point inside it (a sparse axis): interpolate linearly in log frequency between the neighbours
        const double f = matchEqBandFreq(b);
        for (std::size_t i = 1; i < n; ++i) {
            if (freqHz[i - 1] > 0.0 && freqHz[i - 1] <= f && f <= freqHz[i]) {
                const double t = (std::log(f) - std::log(freqHz[i - 1])) / (std::log(freqHz[i]) - std::log(freqHz[i - 1]));
                out[static_cast<std::size_t>(b)] = magDb[i - 1] + t * (magDb[i] - magDb[i - 1]);
                break;
            }
        }
    }
    return out;
}

std::vector<double> matchEqCorrection(const std::vector<double>& source, const std::vector<double>& target, double amount, double fadeLowHz, double fadeHighHz, double maxDb) {
    std::vector<double> out(kMatchEqBands, 0.0);
    if (source.size() != static_cast<std::size_t>(kMatchEqBands) || target.size() != static_cast<std::size_t>(kMatchEqBands)) return out;
    double ms = 0.0, mt = 0.0;
    for (int i = 0; i < kMatchEqBands; ++i) {
        ms += source[static_cast<std::size_t>(i)];
        mt += target[static_cast<std::size_t>(i)];
    }
    ms /= kMatchEqBands;
    mt /= kMatchEqBands;
    for (int i = 0; i < kMatchEqBands; ++i) {
        double d = (target[static_cast<std::size_t>(i)] - mt) - (source[static_cast<std::size_t>(i)] - ms);
        const double f = matchEqBandFreq(i);
        double fade = 1.0;  // a half-cosine over one octave below the low edge and above the high edge
        if (f < fadeLowHz) fade = 0.5 - 0.5 * std::cos(std::clamp(std::log2(f / (fadeLowHz / 2.0)), 0.0, 1.0) * 3.14159265358979323846);
        if (f > fadeHighHz) fade = std::min(fade, 0.5 + 0.5 * std::cos(std::clamp(std::log2(f / fadeHighHz), 0.0, 1.0) * 3.14159265358979323846));
        d = std::clamp(d * amount * fade, -maxDb, maxDb);
        out[static_cast<std::size_t>(i)] = d;
    }
    return out;
}

}  // namespace lpc
