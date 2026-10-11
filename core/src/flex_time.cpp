#include "lpc/flex_time.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <limits>

#include "lpc/fft.h"
#include "lpc/flex_pitch.h"

namespace lpc {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Planar {
    int rate = 0;
    std::int64_t n = 0;
    std::vector<std::vector<float>> ch;
    std::vector<float> mono;
};

Planar split(const WavData& d) {
    Planar p;
    p.rate = d.sampleRate;
    p.n = d.frames();
    const int c = std::max(1, d.channels);
    p.ch.assign(static_cast<std::size_t>(c), std::vector<float>(static_cast<std::size_t>(p.n)));
    p.mono.assign(static_cast<std::size_t>(p.n), 0.0f);
    for (std::int64_t i = 0; i < p.n; ++i) {
        double s = 0.0;
        for (int k = 0; k < c; ++k) {
            const float v = d.samples[static_cast<std::size_t>(i * c + k)];
            p.ch[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)] = v;
            s += v;
        }
        p.mono[static_cast<std::size_t>(i)] = static_cast<float>(s / c);
    }
    return p;
}

WavData join(const Planar& p, const std::vector<std::vector<float>>& out, int channels) {
    WavData d;
    d.sampleRate = p.rate;
    d.channels = channels;
    const std::int64_t n = out.empty() ? 0 : static_cast<std::int64_t>(out[0].size());
    d.samples.resize(static_cast<std::size_t>(n * channels));
    for (std::int64_t i = 0; i < n; ++i)
        for (int k = 0; k < channels; ++k) d.samples[static_cast<std::size_t>(i * channels + k)] = out[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)];
    return d;
}

inline float at(const std::vector<float>& v, std::int64_t i) { return (i >= 0 && i < static_cast<std::int64_t>(v.size())) ? v[static_cast<std::size_t>(i)] : 0.0f; }

// ---------------------------------------------------------------- stretching with a time map: WSOLA, or plain OLA when there is no search

class Stretcher {
public:
    Stretcher(const Planar& x, const TimeMap& map, double frameMs, double toleranceMs, bool similarity) : x_(x), map_(map), similarity_(similarity) {
        frame_ = std::max(64, static_cast<int>(frameMs * 0.001 * x.rate) & ~1);
        hop_ = frame_ / 2;
        tol_ = similarity ? std::max(1, static_cast<int>(toleranceMs * 0.001 * x.rate)) : 0;
        window_.resize(static_cast<std::size_t>(frame_));
        for (int i = 0; i < frame_; ++i) window_[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2 * kPi * i / frame_));
        if (similarity_) {  // a copy of the sound at a quarter of the rate (averaged) for the coarse search
            coarse_ = 4;
            const std::int64_t m = x.n / coarse_;
            small_.resize(static_cast<std::size_t>(m));
            for (std::int64_t i = 0; i < m; ++i) {
                float s = 0;
                for (int k = 0; k < coarse_; ++k) s += x.mono[static_cast<std::size_t>(i * coarse_ + k)];
                small_[static_cast<std::size_t>(i)] = s / coarse_;
            }
        }
    }

    std::vector<std::vector<float>> run() const {
        const std::int64_t len = map_.dstLength();
        std::vector<std::vector<float>> out(x_.ch.size(), std::vector<float>(static_cast<std::size_t>(len), 0.0f));
        std::int64_t prev = 0;
        bool have = false;
        for (std::int64_t m = -1;; ++m) {
            const std::int64_t os = m * hop_;
            if (os >= len) break;
            const std::int64_t target = static_cast<std::int64_t>(std::llround(map_.toSource(static_cast<double>(os + hop_)) - hop_));
            std::int64_t start = target;
            if (have && similarity_) start = bestStart(target, prev + hop_);
            // A frame in a stretched stretch of sound reads only that stretch (mirrored beyond its ends): an attack in the unstretched window
            // next to it must not be copied into the sound around it.
            std::int64_t lo = std::numeric_limits<std::int64_t>::min() / 4, hi = std::numeric_limits<std::int64_t>::max() / 4;
            {
                const std::vector<FlexAnchor>& pts = map_.points();
                const std::int64_t centre = os + hop_;
                for (std::size_t k = 0; k + 1 < pts.size(); ++k)
                    if (centre >= pts[k].dst && centre < pts[k + 1].dst) {
                        if (std::llabs((pts[k + 1].src - pts[k].src) - (pts[k + 1].dst - pts[k].dst)) > 1 && similarity_ && pts.size() > 2) { lo = pts[k].src; hi = pts[k + 1].src; }
                        break;
                    }
            }
            for (std::size_t c = 0; c < x_.ch.size(); ++c) {
                const std::vector<float>& in = x_.ch[c];
                for (int i = 0; i < frame_; ++i) {
                    const std::int64_t o = os + i;
                    if (o < 0 || o >= len) continue;
                    std::int64_t q = start + i;
                    if (q < lo) q = std::min(hi - 1, 2 * lo - q - 1);
                    else if (q >= hi) q = std::max(lo, 2 * hi - q - 1);
                    out[c][static_cast<std::size_t>(o)] += window_[static_cast<std::size_t>(i)] * at(in, q);
                }
            }
            prev = start;
            have = true;
        }
        return out;
    }

