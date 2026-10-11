#include "lpc/flex_pitch.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>

#include "lpc/fft.h"

namespace lpc {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNegInf = -1e30;

struct Candidate {
    double midi = 0.0;
    double prob = 0.0;
};

// ---------------------------------------------------------------- signal preparation

std::vector<float> mixToMono(const WavData& d) {
    const std::int64_t n = d.frames();
    std::vector<float> mono(static_cast<std::size_t>(n));
    const int ch = std::max(1, d.channels);
    for (std::int64_t i = 0; i < n; ++i) {
        double s = 0.0;
        for (int c = 0; c < ch; ++c) s += d.samples[static_cast<std::size_t>(i * ch + c)];
        mono[static_cast<std::size_t>(i)] = static_cast<float>(s / ch);
    }
    return mono;
}

// Low-pass with a windowed sinc and keep every `factor`-th sample: pitch is searched at about 16 kHz, which is plenty and 4x cheaper.
std::vector<float> decimate(const std::vector<float>& x, int factor) {
    if (factor <= 1) return x;
    const int half = 4 * factor;
    const double fc = 0.45 / factor;
    std::vector<double> h(static_cast<std::size_t>(2 * half + 1));
    double sum = 0.0;
    for (int k = -half; k <= half; ++k) {
        const double sinc = (k == 0) ? 2 * fc : std::sin(2 * kPi * fc * k) / (kPi * k);
        const double win = 0.5 + 0.5 * std::cos(kPi * k / (half + 1));
        h[static_cast<std::size_t>(k + half)] = sinc * win;
        sum += sinc * win;
    }
    for (double& v : h) v /= sum;
    const std::int64_t n = static_cast<std::int64_t>(x.size());
    std::vector<float> y(static_cast<std::size_t>((n + factor - 1) / factor));
    for (std::size_t m = 0; m < y.size(); ++m) {
        const std::int64_t centre = static_cast<std::int64_t>(m) * factor;
        double acc = 0.0;
        for (int k = -half; k <= half; ++k) {
            const std::int64_t i = centre + k;
            if (i >= 0 && i < n) acc += h[static_cast<std::size_t>(k + half)] * x[static_cast<std::size_t>(i)];
        }
        y[m] = static_cast<float>(acc);
    }
    return y;
}

// ---------------------------------------------------------------- pYIN, stage 1: candidates of a frame

struct Yin {
    int rate = 0;  // sample rate of the signal analysed here
    int w = 0;     // integration window
    int tauMin = 2, tauMax = 0;
    int n = 0;     // FFT size
    std::vector<double> thresholdWeights;  // P(s_i) for s_i = 0.01 .. 1.00
    Fft fft;

    Yin(int sampleRate, double minHz, double maxHz, double meanThreshold)
        : rate(sampleRate),
          w(std::max(64, static_cast<int>(0.032 * sampleRate))),
          tauMax(std::min(w, static_cast<int>(sampleRate / minHz))),
          n(Fft::nextPowerOfTwo(w + tauMax)),
          fft(n) {
        tauMin = std::max(2, static_cast<int>(sampleRate / maxHz));
        // Beta(2, b) with the requested mean: b = 2 (1 - m) / m
        const double m = std::clamp(meanThreshold, 0.02, 0.5);
        const double b = 2.0 * (1.0 - m) / m;
        thresholdWeights.resize(100);
        double total = 0.0;
        for (int i = 0; i < 100; ++i) {
            const double s = 0.01 * (i + 1);
            const double pdf = b * (b + 1) * s * std::pow(1.0 - s, b - 1);
            thresholdWeights[static_cast<std::size_t>(i)] = pdf;
            total += pdf;
        }
        for (double& v : thresholdWeights) v /= total;
    }

