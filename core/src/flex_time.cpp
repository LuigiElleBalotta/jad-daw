#include "lpc/flex_time.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
    d.samples.resize(static_cast<std::size_t>(p.n * channels));
    for (std::int64_t i = 0; i < p.n; ++i)
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
        const std::int64_t len = x_.n;
        std::vector<std::vector<float>> out(x_.ch.size(), std::vector<float>(static_cast<std::size_t>(len), 0.0f));
        std::int64_t prev = 0;
        bool have = false;
        for (std::int64_t m = -1;; ++m) {
            const std::int64_t os = m * hop_;
            if (os >= len) break;
            const std::int64_t target = static_cast<std::int64_t>(std::llround(map_.toSource(static_cast<double>(os + hop_)) - hop_));
            std::int64_t start = target;
            if (have && similarity_) start = bestStart(target, prev + hop_);
            for (std::size_t c = 0; c < x_.ch.size(); ++c) {
                const std::vector<float>& in = x_.ch[c];
                for (int i = 0; i < frame_; ++i) {
                    const std::int64_t o = os + i;
                    if (o < 0 || o >= len) continue;
                    out[c][static_cast<std::size_t>(o)] += window_[static_cast<std::size_t>(i)] * at(in, start + i);
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
    std::vector<std::vector<float>> out(x.ch.size(), std::vector<float>(static_cast<std::size_t>(x.n), 0.0f));
    for (std::int64_t o = 0; o < x.n; ++o) {
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
    const std::int64_t len = x.n;
    const int rate = x.rate;
    auto ms = [&](double v) { return static_cast<std::int64_t>(v * 0.001 * rate); };
    std::vector<std::int64_t> cuts{0};
    std::vector<std::int64_t> all = transients;
    for (const FlexAnchor& a : map.points()) all.push_back(a.src);
    std::sort(all.begin(), all.end());
    for (std::int64_t t : all)
        if (t > cuts.back() + ms(10) && t < len - ms(10)) cuts.push_back(t);
    cuts.push_back(len);

    std::vector<std::vector<float>> out(x.ch.size(), std::vector<float>(static_cast<std::size_t>(len), 0.0f));
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
                if (pos < 0 || pos >= len) continue;
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
                    if (pos < 0 || pos >= len) continue;
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

TimeMap::TimeMap(std::int64_t length, std::vector<FlexAnchor> anchors) : length_(length) {
    std::stable_sort(anchors.begin(), anchors.end(), [](const FlexAnchor& a, const FlexAnchor& b) { return a.src < b.src; });
    pts_.push_back({0, 0});
    for (const FlexAnchor& a : anchors) {
        if (a.src <= pts_.back().src || a.src >= length) continue;
        if (a.dst <= pts_.back().dst || a.dst >= length) continue;
        pts_.push_back(a);
    }
    pts_.push_back({length, length});
}

double TimeMap::toSource(double dst) const {
    if (pts_.size() < 2) return dst;
    if (dst <= 0) return dst;
    if (dst >= static_cast<double>(length_)) return static_cast<double>(length_) + (dst - static_cast<double>(length_));
    auto it = std::upper_bound(pts_.begin(), pts_.end(), dst, [](double v, const FlexAnchor& a) { return v < static_cast<double>(a.dst); });
    const FlexAnchor& b = *it;
    const FlexAnchor& a = *(it - 1);
    return a.src + (dst - a.dst) * static_cast<double>(b.src - a.src) / static_cast<double>(b.dst - a.dst);
}

double TimeMap::toDest(double src) const {
    if (pts_.size() < 2) return src;
    if (src <= 0) return src;
    if (src >= static_cast<double>(length_)) return static_cast<double>(length_) + (src - static_cast<double>(length_));
    auto it = std::upper_bound(pts_.begin(), pts_.end(), src, [](double v, const FlexAnchor& a) { return v < static_cast<double>(a.src); });
    const FlexAnchor& b = *it;
    const FlexAnchor& a = *(it - 1);
    return a.dst + (src - a.src) * static_cast<double>(b.dst - a.dst) / static_cast<double>(b.src - a.src);
}

TimeMap TimeMap::withProtectedTransients(const std::vector<std::int64_t>& transients, int sampleRate, double preMs, double postMs) const {
    if (pts_.size() < 2) return *this;
    const std::int64_t pre = std::max<std::int64_t>(1, static_cast<std::int64_t>(preMs * 0.001 * sampleRate));
    const std::int64_t post = std::max<std::int64_t>(1, static_cast<std::int64_t>(postMs * 0.001 * sampleRate));
    std::vector<FlexAnchor> out(pts_.begin() + 1, pts_.end() - 1);
    for (std::size_t i = 0; i + 1 < pts_.size(); ++i) {
        const FlexAnchor a = pts_[i], b = pts_[i + 1];
        const std::int64_t ls = b.src - a.src, ld = b.dst - a.dst;
        if (std::llabs(ls - ld) <= 1) continue;  // not stretched: nothing to protect
        std::vector<FlexAnchor> add;
        std::int64_t lastSrc = a.src, lastDst = a.dst;
        std::int64_t windows = 0;
        for (std::int64_t t : transients) {
            if (t - pre <= lastSrc || t + post >= b.src) continue;
            const std::int64_t td = static_cast<std::int64_t>(std::llround(toDest(static_cast<double>(t))));
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
    return TimeMap(length_, out);
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
    for (std::int64_t k = back; k < frames; ++k) rise[static_cast<std::size_t>(k)] = std::max(0.0, e[static_cast<std::size_t>(k)] - e[static_cast<std::size_t>(k - back)]);
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

WavData renderFlexTime(const WavData& src, const std::vector<FlexAnchor>& anchors, const std::vector<std::int64_t>& transients, const FlexTimeParams& params) {
    if (src.sampleRate <= 0 || src.channels <= 0 || src.frames() == 0) return src;
    TimeMap map(src.frames(), anchors);
    if (map.isIdentity()) return src;
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
        case FlexMode::Monophonic:
        case FlexMode::Polyphonic:
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