private:
    // the start in [target - tol, target + tol] whose first `hop_` samples are most like the natural continuation `nat`
    std::int64_t bestStart(std::int64_t target, std::int64_t nat) const {
        const std::vector<float>& mono = x_.mono;
        // coarse
        const int len = hop_ / coarse_;
        double bestScore = -1e300;
        std::int64_t bestCoarse = target / coarse_;
        const std::int64_t natC = nat / coarse_;
        double refEnergy = 0.0;
        for (int j = 0; j < len; ++j) refEnergy += at(small_, natC + j) * at(small_, natC + j);
        if (refEnergy < 1e-12) return target;  // the continuation is silent: nothing to align with
        for (std::int64_t s = (target - tol_) / coarse_; s <= (target + tol_) / coarse_; ++s) {
            double dot = 0.0, e = 0.0;
            for (int j = 0; j < len; ++j) {
                const double b = at(small_, s + j);
                dot += at(small_, natC + j) * b;
                e += b * b;
            }
            const double score = dot / std::sqrt(e + 1e-9);
            if (score > bestScore) { bestScore = score; bestCoarse = s; }
        }
        // fine, around the coarse winner (and the exact target, so that an unstretched passage stays untouched)
        std::int64_t best = target;
        double bestFine = -1e300;
        auto consider = [&](std::int64_t s) {
            if (s < target - tol_ || s > target + tol_) return;
            double dot = 0.0, e = 0.0;
            for (int j = 0; j < hop_; ++j) {
                const double b = at(mono, s + j);
                dot += at(mono, nat + j) * b;
                e += b * b;
            }
            const double score = dot / std::sqrt(e + 1e-9);
            if (score > bestFine + 1e-12) { bestFine = score; best = s; }
        };
        consider(target);
        for (std::int64_t s = bestCoarse * coarse_ - coarse_; s <= bestCoarse * coarse_ + coarse_; ++s) consider(s);
        return best;
    }

    const Planar& x_;
    const TimeMap& map_;
    bool similarity_ = true;
    int frame_ = 0, hop_ = 0, tol_ = 0, coarse_ = 1;
    std::vector<float> window_;
    std::vector<float> small_;
};

// ---------------------------------------------------------------- speed: resampling along the map (the pitch follows)

std::vector<std::vector<float>> speedRender(const Planar& x, const TimeMap& map) {
    std::vector<std::vector<float>> out(x.ch.size(), std::vector<float>(static_cast<std::size_t>(map.dstLength()), 0.0f));
    for (std::int64_t o = 0; o < map.dstLength(); ++o) {
        const double s = map.toSource(static_cast<double>(o));
        const std::int64_t i = static_cast<std::int64_t>(std::floor(s));
        const double f = s - static_cast<double>(i);
        for (std::size_t c = 0; c < x.ch.size(); ++c) {
            const std::vector<float>& v = x.ch[c];
            const double p0 = at(v, i - 1), p1 = at(v, i), p2 = at(v, i + 1), p3 = at(v, i + 2);
            // Catmull-Rom
            out[c][static_cast<std::size_t>(o)] = static_cast<float>(p1 + 0.5 * f * (p2 - p0 + f * (2 * p0 - 5 * p1 + 4 * p2 - p3 + f * (3 * (p1 - p2) + p3 - p0))));
        }
    }
    return out;
}

// ---------------------------------------------------------------- slicing and rhythmic: slices at their original speed, moved