    // `x` is the whole signal, the window starts at `start` (it may reach outside the signal: zeros there).
    std::vector<Candidate> candidates(const std::vector<float>& x, const std::vector<double>& energyPrefix, std::int64_t start) const {
        const std::int64_t len = static_cast<std::int64_t>(x.size());
        auto at = [&](std::int64_t i) -> double { return (i >= 0 && i < len) ? x[static_cast<std::size_t>(i)] : 0.0; };
        auto energy = [&](std::int64_t from, std::int64_t count) {  // sum of squares of [from, from + count)
            const std::int64_t a = std::clamp<std::int64_t>(from, 0, len), b = std::clamp<std::int64_t>(from + count, 0, len);
            return energyPrefix[static_cast<std::size_t>(b)] - energyPrefix[static_cast<std::size_t>(a)];
        };
        const double ea = energy(start, w);
        if (ea / w < 1e-9) return {};

        // cross-correlation of a = x[start, start + w) with b = x[start, start + w + tauMax) by one FFT of a + i b
        std::vector<std::complex<double>> z(static_cast<std::size_t>(n));
        for (int i = 0; i < w + tauMax; ++i) z[static_cast<std::size_t>(i)] = {i < w ? at(start + i) : 0.0, at(start + i)};
        fft.forward(z.data());
        std::vector<std::complex<double>> c(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k) {
            const std::complex<double> zk = z[static_cast<std::size_t>(k)];
            const std::complex<double> zn = std::conj(z[static_cast<std::size_t>((n - k) % n)]);
            const std::complex<double> a = 0.5 * (zk + zn);
            const std::complex<double> b = std::complex<double>(0, -0.5) * (zk - zn);
            c[static_cast<std::size_t>(k)] = std::conj(a) * b;
        }
        fft.inverse(c.data());

        std::vector<double> d(static_cast<std::size_t>(tauMax + 1), 0.0), dn(static_cast<std::size_t>(tauMax + 1), 1.0);
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau) {
            const double eb = energy(start + tau, w);
            d[static_cast<std::size_t>(tau)] = std::max(0.0, ea + eb - 2.0 * c[static_cast<std::size_t>(tau)].real());
            running += d[static_cast<std::size_t>(tau)];
            dn[static_cast<std::size_t>(tau)] = running > 0 ? d[static_cast<std::size_t>(tau)] * tau / running : 1.0;
        }

        // the dips of the normalised difference, and the lowest of all
        std::vector<int> dips;
        for (int tau = std::max(tauMin, 2); tau < tauMax; ++tau)
            if (dn[static_cast<std::size_t>(tau)] < dn[static_cast<std::size_t>(tau - 1)] && dn[static_cast<std::size_t>(tau)] <= dn[static_cast<std::size_t>(tau + 1)])
                dips.push_back(tau);
        int lowest = std::max(tauMin, 1);
        for (int tau = lowest; tau <= tauMax; ++tau)
            if (dn[static_cast<std::size_t>(tau)] < dn[static_cast<std::size_t>(lowest)]) lowest = tau;

        constexpr double kAperiodic = 0.01;  // the weight of a threshold that no dip passes
        std::vector<std::pair<int, double>> picked;  // (tau, probability)
        auto add = [&](int tau, double p) {
            for (auto& e : picked)
                if (e.first == tau) { e.second += p; return; }
            picked.emplace_back(tau, p);
        };
        for (int i = 0; i < 100; ++i) {
            const double s = 0.01 * (i + 1);
            const double weight = thresholdWeights[static_cast<std::size_t>(i)];
            int choice = -1;
            for (int tau : dips)
                if (dn[static_cast<std::size_t>(tau)] < s) { choice = tau; break; }
            if (choice >= 0) add(choice, weight);
            else add(lowest, weight * kAperiodic);
        }

        std::vector<Candidate> out;
        for (const auto& e : picked) {
            const int tau = e.first;
            double refined = tau;
            if (tau > 1 && tau < tauMax) {
                const double a = d[static_cast<std::size_t>(tau - 1)], b = d[static_cast<std::size_t>(tau)], cc = d[static_cast<std::size_t>(tau + 1)];
                const double den = a - 2 * b + cc;
                if (den > 1e-12) refined += std::clamp(0.5 * (a - cc) / den, -1.0, 1.0);
            }
            const double hz = rate / refined;
            if (hz < rate / static_cast<double>(tauMax) * 0.99 || hz > rate / static_cast<double>(tauMin) * 1.01) continue;
            out.push_back({hzToMidi(hz), e.second});
        }
        return out;
    }
};

