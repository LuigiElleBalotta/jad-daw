#include "lpc/drum_replace.h"

#include <algorithm>
#include <cmath>

#include "lpc/flex_time.h"

namespace lpc {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Rng {
    unsigned s = 12345;
    double next() {  // -1..1
        s = s * 1664525u + 1013904223u;
        return static_cast<double>(s >> 8) / 8388608.0 - 1.0;
    }
};

WavData mono(int rate, int n) {
    WavData d;
    d.sampleRate = rate;
    d.channels = 1;
    d.samples.assign(static_cast<std::size_t>(n), 0.0f);
    return d;
}

}  // namespace

DrumSound drumSoundFromName(const std::string& name) {
    if (name == "kick") return DrumSound::Kick;
    if (name == "tom") return DrumSound::Tom;
    if (name == "hihat") return DrumSound::HiHat;
    if (name == "clap") return DrumSound::Clap;
    return DrumSound::Snare;
}

WavData synthDrum(DrumSound sound, int rate) {
    Rng rng;
    switch (sound) {
        case DrumSound::Kick: {  // a sine that falls from 150 to 48 Hz, a short click on the front
            WavData d = mono(rate, rate * 4 / 10);
            double phase = 0.0;
            for (int i = 0; i < d.frames(); ++i) {
                const double t = static_cast<double>(i) / rate;
                phase += 2.0 * kPi * (48.0 + 102.0 * std::exp(-t * 38.0)) / rate;
                double v = std::sin(phase) * std::exp(-t * 9.0);
                v += 0.25 * rng.next() * std::exp(-t * 400.0);
                d.samples[static_cast<std::size_t>(i)] = static_cast<float>(0.9 * v);
            }
            return d;
        }
        case DrumSound::Tom: {
            WavData d = mono(rate, rate * 5 / 10);
            double phase = 0.0;
            for (int i = 0; i < d.frames(); ++i) {
                const double t = static_cast<double>(i) / rate;
                phase += 2.0 * kPi * (95.0 + 90.0 * std::exp(-t * 22.0)) / rate;
                d.samples[static_cast<std::size_t>(i)] = static_cast<float>(0.85 * std::sin(phase) * std::exp(-t * 7.0) + 0.1 * rng.next() * std::exp(-t * 250.0));
            }
            return d;
        }
        case DrumSound::HiHat: {  // noise with the lows taken out, fast decay
            WavData d = mono(rate, rate * 12 / 100);
            double lp = 0.0, lp2 = 0.0;
            for (int i = 0; i < d.frames(); ++i) {
                const double t = static_cast<double>(i) / rate;
                const double n = rng.next();
                lp += 0.35 * (n - lp);
                lp2 += 0.35 * (lp - lp2);
                d.samples[static_cast<std::size_t>(i)] = static_cast<float>(0.6 * (n - lp2) * std::exp(-t * 45.0));
            }
            return d;
        }
        case DrumSound::Clap: {  // three quick bursts and a tail
            WavData d = mono(rate, rate * 3 / 10);
            double lp = 0.0;
            for (int i = 0; i < d.frames(); ++i) {
                const double t = static_cast<double>(i) / rate;
                lp += 0.4 * (rng.next() - lp);
                double env = std::exp(-t * 18.0) * 0.5;
                for (double burst : {0.0, 0.010, 0.021})
                    if (t >= burst) env += 0.7 * std::exp(-(t - burst) * 140.0);
                d.samples[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * lp * env);
            }
            return d;
        }
        case DrumSound::Snare:
        default: {  // a tone around 190 Hz and noise, the noise lasting longer
            WavData d = mono(rate, rate * 3 / 10);
            double lp = 0.0;
            for (int i = 0; i < d.frames(); ++i) {
                const double t = static_cast<double>(i) / rate;
                lp += 0.6 * (rng.next() - lp);
                const double tone = std::sin(2.0 * kPi * 190.0 * t) * std::exp(-t * 28.0);
                const double noise = lp * std::exp(-t * 20.0);
                d.samples[static_cast<std::size_t>(i)] = static_cast<float>(0.55 * tone + 0.6 * noise);
            }
            return d;
        }
    }
}