std::vector<std::vector<float>> sliceRender(const Planar& x, const TimeMap& map, const std::vector<std::int64_t>& transients, const FlexTimeParams& p) {
    const std::int64_t len = x.n;                  // the sound as recorded
    const std::int64_t outLen = map.dstLength();   // the sound as flexed
    const int rate = x.rate;
    auto ms = [&](double v) { return static_cast<std::int64_t>(v * 0.001 * rate); };
    std::vector<std::int64_t> cuts{0};
    std::vector<std::int64_t> all = transients;
    for (const FlexAnchor& a : map.points()) all.push_back(a.src);
    std::sort(all.begin(), all.end());
    for (std::int64_t t : all)
        if (t > cuts.back() + ms(10) && t < len - ms(10)) cuts.push_back(t);
    cuts.push_back(len);

    std::vector<std::vector<float>> out(x.ch.size(), std::vector<float>(static_cast<std::size_t>(outLen), 0.0f));
    const bool rhythmic = p.mode == FlexMode::Rhythmic;
    for (std::size_t k = 0; k + 1 < cuts.size(); ++k) {
        const std::int64_t s0 = cuts[k], s1 = cuts[k + 1];
        const std::int64_t d0 = static_cast<std::int64_t>(std::llround(map.toDest(static_cast<double>(s0))));
        const std::int64_t d1 = static_cast<std::int64_t>(std::llround(map.toDest(static_cast<double>(s1))));
        const std::int64_t room = d1 - d0;  // where the next slice starts
        if (room <= 0) continue;
        const std::int64_t preroll = (k == 0) ? 0 : std::min<std::int64_t>(ms(3), (s0 - cuts[k - 1]) / 2);
        std::int64_t playLen = std::max<std::int64_t>(1, static_cast<std::int64_t>((s1 - s0) * (rhythmic ? 1.0 : std::clamp(p.sliceLength, 0.05, 1.0))));
        const std::int64_t xfade = std::min<std::int64_t>(ms(4), std::max<std::int64_t>(1, playLen / 4));
        bool cut = false;
        if (playLen > room + xfade) { playLen = room + xfade; cut = true; }  // squeezed: the slice is cut where the next begins
        const std::int64_t endFade = cut ? xfade : std::min<std::int64_t>(ms(2), playLen / 2);
        for (std::size_t c = 0; c < x.ch.size(); ++c) {
            const std::vector<float>& in = x.ch[c];
            std::vector<float>& o = out[c];
            for (std::int64_t i = -preroll; i < playLen; ++i) {
                const std::int64_t pos = d0 + i;
                if (pos < 0 || pos >= outLen) continue;
                double g = 1.0;
                if (i < 0) g = static_cast<double>(i + preroll) / std::max<std::int64_t>(1, preroll);   // the lead-in before the attack
                if (i >= playLen - endFade) g *= static_cast<double>(playLen - i) / std::max<std::int64_t>(1, endFade);
                o[static_cast<std::size_t>(pos)] += static_cast<float>(g * at(in, s0 + i));
            }
            // fill the gap left after the slice
            const std::int64_t gapStart = d0 + playLen;
            const std::int64_t gap = d1 - gapStart;
            if (!cut && gap > 0 && p.decay > 0.02) {
                const std::int64_t loopLen = std::clamp<std::int64_t>(ms(rhythmic ? p.loopLengthMs : 20.0), 8, std::max<std::int64_t>(8, playLen));
                const std::int64_t loopEnd = std::max<std::int64_t>(loopLen, playLen - (rhythmic ? ms(p.loopOffsetMs) : 0));
                const std::int64_t loopFrom = s0 + loopEnd - loopLen;
                const double tau = std::max(1.0, static_cast<double>(gap) * (0.1 + 2.0 * p.decay * p.decay));
                const std::int64_t xf = std::max<std::int64_t>(1, std::min<std::int64_t>(ms(2), loopLen / 3));
                for (std::int64_t i = 0; i < gap; ++i) {
                    const std::int64_t pos = gapStart + i;
                    if (pos < 0 || pos >= outLen) continue;
                    const std::int64_t within = i % (loopLen - xf);
                    float v = at(in, loopFrom + within);
                    if (within >= loopLen - 2 * xf && loopLen > 3 * xf) {  // blend the end of the piece into its own beginning
                        const double a = static_cast<double>(within - (loopLen - 2 * xf)) / xf;
                        v = static_cast<float>(v * (1.0 - std::min(1.0, a)) + at(in, loopFrom + (within - (loopLen - 2 * xf))) * std::min(1.0, a));
                    }
                    double g = std::exp(-static_cast<double>(i) / tau) * 0.7;
                    const std::int64_t toEnd = gap - i;
                    if (toEnd < ms(4)) g *= static_cast<double>(toEnd) / std::max<std::int64_t>(1, ms(4));
                    if (i < xfade) g *= static_cast<double>(i) / xfade;
                    o[static_cast<std::size_t>(pos)] += static_cast<float>(g * v);
                }
            }
        }
    }
    return out;
}