// ---------------------------------------------------------------- pYIN, stage 2: the MonoNote hidden Markov model

struct NoteModel {
    static constexpr int kPerSemitone = 3;  // pitches per semitone
    static constexpr int kKinds = 3;        // attack, stable, silent
    double minMidi = 30.0;
    int nPitch = 0, nState = 0;
    // parameters (the transition ones are per frame of 5.8 ms and are rescaled to the real hop)
    double pAttackSelf = 0.9, pStableSelf = 0.99, pStableToSilent = 0.01, pSilentSelf = 0.9999;
    double sigmaNote = 0.7, minSemitoneDistance = 0.5, maxJump = 13;
    double sigmaAttack = 5.0, sigmaStable = 0.8, yinTrust = 0.1, priorPitched = 0.7, priorWeight = 0.5;
    std::vector<int> from, to;
    std::vector<float> logProb;

    NoteModel(double lowMidi, double highMidi, double hopSeconds) {
        minMidi = lowMidi;
        nPitch = std::max(1, static_cast<int>(std::ceil((highMidi - lowMidi) * kPerSemitone)));
        nState = nPitch * kKinds;
        const double scale = hopSeconds / 0.0058;
        pAttackSelf = std::pow(pAttackSelf, scale);
        pStableSelf = std::pow(pStableSelf, scale);
        pStableToSilent = 1.0 - pStableSelf;
        pSilentSelf = std::pow(pSilentSelf, scale);
        auto add = [&](int f, int t, double p) {
            from.push_back(f);
            to.push_back(t);
            logProb.push_back(static_cast<float>(std::log(std::max(p, 1e-300))));
        };
        for (int p = 0; p < nPitch; ++p) {
            const int i = p * kKinds;
            add(i, i, pAttackSelf);
            add(i, i + 1, 1.0 - pAttackSelf);
            add(i + 1, i + 1, pStableSelf);
            add(i + 1, i + 2, pStableToSilent);
            add(i + 2, i + 2, pSilentSelf);
            std::vector<std::pair<int, double>> targets;
            double total = 0.0;
            for (int q = 0; q < nPitch; ++q) {
                const double dist = std::abs(p - q) / static_cast<double>(kPerSemitone);
                if (dist == 0.0 || (dist > minSemitoneDistance && dist < maxJump)) {
                    const double wgt = std::exp(-0.5 * dist * dist / (sigmaNote * sigmaNote));
                    targets.emplace_back(q, wgt);
                    total += wgt;
                }
            }
            for (const auto& t : targets) add(i + 2, t.first * kKinds, (1.0 - pSilentSelf) * t.second / total);
        }
    }

    double midiOf(int pitchIndex) const { return minMidi + static_cast<double>(pitchIndex) / kPerSemitone; }

