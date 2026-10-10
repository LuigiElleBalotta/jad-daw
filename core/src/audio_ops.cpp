#include "lpc/audio_ops.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace lpc {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

float peakOf(const WavData& d) {
    float p = 0;
    for (const float v : d.samples) p = std::max(p, std::abs(v));
    return p;
}

void applyGain(WavData& d, double gainDb) {
    const float g = static_cast<float>(std::pow(10.0, gainDb / 20.0));
    for (float& v : d.samples) v *= g;
}

double normalize(WavData& d, double targetDb) {
    const float peak = peakOf(d);
    if (peak < 1e-9f) return 0.0;
    const double gain = targetDb - 20.0 * std::log10(static_cast<double>(peak));
    applyGain(d, gain);
    return gain;
}

void reverse(WavData& d) {
    const std::int64_t frames = d.frames();
    const auto ch = static_cast<std::size_t>(d.channels);
    for (std::int64_t i = 0; i < frames / 2; ++i)
        for (std::size_t c = 0; c < ch; ++c)
            std::swap(d.samples[static_cast<std::size_t>(i) * ch + c], d.samples[static_cast<std::size_t>(frames - 1 - i) * ch + c]);
}

void fade(WavData& d, bool in, std::int64_t frames) {
    const std::int64_t total = d.frames();
    frames = std::clamp<std::int64_t>(frames, 0, total);
    const auto ch = static_cast<std::size_t>(d.channels);
    for (std::int64_t i = 0; i < frames; ++i) {
        const float g = static_cast<float>(std::sin(0.5 * kPi * (static_cast<double>(i) + 0.5) / static_cast<double>(frames)));
        const std::int64_t at = in ? i : total - 1 - i;
        for (std::size_t c = 0; c < ch; ++c) d.samples[static_cast<std::size_t>(at) * ch + c] *= g;
    }
}

void silence(WavData& d, std::int64_t from, std::int64_t to) {
    const std::int64_t total = d.frames();
    from = std::clamp<std::int64_t>(from, 0, total);
    to = std::clamp<std::int64_t>(to, from, total);
    const auto ch = static_cast<std::size_t>(d.channels);
    std::fill(d.samples.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(from) * ch), d.samples.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(to) * ch), 0.0f);
}

WavData timeStretch(const WavData& d, double ratio) {
    ratio = std::clamp(ratio, 0.25, 4.0);
    WavData out;
    out.sampleRate = d.sampleRate;
    out.channels = d.channels;
    const std::int64_t F = d.frames();
    if (F == 0) return out;
    const auto ch = static_cast<std::size_t>(d.channels);
    const std::int64_t L = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::llround(static_cast<double>(F) * ratio)));
    out.samples.assign(static_cast<std::size_t>(L) * ch, 0.0f);
    constexpr int N = 2048, Hs = N / 2, kTol = 512, kDec = 4;
    if (F < N * 2) {  // too short for windows: plain resampling would change the pitch, so the sound is repeated or cut
        for (std::int64_t i = 0; i < L; ++i)
            for (std::size_t c = 0; c < ch; ++c) out.samples[static_cast<std::size_t>(i) * ch + c] = d.samples[static_cast<std::size_t>(std::min<std::int64_t>(i, F - 1)) * ch + c];
        return out;
    }
    std::vector<float> win(N);
    for (int i = 0; i < N; ++i) win[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2 * kPi * (i + 0.5) / N));  // Hann: 50 % overlaps add to 1
    std::vector<float> mono(static_cast<std::size_t>(F));
    for (std::int64_t i = 0; i < F; ++i) {
        float m = 0;
        for (std::size_t c = 0; c < ch; ++c) m += d.samples[static_cast<std::size_t>(i) * ch + c];
        mono[static_cast<std::size_t>(i)] = m / static_cast<float>(ch);
    }
    auto at = [&](std::int64_t i) { return i >= 0 && i < F ? mono[static_cast<std::size_t>(i)] : 0.0f; };
    // the correlation of the window at `candidate` with what naturally follows the last chosen window, over Hs samples with a step
    auto score = [&](std::int64_t candidate, std::int64_t reference, int step) {
        double s = 0;
        for (int i = 0; i < Hs; i += step) s += static_cast<double>(at(candidate + i)) * static_cast<double>(at(reference + i));
        return s;
    };
    std::int64_t chosen = 0;  // where the previous window was taken from
    const std::int64_t frames = (L + Hs - 1) / Hs + 1;
    for (std::int64_t k = 0; k < frames; ++k) {
        const double nominal = static_cast<double>(k) * Hs / ratio;
        std::int64_t start = static_cast<std::int64_t>(std::llround(nominal));
        if (k > 0) {
            const std::int64_t reference = chosen + Hs;  // the samples that follow the last window: joining there is seamless
            std::int64_t best = start;
            double bestScore = -1e300;
            for (int delta = -kTol; delta <= kTol; delta += kDec) {  // coarse, on every 4th sample
                const double s = score(start + delta, reference, kDec);
                if (s > bestScore) { bestScore = s; best = start + delta; }
            }
            for (int delta = -kDec; delta <= kDec; ++delta) {          // then fine, around the coarse winner
                const double s = score(best + delta, reference, 1);
                if (s > bestScore) { bestScore = s; best = best + delta; }
            }
            start = best;
        }
        chosen = start;
        const std::int64_t outStart = k * Hs;
        for (int i = 0; i < N; ++i) {
            const std::int64_t o = outStart + i, in = start + i;
            if (o >= L || in < 0 || in >= F) continue;
            const float w = win[static_cast<std::size_t>(i)];
            for (std::size_t c = 0; c < ch; ++c) out.samples[static_cast<std::size_t>(o) * ch + c] += d.samples[static_cast<std::size_t>(in) * ch + c] * w;
        }
    }
    return out;
}