// ---------------------------------------------------------------- phase vocoder with identity phase locking (Laroche & Dolson, 1999)

class PhaseVocoder {
public:
    PhaseVocoder(const Planar& x, const TimeMap& map) : x_(x), map_(map) {
        n_ = 2048;
        while (n_ * 2 <= 0.11 * x.rate) n_ *= 2;  // about 90 ms: 4096 at 44.1 and 48 kHz
        hs_ = n_ / 4;
        window_.resize(static_cast<std::size_t>(n_));
        for (int i = 0; i < n_; ++i) window_[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2 * kPi * i / n_);
    }

    std::vector<std::vector<float>> run() const {
        const std::int64_t len = map_.dstLength();
        std::vector<std::vector<float>> out;
        for (const std::vector<float>& ch : x_.ch) out.push_back(runChannel(ch, len));
        return out;
    }

private:
    std::vector<float> runChannel(const std::vector<float>& in, std::int64_t len) const {
        const int half = n_ / 2;
        const Fft fft(n_);
        std::vector<float> out(static_cast<std::size_t>(len), 0.0f);
        std::vector<std::complex<double>> z(static_cast<std::size_t>(n_)), y(static_cast<std::size_t>(n_));
        std::vector<double> mag(static_cast<std::size_t>(half + 1)), ph1(static_cast<std::size_t>(half + 1)), ph0(static_cast<std::size_t>(half + 1));
        std::vector<double> psi(static_cast<std::size_t>(half + 1), 0.0);   // synthesis phases of the previous frame
        std::vector<int> peaks;
        bool first = true;
        std::int64_t prevStart = 0;
        for (std::int64_t m = -3;; ++m) {
            const std::int64_t os = m * hs_;
            if (os >= len) break;
            const std::int64_t start = static_cast<std::int64_t>(std::llround(map_.toSource(static_cast<double>(os + half)) - half));
            const std::int64_t hp = std::max<std::int64_t>(1, std::min<std::int64_t>(hs_, std::max<std::int64_t>(1, n_ / 8)));  // spacing of the two analysis frames
            for (int i = 0; i < n_; ++i) {
                const double w = window_[static_cast<std::size_t>(i)];
                z[static_cast<std::size_t>(i)] = {w * at(in, start + i), w * at(in, start - hp + i)};
            }
            fft.forward(z.data());
            for (int k = 0; k <= half; ++k) {
                const std::complex<double> zk = z[static_cast<std::size_t>(k)], zn = std::conj(z[static_cast<std::size_t>((n_ - k) % n_)]);
                const std::complex<double> x1 = 0.5 * (zk + zn);
                const std::complex<double> x0 = std::complex<double>(0, -0.5) * (zk - zn);
                mag[static_cast<std::size_t>(k)] = std::abs(x1);
                ph1[static_cast<std::size_t>(k)] = std::arg(x1);
                ph0[static_cast<std::size_t>(k)] = std::arg(x0);
            }
            // peaks: larger than the two neighbours on each side; every other bin belongs to the region of the nearest peak
            peaks.clear();
            for (int k = 2; k < half - 1; ++k)
                if (mag[static_cast<std::size_t>(k)] > mag[static_cast<std::size_t>(k - 1)] && mag[static_cast<std::size_t>(k)] > mag[static_cast<std::size_t>(k - 2)] &&
                    mag[static_cast<std::size_t>(k)] >= mag[static_cast<std::size_t>(k + 1)] && mag[static_cast<std::size_t>(k)] >= mag[static_cast<std::size_t>(k + 2)])
                    peaks.push_back(k);
            const bool unstretched = !first && std::llabs((start - prevStart) - hs_) <= 1;
            if (first || unstretched) {
                // no stretch here (or the very first frame): the analysis phases are the right ones
                for (int k = 0; k <= half; ++k) psi[static_cast<std::size_t>(k)] = ph1[static_cast<std::size_t>(k)];
            } else if (!peaks.empty()) {
                std::vector<double> next(psi.size());
                        for (std::size_t pi = 0; pi < peaks.size(); ++pi) {
                    const int pk = peaks[pi];
                    const int lo = pi == 0 ? 0 : (peaks[pi - 1] + pk) / 2 + 1;
                    const int hi = pi + 1 == peaks.size() ? half : (pk + peaks[pi + 1]) / 2;
                    // the frequency of the sinusoid at this peak, from how its phase moved over the spacing of the two frames
                    const double omega = 2 * kPi * pk / n_;
                    double delta = ph1[static_cast<std::size_t>(pk)] - ph0[static_cast<std::size_t>(pk)] - omega * static_cast<double>(hp);
                    delta -= 2 * kPi * std::round(delta / (2 * kPi));
                    const double inst = omega + delta / static_cast<double>(hp);
                    const double peakPhase = psi[static_cast<std::size_t>(pk)] + inst * static_cast<double>(hs_);
                    for (int k = lo; k <= hi; ++k) next[static_cast<std::size_t>(k)] = peakPhase + (ph1[static_cast<std::size_t>(k)] - ph1[static_cast<std::size_t>(pk)]);
                }
                psi.swap(next);
            } else {
                for (int k = 0; k <= half; ++k) psi[static_cast<std::size_t>(k)] = ph1[static_cast<std::size_t>(k)];
            }
            first = false;
            prevStart = start;
            for (int k = 0; k <= half; ++k) {
                const std::complex<double> v = std::polar(mag[static_cast<std::size_t>(k)], psi[static_cast<std::size_t>(k)]);
                y[static_cast<std::size_t>(k)] = v;
                if (k > 0 && k < half) y[static_cast<std::size_t>(n_ - k)] = std::conj(v);
            }
            y[0] = {y[0].real(), 0.0};
            y[static_cast<std::size_t>(half)] = {y[static_cast<std::size_t>(half)].real(), 0.0};
            fft.inverse(y.data());
            for (int i = 0; i < n_; ++i) {
                const std::int64_t o = os + i;
                if (o < 0 || o >= len) continue;
                out[static_cast<std::size_t>(o)] += static_cast<float>(y[static_cast<std::size_t>(i)].real() * window_[static_cast<std::size_t>(i)] * (2.0 / 3.0));  // Hann^2 at 75% overlap adds up to 1.5
            }
        }
        return out;
    }