    // log observation probabilities of the frame's candidates for every state
    void observe(const std::vector<Candidate>& cands, std::vector<double>& out) const {
        out.assign(static_cast<std::size_t>(nState), kNegInf);
        double pitched = 0.0;
        for (const Candidate& c : cands) pitched += c.prob;
        pitched = std::clamp(pitched * (1.0 - priorWeight) + priorPitched * priorWeight, 1e-6, 1.0 - 1e-6);
        double maxLog = kNegInf;
        for (int p = 0; p < nPitch; ++p) {
            const double mu = midiOf(p);
            double tempAttack = 0.0, tempStable = 0.0;
            if (!cands.empty()) {
                const Candidate* best = &cands[0];
                for (const Candidate& c : cands)
                    if (std::abs(c.midi - mu) < std::abs(best->midi - mu)) best = &c;
                const double trust = yinTrust * std::log(std::max(best->prob, 1e-12));
                auto logPdf = [&](double sigma) {
                    const double z = (best->midi - mu) / sigma;
                    return -0.5 * z * z - std::log(sigma * std::sqrt(2 * kPi));
                };
                tempAttack = trust + logPdf(sigmaAttack);
                tempStable = trust + logPdf(sigmaStable);
            }
            out[static_cast<std::size_t>(p * kKinds)] = tempAttack;
            out[static_cast<std::size_t>(p * kKinds + 1)] = tempStable;
            maxLog = std::max({maxLog, tempAttack, tempStable});
        }
        double sum = 0.0;
        for (int p = 0; p < nPitch; ++p)
            for (int k = 0; k < 2; ++k) sum += std::exp(out[static_cast<std::size_t>(p * kKinds + k)] - maxLog);
        const double logNorm = maxLog + std::log(sum);
        for (int p = 0; p < nPitch; ++p) {
            for (int k = 0; k < 2; ++k) out[static_cast<std::size_t>(p * kKinds + k)] += std::log(pitched) - logNorm;
            out[static_cast<std::size_t>(p * kKinds + 2)] = std::log((1.0 - pitched) / nPitch);
        }
    }

    // Viterbi: the most likely state of every frame
    std::vector<int> decode(const std::vector<std::vector<Candidate>>& frames) const {
        const std::size_t t = frames.size();
        std::vector<int> path(t, 0);
        if (t == 0) return path;
        std::vector<std::int16_t> back(t * static_cast<std::size_t>(nState), 0);
        std::vector<double> delta(static_cast<std::size_t>(nState), kNegInf), next(delta), obs;
        for (int p = 0; p < nPitch; ++p) delta[static_cast<std::size_t>(p * kKinds + 2)] = std::log(1.0 / nPitch);
        observe(frames[0], obs);
        for (int s = 0; s < nState; ++s) delta[static_cast<std::size_t>(s)] += obs[static_cast<std::size_t>(s)];
        for (std::size_t f = 1; f < t; ++f) {
            std::fill(next.begin(), next.end(), kNegInf);
            std::int16_t* bp = &back[f * static_cast<std::size_t>(nState)];
            for (std::size_t k = 0; k < from.size(); ++k) {
                const double v = delta[static_cast<std::size_t>(from[k])] + logProb[k];
                if (v > next[static_cast<std::size_t>(to[k])]) {
                    next[static_cast<std::size_t>(to[k])] = v;
                    bp[to[k]] = static_cast<std::int16_t>(from[k]);
                }
            }
            observe(frames[f], obs);
            for (int s = 0; s < nState; ++s) next[static_cast<std::size_t>(s)] += obs[static_cast<std::size_t>(s)];
            std::swap(delta, next);
        }
        int best = 0;
        for (int s = 1; s < nState; ++s)
            if (delta[static_cast<std::size_t>(s)] > delta[static_cast<std::size_t>(best)]) best = s;
        for (std::size_t f = t; f-- > 0;) {
            path[f] = best;
            if (f > 0) best = back[f * static_cast<std::size_t>(nState) + static_cast<std::size_t>(best)];
        }
        return path;
    }
};

double medianOf(std::vector<double> v) {
    if (v.empty()) return 0.0;
    const std::size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    return v[mid];
}

}  // namespace

double midiToHz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }
double hzToMidi(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }

double PitchAnalysis::midiAt(std::int64_t pos) const {
    if (midi.empty() || hop <= 0) return 0.0;
    const double x = static_cast<double>(pos) / hop;
    const std::int64_t i0 = static_cast<std::int64_t>(std::floor(x));
    const double frac = x - static_cast<double>(i0);
    auto get = [&](std::int64_t i) { return (i >= 0 && i < static_cast<std::int64_t>(midi.size())) ? static_cast<double>(midi[static_cast<std::size_t>(i)]) : 0.0; };
    const double a = get(i0), b = get(i0 + 1);
    if (a > 0 && b > 0) return a + (b - a) * frac;
    if (a > 0 && frac < 0.5) return a;
    if (b > 0 && frac >= 0.5) return b;
    return 0.0;
}