WavData pitchShift(const WavData& d, double semitones) {
    semitones = std::clamp(semitones, -24.0, 24.0);
    if (std::abs(semitones) < 1e-6 || d.frames() == 0) return d;
    const double f = std::pow(2.0, semitones / 12.0);
    const WavData stretched = timeStretch(d, f);  // f times as long (higher: longer) at the old pitch
    WavData out;
    out.sampleRate = d.sampleRate;
    out.channels = d.channels;
    const std::int64_t S = stretched.frames();
    const auto ch = static_cast<std::size_t>(d.channels);
    const std::int64_t L = d.frames();
    out.samples.resize(static_cast<std::size_t>(L) * ch);
    auto at = [&](std::int64_t i, std::size_t c) { return stretched.samples[static_cast<std::size_t>(std::clamp<std::int64_t>(i, 0, S - 1)) * ch + c]; };
    for (std::int64_t o = 0; o < L; ++o) {
        const double pos = static_cast<double>(o) * f;  // read f times faster: the pitch goes up by f, the length is the old one again
        const auto i = static_cast<std::int64_t>(pos);
        const float t = static_cast<float>(pos - static_cast<double>(i));
        for (std::size_t c = 0; c < ch; ++c) {
            const float p0 = at(i - 1, c), p1 = at(i, c), p2 = at(i + 1, c), p3 = at(i + 2, c);
            out.samples[static_cast<std::size_t>(o) * ch + c] = p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
        }
    }
    return out;
}

std::vector<std::pair<std::int64_t, std::int64_t>> findSounds(const WavData& d, double thresholdDb, double minSilenceMs, double minSoundMs, double padMs) {
    std::vector<std::pair<std::int64_t, std::int64_t>> sounds;
    const std::int64_t F = d.frames();
    if (F == 0 || d.sampleRate <= 0) return sounds;
    const auto ch = static_cast<std::size_t>(d.channels);
    const std::int64_t win = std::max<std::int64_t>(1, static_cast<std::int64_t>(0.010 * d.sampleRate));
    const double threshold = std::pow(10.0, thresholdDb / 20.0);
    // 10 ms blocks above the threshold (by RMS), merged into ranges
    std::vector<std::pair<std::int64_t, std::int64_t>> loud;
    for (std::int64_t at = 0; at < F; at += win) {
        const std::int64_t end = std::min(F, at + win);
        double sum = 0;
        for (std::int64_t i = at; i < end; ++i)
            for (std::size_t c = 0; c < ch; ++c) {
                const double v = d.samples[static_cast<std::size_t>(i) * ch + c];
                sum += v * v;
            }
        const double rms = std::sqrt(sum / static_cast<double>((end - at) * static_cast<std::int64_t>(ch)));
        if (rms < threshold) continue;
        if (!loud.empty() && loud.back().second == at) loud.back().second = end;
        else loud.emplace_back(at, end);
    }
    const std::int64_t gap = static_cast<std::int64_t>(minSilenceMs * 0.001 * d.sampleRate);
    for (const auto& r : loud) {
        if (!sounds.empty() && r.first - sounds.back().second < gap) sounds.back().second = r.second;  // a short gap does not split a sound
        else sounds.push_back(r);
    }
    const std::int64_t minSound = static_cast<std::int64_t>(minSoundMs * 0.001 * d.sampleRate), pad = static_cast<std::int64_t>(padMs * 0.001 * d.sampleRate);
    std::vector<std::pair<std::int64_t, std::int64_t>> out;
    for (auto r : sounds) {
        if (r.second - r.first < minSound) continue;
        r.first = std::max<std::int64_t>(0, r.first - pad);
        r.second = std::min(F, r.second + pad);
        if (!out.empty() && r.first <= out.back().second) out.back().second = std::max(out.back().second, r.second);
        else out.push_back(r);
    }
    return out;
}

}  // namespace lpc