    const Planar& x_;
    const TimeMap& map_;
    int n_ = 0, hs_ = 0;
    std::vector<double> window_;
};


// The phase vocoder smears attacks over its long frames. Where an attack sits in an unstretched window, the original sound of the attack
// is put back over the result (a short cross-fade at both ends).
void spliceAttacks(const Planar& x, const TimeMap& used, const std::vector<std::int64_t>& transients, std::vector<std::vector<float>>& out) {
    const std::int64_t pre = static_cast<std::int64_t>(0.003 * x.rate), post = static_cast<std::int64_t>(0.025 * x.rate);
    const std::int64_t fadeIn = std::max<std::int64_t>(1, static_cast<std::int64_t>(0.002 * x.rate)), fadeOut = std::max<std::int64_t>(1, static_cast<std::int64_t>(0.006 * x.rate));
    for (std::int64_t t : transients) {
        const std::int64_t a = static_cast<std::int64_t>(std::llround(used.toDest(static_cast<double>(t - pre))));
        const std::int64_t b = static_cast<std::int64_t>(std::llround(used.toDest(static_cast<double>(t + post))));
        if (std::llabs((b - a) - (pre + post)) > 2) continue;  // that window is stretched: there is nothing to put back
        for (std::size_t c = 0; c < x.ch.size(); ++c) {
            for (std::int64_t i = -fadeIn; i < pre + post + fadeOut; ++i) {
                const std::int64_t pos = a + i;
                if (pos < 0 || pos >= used.dstLength()) continue;
                double m = 1.0;
                if (i < 0) m = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(i + fadeIn) / fadeIn);
                else if (i >= pre + post) m = 0.5 + 0.5 * std::cos(kPi * static_cast<double>(i - (pre + post)) / fadeOut);
                const float orig = at(x.ch[c], t - pre + i);
                out[c][static_cast<std::size_t>(pos)] = static_cast<float>(out[c][static_cast<std::size_t>(pos)] * (1.0 - m) + orig * m);
            }
        }
    }
}

}  // namespace

// ---------------------------------------------------------------- names