PitchAnalysis analysePitch(const WavData& d, const PitchOptions& options) {
    PitchAnalysis out;
    out.sampleRate = d.sampleRate;
    out.frames = d.frames();
    if (d.sampleRate <= 0 || d.channels <= 0 || d.frames() < d.sampleRate / 20) {
        out.hop = std::max(1, d.sampleRate / 100);
        return out;
    }
    const int factor = std::max(1, d.sampleRate / 16000);
    const int rate = d.sampleRate / factor;
    const std::vector<float> x = decimate(mixToMono(d), factor);
    std::vector<double> prefix(x.size() + 1, 0.0);
    for (std::size_t i = 0; i < x.size(); ++i) prefix[i + 1] = prefix[i] + static_cast<double>(x[i]) * x[i];

    const Yin yin(rate, options.minHz, options.maxHz, options.meanThreshold);
    const int hop = std::max(1, static_cast<int>(std::lround(0.010 * rate)));
    const std::size_t frameCount = x.size() / static_cast<std::size_t>(hop) + 1;
    std::vector<std::vector<Candidate>> cands(frameCount);
    for (std::size_t f = 0; f < frameCount; ++f)
        cands[f] = yin.candidates(x, prefix, static_cast<std::int64_t>(f) * hop - yin.w / 2);

    const NoteModel model(std::floor(hzToMidi(options.minHz)), std::ceil(hzToMidi(options.maxHz)), static_cast<double>(hop) / rate);
    const std::vector<int> path = model.decode(cands);

    out.hop = hop * factor;
    out.midi.assign(frameCount, 0.0f);

    // notes: an attack state after silence opens one, silence (or the end) closes it
    std::size_t f = 0;
    while (f < frameCount) {
        if (path[f] % NoteModel::kKinds == 2) { ++f; continue; }
        std::size_t end = f;
        while (end < frameCount && path[end] % NoteModel::kKinds != 2) ++end;
        // the pitch track inside the note: the candidate nearest the state's pitch, within a semitone and a half
        std::vector<double> values(end - f, 0.0);
        std::vector<double> valid;
        for (std::size_t i = f; i < end; ++i) {
            const double target = model.midiOf(path[i] / NoteModel::kKinds);
            const Candidate* best = nullptr;
            for (const Candidate& c : cands[i])
                if (std::abs(c.midi - target) < 1.5 && (!best || std::abs(c.midi - target) < std::abs(best->midi - target))) best = &c;
            if (best) { values[i - f] = best->midi; valid.push_back(best->midi); }
        }
        if (end - f >= 3 && valid.size() >= 3) {
            const double pitch = medianOf(valid);
            // fill the frames without an estimate from their neighbours
            double last = 0.0;
            std::size_t lastIdx = 0;
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (values[i] > 0) {
                    if (last > 0 && i > lastIdx + 1)
                        for (std::size_t j = lastIdx + 1; j < i; ++j)
                            values[j] = last + (values[i] - last) * static_cast<double>(j - lastIdx) / static_cast<double>(i - lastIdx);
                    last = values[i];
                    lastIdx = i;
                }
            }
            double firstV = 0.0;
            for (double v : values) if (v > 0) { firstV = v; break; }
            for (double& v : values) if (v <= 0) v = firstV;
            for (std::size_t i = lastIdx + 1; i < values.size(); ++i) values[i] = last;
            for (std::size_t i = f; i < end; ++i) out.midi[i] = static_cast<float>(values[i - f]);

            FlexNote note;
            const std::int64_t halfHop = out.hop / 2;
            note.start = std::max<std::int64_t>(0, static_cast<std::int64_t>(f) * out.hop - halfHop);
            const std::int64_t stop = std::min<std::int64_t>(out.frames, static_cast<std::int64_t>(end) * out.hop - halfHop);
            note.length = std::max<std::int64_t>(0, stop - note.start);
            note.pitch = pitch;
            if (note.length > 0) out.notes.push_back(note);
        }
        f = end;
    }
    return out;
}

}  // namespace lpc