std::vector<DrumHit> findDrumHits(const WavData& audio, double sensitivity, double minGapMs) {
    std::vector<DrumHit> hits;
    if (audio.frames() < 64 || audio.channels < 1) return hits;
    const std::vector<std::int64_t> onsets = findTransients(audio, sensitivity);
    const std::int64_t gap = static_cast<std::int64_t>(minGapMs * 0.001 * audio.sampleRate);
    const std::int64_t reach = std::max<std::int64_t>(8, audio.sampleRate * 12 / 1000);
    std::vector<double> levels;
    for (std::int64_t at : onsets) {
        if (!hits.empty() && at - hits.back().frame < gap) continue;
        double peak = 0.0;
        for (std::int64_t i = at; i < std::min(audio.frames(), at + reach); ++i)
            for (int c = 0; c < audio.channels; ++c) peak = std::max(peak, static_cast<double>(std::abs(audio.samples[static_cast<std::size_t>(i * audio.channels + c)])));
        hits.push_back({at, peak});
        levels.push_back(peak);
    }
    if (hits.empty()) return hits;
    std::sort(levels.begin(), levels.end());
    const double reference = std::max(1e-6, levels[static_cast<std::size_t>(0.9 * static_cast<double>(levels.size() - 1))]);  // the loud hits
    for (DrumHit& h : hits) h.level /= reference;
    return hits;
}

WavData replaceDrums(const WavData& audio, const std::vector<DrumHit>& hits, const WavData& sample, const DrumReplaceParams& p) {
    WavData out = audio;
    const std::int64_t frames = audio.frames();
    if (frames == 0 || sample.frames() == 0) return out;
    const int ch = audio.channels;
    const double gain = std::pow(10.0, p.levelDb / 20.0);
    const std::int64_t pre = std::max<std::int64_t>(1, audio.sampleRate / 1000);  // the original starts to give way a millisecond before
    const std::int64_t hold = static_cast<std::int64_t>(p.duckMs * 0.001 * audio.sampleRate);
    const std::int64_t rel = std::max<std::int64_t>(1, audio.sampleRate * 15 / 1000);
    if (p.replace) {
        std::vector<float> duck(static_cast<std::size_t>(frames), 0.0f);  // the reduction of the original, 0..mix
        for (std::size_t h = 0; h < hits.size(); ++h) {
            const std::int64_t start = hits[h].frame - pre;
            const std::int64_t end = std::min<std::int64_t>(h + 1 < hits.size() ? hits[h + 1].frame - pre : frames, hits[h].frame + hold);
            for (std::int64_t i = std::max<std::int64_t>(0, start); i < std::min(frames, end + rel); ++i) {
                double v = 1.0;
                if (i < hits[h].frame) v = static_cast<double>(i - start) / pre;
                else if (i >= end) v = 1.0 - static_cast<double>(i - end) / rel;
                duck[static_cast<std::size_t>(i)] = std::max(duck[static_cast<std::size_t>(i)], static_cast<float>(std::clamp(v, 0.0, 1.0) * p.mix));
            }
        }
        for (std::int64_t i = 0; i < frames; ++i)
            for (int c = 0; c < ch; ++c) out.samples[static_cast<std::size_t>(i * ch + c)] *= 1.0f - duck[static_cast<std::size_t>(i)];
    }
    for (std::size_t h = 0; h < hits.size(); ++h) {
        const double vel = std::pow(std::clamp(hits[h].level, 0.08, 1.5), std::clamp(p.velocity, 0.0, 1.0));
        const float g = static_cast<float>(gain * vel);
        const std::int64_t limit = h + 1 < hits.size() ? std::min(frames, hits[h + 1].frame) : frames;  // a sound ends where the next hit begins
        const std::int64_t n = std::min<std::int64_t>(sample.frames(), limit - hits[h].frame);
        const std::int64_t fade = std::min<std::int64_t>(n / 2, audio.sampleRate * 3 / 1000);
        for (std::int64_t i = 0; i < n; ++i) {
            const double w = (fade > 0 && n - i < fade) ? static_cast<double>(n - i) / fade : 1.0;
            for (int c = 0; c < ch; ++c) {
                const int sc = std::min(c, sample.channels - 1);
                out.samples[static_cast<std::size_t>((hits[h].frame + i) * ch + c)] += g * static_cast<float>(w) * sample.samples[static_cast<std::size_t>(i * sample.channels + sc)];
            }
        }
    }
    return out;
}

}  // namespace lpc