const char* flexModeName(FlexMode m) {
    switch (m) {
        case FlexMode::Off: return "off";
        case FlexMode::Slicing: return "slicing";
        case FlexMode::Rhythmic: return "rhythmic";
        case FlexMode::Monophonic: return "monophonic";
        case FlexMode::Polyphonic: return "polyphonic";
        case FlexMode::Tempophone: return "tempophone";
        case FlexMode::Speed: return "speed";
        case FlexMode::Automatic: return "automatic";
    }
    return "off";
}

FlexMode flexModeFromName(const char* name) {
    for (FlexMode m : {FlexMode::Slicing, FlexMode::Rhythmic, FlexMode::Monophonic, FlexMode::Polyphonic, FlexMode::Tempophone, FlexMode::Speed, FlexMode::Automatic})
        if (std::strcmp(name, flexModeName(m)) == 0) return m;
    return FlexMode::Off;
}

// ---------------------------------------------------------------- TimeMap

TimeMap::TimeMap(std::int64_t srcLength, std::int64_t dstLength, std::vector<FlexAnchor> anchors) : srcLength_(srcLength), dstLength_(dstLength) {
    std::stable_sort(anchors.begin(), anchors.end(), [](const FlexAnchor& a, const FlexAnchor& b) { return a.src < b.src; });
    pts_.push_back({0, 0});
    for (const FlexAnchor& a : anchors) {
        if (a.src <= pts_.back().src || a.src >= srcLength) continue;
        if (a.dst <= pts_.back().dst || a.dst >= dstLength) continue;
        pts_.push_back(a);
    }
    pts_.push_back({srcLength, dstLength});
}

double TimeMap::toSource(double dst) const {
    if (pts_.size() < 2) return dst;
    if (dst <= 0) return dst;
    if (dst >= static_cast<double>(dstLength_)) return static_cast<double>(srcLength_) + (dst - static_cast<double>(dstLength_));
    auto it = std::upper_bound(pts_.begin(), pts_.end(), dst, [](double v, const FlexAnchor& a) { return v < static_cast<double>(a.dst); });
    const FlexAnchor& b = *it;
    const FlexAnchor& a = *(it - 1);
    return a.src + (dst - a.dst) * static_cast<double>(b.src - a.src) / static_cast<double>(b.dst - a.dst);
}

double TimeMap::toDest(double src) const {
    if (pts_.size() < 2) return src;
    if (src <= 0) return src;
    if (src >= static_cast<double>(srcLength_)) return static_cast<double>(dstLength_) + (src - static_cast<double>(srcLength_));
    auto it = std::upper_bound(pts_.begin(), pts_.end(), src, [](double v, const FlexAnchor& a) { return v < static_cast<double>(a.src); });
    const FlexAnchor& b = *it;
    const FlexAnchor& a = *(it - 1);
    return a.dst + (src - a.src) * static_cast<double>(b.dst - a.dst) / static_cast<double>(b.src - a.src);
}

TimeMap TimeMap::withProtectedTransients(const std::vector<std::int64_t>& transients, int sampleRate, double preMs, double postMs) const {
    if (pts_.size() < 2) return *this;
    const std::int64_t pre = std::max<std::int64_t>(1, static_cast<std::int64_t>(preMs * 0.001 * sampleRate));
    const std::int64_t post = std::max<std::int64_t>(1, static_cast<std::int64_t>(postMs * 0.001 * sampleRate));
    const std::int64_t near = std::max<std::int64_t>(1, static_cast<std::int64_t>(0.002 * sampleRate));
    // 1. a marker that sits on a transient (the usual case: that is what was dragged) gets an unstretched window around it
    std::vector<FlexAnchor> first;
    for (std::size_t i = 1; i + 1 < pts_.size(); ++i) {
        const FlexAnchor a = pts_[i];
        bool onTransient = false;
        for (std::int64_t t : transients)
            if (std::llabs(t - a.src) <= near) { onTransient = true; break; }
        const FlexAnchor& prev = pts_[i - 1];
        const FlexAnchor& next = pts_[i + 1];
        if (onTransient && a.src - pre > prev.src && a.dst - pre > prev.dst && a.src + post < next.src && a.dst + post < next.dst) {
            first.push_back({a.src - pre, a.dst - pre});
            first.push_back(a);
            first.push_back({a.src + post, a.dst + post});
        } else {
            first.push_back(a);
        }
    }
    const TimeMap ext(srcLength_, dstLength_, first);
    // 2. transients inside a stretched stretch of sound get one too, and the stretch is taken up by the sound between them
    std::vector<FlexAnchor> out(ext.pts_.begin() + 1, ext.pts_.end() - 1);
    for (std::size_t i = 0; i + 1 < ext.pts_.size(); ++i) {
        const FlexAnchor a = ext.pts_[i], b = ext.pts_[i + 1];
        const std::int64_t ls = b.src - a.src, ld = b.dst - a.dst;
        if (std::llabs(ls - ld) <= 1) continue;  // not stretched: nothing to protect
        std::vector<FlexAnchor> add;
        std::int64_t lastSrc = a.src, lastDst = a.dst;
        std::int64_t windows = 0;
        for (std::int64_t t : transients) {
            if (t - pre <= lastSrc || t + post >= b.src) continue;
            const std::int64_t td = static_cast<std::int64_t>(std::llround(ext.toDest(static_cast<double>(t))));
            if (td - pre <= lastDst || td + post >= b.dst) continue;
            add.push_back({t - pre, td - pre});
            add.push_back({t + post, td + post});
            lastSrc = t + post;
            lastDst = td + post;
            windows += pre + post;
        }
        // the sound between the windows must keep room on both sides, and take up at most the stretch (a ratio between 1/8 and 8)
        if (add.empty() || windows * 2 > std::min(ls, ld)) continue;
        bool ok = true;
        std::int64_t ps = a.src, pd = a.dst;
        for (std::size_t k = 0; k <= add.size(); ++k) {
            const std::int64_t ns = k < add.size() ? add[k].src : b.src, nd = k < add.size() ? add[k].dst : b.dst;
            const std::int64_t is = ns - ps, id = nd - pd;
            if (is <= 0 || id <= 0 || id > 8 * is || is > 8 * id) { ok = false; break; }
            ps = ns;
            pd = nd;
        }
        if (ok) out.insert(out.end(), add.begin(), add.end());
    }
    return TimeMap(srcLength_, dstLength_, out);
}

// ---------------------------------------------------------------- transients

std::vector<std::int64_t> findTransients(const WavData& d, double sensitivity) {
    std::vector<std::int64_t> result;
    const std::int64_t n = d.frames();
    if (d.sampleRate <= 0 || d.channels <= 0 || n < d.sampleRate / 10) return result;
    const Planar p = split(d);
    const int hop = std::max(1, static_cast<int>(0.002 * p.rate));
    const int win = hop * 3;
    // energy of the signal emphasised towards the highs (an attack is broad-band), in dB, every 2 ms
    const std::int64_t frames = n / hop;
    std::vector<double> e(static_cast<std::size_t>(frames), -100.0);
    for (std::int64_t k = 0; k < frames; ++k) {
        double s = 0.0;
        const std::int64_t from = k * hop - win / 2;
        for (int i = 0; i < win; ++i) {
            const double v = at(p.mono, from + i) - 0.95 * at(p.mono, from + i - 1);
            s += v * v;
        }
        e[static_cast<std::size_t>(k)] = 10.0 * std::log10(s / win + 1e-10);
    }
    double top = -100.0;
    for (double v : e) top = std::max(top, v);
    const double minRise = 12.0 - 8.0 * std::clamp(sensitivity, 0.0, 1.0);  // dB over 6 ms: 12 .. 4
    const int back = 3, minGap = static_cast<int>(0.050 / (hop / static_cast<double>(p.rate)));
    const int around = static_cast<int>(0.040 / (hop / static_cast<double>(p.rate)));
    std::vector<double> rise(static_cast<std::size_t>(frames), 0.0);
    // how far the energy now stands above the loudest of the last 30 ms (a sound that merely beats against itself is not an attack)
    const int lookBack = std::max(back + 1, static_cast<int>(0.030 / (hop / static_cast<double>(p.rate))));
    for (std::int64_t k = back; k < frames; ++k) {
        double recent = -100.0;
        for (std::int64_t j = std::max<std::int64_t>(0, k - lookBack); j <= k - back + 1 && j < k; ++j) recent = std::max(recent, e[static_cast<std::size_t>(j)]);
        rise[static_cast<std::size_t>(k)] = std::max(0.0, e[static_cast<std::size_t>(k)] - recent);
    }
    std::vector<std::pair<std::int64_t, double>> peaks;  // (frame, rise)
    for (std::int64_t k = back; k < frames; ++k) {
        const double r = rise[static_cast<std::size_t>(k)];
        if (r < minRise || e[static_cast<std::size_t>(k)] < top - 55.0) continue;
        bool isMax = true;
        for (std::int64_t j = std::max<std::int64_t>(back, k - around); j <= std::min<std::int64_t>(frames - 1, k + around) && isMax; ++j)
            if (rise[static_cast<std::size_t>(j)] > r || (rise[static_cast<std::size_t>(j)] == r && j < k)) isMax = false;
        if (isMax) peaks.emplace_back(k, r);
    }
    // keep the stronger of two peaks that are too close, then put every attack where its rise began
    std::vector<std::pair<std::int64_t, double>> kept;
    for (const auto& pk : peaks) {
        if (!kept.empty() && pk.first - kept.back().first < minGap) {
            if (pk.second > kept.back().second) kept.back() = pk;
            continue;
        }
        kept.push_back(pk);
    }
    for (const auto& pk : kept) {
        const std::int64_t k = pk.first;
        double base = e[static_cast<std::size_t>(k)];
        for (std::int64_t j = std::max<std::int64_t>(0, k - 8); j <= k; ++j) base = std::min(base, e[static_cast<std::size_t>(j)]);
        const double level = base + 0.2 * (e[static_cast<std::size_t>(k)] - base);
        std::int64_t onset = k;
        for (std::int64_t j = std::max<std::int64_t>(0, k - 8); j <= k; ++j)
            if (e[static_cast<std::size_t>(j)] >= level) { onset = j; break; }
        result.push_back(std::clamp<std::int64_t>(onset * hop, 0, n - 1));
    }
    return result;
}

// ---------------------------------------------------------------- rendering

WavData renderFlexTime(const WavData& src, const std::vector<FlexAnchor>& anchors, const std::vector<std::int64_t>& transients, const FlexTimeParams& params,
                       std::int64_t outFrames) {
    if (src.sampleRate <= 0 || src.channels <= 0 || src.frames() == 0) return src;
    if (outFrames < 0) outFrames = src.frames();
    TimeMap map(src.frames(), outFrames, anchors);
    if (map.isIdentity() && outFrames == src.frames()) return src;
    const Planar x = split(src);
    FlexMode mode = params.mode;
    if (mode == FlexMode::Off) return src;
    if (mode == FlexMode::Automatic) mode = chooseFlexMode(src);
    std::vector<std::vector<float>> out;
    switch (mode) {
        case FlexMode::Slicing:
        case FlexMode::Rhythmic: {
            FlexTimeParams p = params;
            p.mode = mode;
            out = sliceRender(x, map, transients, p);
            break;
        }
        case FlexMode::Speed:
            out = speedRender(x, map);
            break;
        case FlexMode::Tempophone: {
            out = Stretcher(x, map, params.grainMs, 0.0, false).run();
            break;
        }
        case FlexMode::Polyphonic: {
            const TimeMap used = params.preserveTransients ? map.withProtectedTransients(transients, src.sampleRate) : map;
            out = PhaseVocoder(x, used).run();
            if (params.preserveTransients) spliceAttacks(x, used, transients, out);
            break;
        }
        case FlexMode::Monophonic:
        default: {
            const TimeMap used = params.preserveTransients ? map.withProtectedTransients(transients, src.sampleRate) : map;
            out = Stretcher(x, used, 50.0, 25.0, true).run();
            break;
        }
    }
    return join(x, out, src.channels);
}

FlexMode chooseFlexMode(const WavData& d) {
    if (d.sampleRate <= 0 || d.frames() < d.sampleRate / 4) return FlexMode::Polyphonic;
    const double seconds = static_cast<double>(d.frames()) / d.sampleRate;
    const std::size_t hits = findTransients(d, 0.5).size();
    // how much of the sound that is there is pitched notes
    const Planar p = split(d);
    const int block = std::max(1, d.sampleRate / 100);
    std::int64_t active = 0;
    for (std::int64_t i = 0; i + block <= p.n; i += block) {
        double s = 0.0;
        for (int j = 0; j < block; ++j) s += static_cast<double>(p.mono[static_cast<std::size_t>(i + j)]) * p.mono[static_cast<std::size_t>(i + j)];
        if (s / block > 1e-6) ++active;
    }
    const PitchAnalysis a = analysePitch(d);
    std::int64_t noted = 0;
    for (const FlexNote& n : a.notes) noted += n.length;
    const double coverage = active > 0 ? static_cast<double>(noted) / (static_cast<double>(active) * block) : 0.0;
    if (coverage > 0.6) return FlexMode::Monophonic;
    if (static_cast<double>(hits) / seconds > 4.0) return FlexMode::Slicing;
    return FlexMode::Polyphonic;
}

}  // namespace lpc
