#include "lpc/audio/effects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <vector>

#include "lpc/audio/eq_filters.h"
#include "lpc/effect_specs.h"
#include "lpc/match_eq.h"
#include "lpc/ml/neural_stream.h"
#include "lpc/processor_ids.h"

namespace lpc::audio {

namespace {

constexpr double kPi = 3.14159265358979323846;

// A parameter of the ref, kept inside the range of its spec (the default when it is missing).
double param(const ProcessorRef& ref, const char* name) {
    const EffectSpec* spec = findEffectSpec(ref.processorId);
    const EffectParam* p = spec ? spec->find(name) : nullptr;
    if (!p) return 0.0;
    const auto it = ref.params.find(name);
    const double v = it == ref.params.end() ? p->def : it->second;
    return std::clamp(v, p->min, p->max);
}

// The gain-reduction meters of Logic's compressors and limiters (MADSP) read dB from the float's bits: m in [1,2) and the exponent e give
// log2(x) ~ e + m - 0.33985 (m - 1.5)^2 - 0.9150375, times 20 / log2(10) = 6.0206003. Good to about 0.02 dB, no log call per sample.
float fastDb(float x) {
    if (!(x > 1e-20f)) return -400.0f;  // the meter floor
    std::uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    const float e = static_cast<float>(static_cast<int>((bits >> 23) & 0xff) - 127);
    bits = (bits & 0x007fffffu) | 0x3f800000u;
    float m;
    std::memcpy(&m, &bits, sizeof m);
    return (e + m - 0.33985f * (m - 1.5f) * (m - 1.5f) - 0.9150375f) * 6.0206003f;
}

double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
double linToDb(double lin) { return 20.0 * std::log10(std::max(lin, 1e-9)); }
// the coefficient of a one-pole smoother that reaches 63 % in `ms` milliseconds
double smoothing(double ms, double sampleRate) { return std::exp(-1.0 / (std::max(ms, 0.01) * 0.001 * sampleRate)); }
// Logic's compressors and limiters all use one time-constant formula (MADSP, decompiled): exp(-2300 / (ms * sampleRate)), i.e. the envelope
// has fallen to a tenth (-20 dB) after `ms`. A time of 0 is an instant response.
double settling(double ms, double sampleRate) { return ms <= 0.0 ? 0.0 : std::exp(-2302.585 / (ms * sampleRate)); }

// One biquad section per channel pair, direct form II transposed
struct Section {
    BiquadCoeffs c;
    double z[4] = {0, 0, 0, 0};
    double l(double x) {
        const double y = c.b0 * x + z[0];
        z[0] = c.b1 * x - c.a1 * y + z[1];
        z[1] = c.b2 * x - c.a2 * y;
        return y;
    }
    double r(double x) {
        const double y = c.b0 * x + z[2];
        z[2] = c.b1 * x - c.a1 * y + z[3];
        z[3] = c.b2 * x - c.a2 * y;
        return y;
    }
};

// ---------------------------------------------------------------- Channel EQ

struct EqBandDef {
    const char* key;
    int kind;  // 0 low cut, 1 low shelf, 2 peak, 3 high shelf, 4 high cut
};
constexpr std::array<EqBandDef, 8> kEqBands{{{"lc", 0}, {"ls", 1}, {"p1", 2}, {"p2", 2}, {"p3", 2}, {"p4", 2}, {"hs", 3}, {"hc", 4}}};

// The sections of every band that is on, in the order low cut ... high cut
std::vector<BiquadCoeffs> eqSections(const ProcessorRef& ref, double sr) {
    std::vector<BiquadCoeffs> out;
    for (const EqBandDef& b : kEqBands) {
        const std::string k = b.key;
        if (param(ref, (k + "On").c_str()) < 0.5) continue;
        const double f = param(ref, (k + "Freq").c_str()), q = param(ref, (k + "Q").c_str());
        switch (b.kind) {
            case 0:
            case 4: {
                const int order = static_cast<int>(std::lround(param(ref, (k + "Slope").c_str())));
                for (const BiquadCoeffs& c : cutFilter(sr, f, order, q, b.kind == 0)) out.push_back(c);
                break;
            }
            case 1: out.push_back(lowShelf(sr, f, param(ref, (k + "Gain").c_str()), q)); break;
            case 3: out.push_back(highShelf(sr, f, param(ref, (k + "Gain").c_str()), q)); break;
            default: out.push_back(peaking(sr, f, param(ref, (k + "Gain").c_str()), q)); break;
        }
    }
    return out;
}

// HQ: the EQ runs at twice the sample rate, so that its filters are not squeezed towards the top of the band by the bilinear transform. The signal goes up and
// down through a 31-tap Blackman-windowed sinc low pass (cut at the original Nyquist), which costs 15 samples of latency in all.
struct HalfBandPath {
    static constexpr int kTaps = 31;
    static const std::array<double, kTaps>& taps() {
        static const std::array<double, kTaps> h = [] {
            std::array<double, kTaps> t{};
            double sum = 0.0;
            for (int n = 0; n < kTaps; ++n) {
                const double m = n - (kTaps - 1) / 2.0;
                const double sinc = m == 0.0 ? 0.5 : std::sin(kPi * 0.5 * m) / (kPi * m);  // cut at a quarter of the doubled rate
                const double w = 0.42 - 0.5 * std::cos(2 * kPi * n / (kTaps - 1)) + 0.08 * std::cos(4 * kPi * n / (kTaps - 1));
                t[static_cast<std::size_t>(n)] = sinc * w;
                sum += t[static_cast<std::size_t>(n)];
            }
            for (double& v : t) v /= sum;  // unity at DC
            return t;
        }();
        return h;
    }
    std::array<double, kTaps> up{}, down{};  // the last samples of the stuffed signal and of the filtered one
    void push(std::array<double, kTaps>& h, double x) { for (int i = kTaps - 1; i > 0; --i) h[static_cast<std::size_t>(i)] = h[static_cast<std::size_t>(i - 1)]; h[0] = x; }
    static double fir(const std::array<double, kTaps>& h) { double y = 0.0; const auto& t = taps(); for (int i = 0; i < kTaps; ++i) y += t[static_cast<std::size_t>(i)] * h[static_cast<std::size_t>(i)]; return y; }
};

class EqProcessor final : public IProcessor {
public:
    EqProcessor(const ProcessorRef& ref, double sr)
        : out_(static_cast<float>(dbToLin(param(ref, "masterGain")))), mode_(static_cast<int>(std::lround(param(ref, "processing")))), hq_(param(ref, "hq") >= 0.5) {
        for (const BiquadCoeffs& c : eqSections(ref, hq_ ? sr * 2.0 : sr)) sections_.push_back(Section{c});
    }
    int latencySamples() const override { return hq_ ? HalfBandPath::kTaps / 2 : 0; }
    // one sample of one path through the sections at twice the rate (sections `a` is the l state, `!a` the r state)
    double hqSample(HalfBandPath& p, bool useL, double x) noexcept {
        p.push(p.up, 2.0 * x);
        double y0 = HalfBandPath::fir(p.up);
        p.push(p.up, 0.0);
        double y1 = HalfBandPath::fir(p.up);
        for (Section& s : sections_) {
            y0 = useL ? s.l(y0) : s.r(y0);
            y1 = useL ? s.l(y1) : s.r(y1);
        }
        p.push(p.down, y0);
        const double out = HalfBandPath::fir(p.down);  // the even phase: the samples the input had
        p.push(p.down, y1);
        return out;
    }
    void process(float* l, float* r, int frames) noexcept override {
        for (int i = 0; i < frames; ++i) {
            // Processing: the EQ (and the master gain) works on one channel, or on the mid or the side of the pair; what it leaves goes through untouched.
            // The filters are linear, so the two paths of Section (l and r state) serve the two signals being filtered: a (left or mid) and b (right or side).
            double a = l[i], b = r[i];
            if (mode_ >= 3) { const double m = 0.5 * (a + b), s = 0.5 * (a - b); a = m; b = s; }
            const bool doA = mode_ == 0 || mode_ == 1 || mode_ == 3, doB = mode_ == 0 || mode_ == 2 || mode_ == 4;
            double ya = a, yb = b;
            if (hq_) {
                // what the EQ leaves alone is delayed like the rest, so that the channels stay aligned
                ya = doA ? hqSample(pathA_, true, a) : hqDelay(delayA_, a);
                yb = doB ? hqSample(pathB_, false, b) : hqDelay(delayB_, b);
            } else {
                for (Section& s : sections_) {
                    if (doA) ya = s.l(ya);
                    if (doB) yb = s.r(yb);
                }
            }
            if (doA) ya *= out_;
            if (doB) yb *= out_;
            if (mode_ >= 3) { l[i] = static_cast<float>(ya + yb); r[i] = static_cast<float>(ya - yb); }
            else { l[i] = static_cast<float>(ya); r[i] = static_cast<float>(yb); }
        }
    }
    nlohmann::json describe() const override { return {{"id", kProcEq}}; }

private:
    static double hqDelay(std::array<double, HalfBandPath::kTaps / 2>& line, double x) noexcept {
        const double y = line.back();
        for (std::size_t i = line.size() - 1; i > 0; --i) line[i] = line[i - 1];
        line[0] = x;
        return y;
    }
    std::vector<Section> sections_;
    float out_;
    int mode_;  // 0 stereo, 1 left, 2 right, 3 mid, 4 side
    bool hq_;
    HalfBandPath pathA_, pathB_;
    std::array<double, HalfBandPath::kTaps / 2> delayA_{}, delayB_{};
};

// ---------------------------------------------------------------- Match EQ (a bank of third-octave peaking filters)

class MatchEqProcessor final : public IProcessor {
public:
    MatchEqProcessor(const ProcessorRef& ref, double sr) {
        const double amount = param(ref, "amount") / 100.0;
        for (int i = 0; i < kMatchEqBands; ++i) {
            const double g = param(ref, ("b" + std::to_string(i)).c_str()) * amount;
            if (std::abs(g) < 0.05) continue;
            sections_.push_back(Section{peaking(sr, matchEqBandFreq(i), g, 4.318)});  // Q of a third-octave band
        }
    }
    void process(float* l, float* r, int frames) noexcept override {
        for (int i = 0; i < frames; ++i) {
            double xl = l[i], xr = r[i];
            for (Section& s : sections_) {
                xl = s.l(xl);
                xr = s.r(xr);
            }
            l[i] = static_cast<float>(xl);
            r[i] = static_cast<float>(xr);
        }
    }
    nlohmann::json describe() const override { return {{"id", kProcMatchEq}}; }

private:
    std::vector<Section> sections_;
};

// ---------------------------------------------------------------- Compressor

// The circuit types of Logic's Compressor (values as stored in its presets) differ in more than numbers (MADSP, docs/logic-reference/mixer.md):
// SSL (Studio VCA) has two release time constants, Opto one that grows with the depth, DBX a fixed soft knee and RMS, FET a much faster attack,
// Platinum is the plain digital one. The curves themselves are tables in Logic and were not extracted; these are the same behaviours made
// with a conventional gain computer.
struct CircuitTraits {
    double attackScale = 1.0;
    double minKneeDb = 0.0;
    bool rms = false;           // forces RMS detection
    bool dualRelease = false;   // two release followers (fast and slow), like the SSL bus compressor
    bool optoRelease = false;   // the release slows down with the depth of the reduction
    double saturation = 0.0;    // soft saturation of the output, 0 = none
};

CircuitTraits circuitTraits(int circuit) {
    CircuitTraits t;
    switch (circuit) {
        case 1: case 2: t.dualRelease = true; t.saturation = 0.15; t.minKneeDb = 6; break;  // Class A R / U
        case 3: t.attackScale = 2.0; t.saturation = 0.1; break;                              // Vintage VCA
        case 4: t.dualRelease = true; break;                                                 // Studio VCA
        case 5: t.attackScale = 0.25; t.saturation = 0.2; break;                             // Studio FET
        case 6: t.optoRelease = true; t.minKneeDb = 12; t.rms = true; t.attackScale = 2.0; break;  // Vintage Opto
        case 7: t.attackScale = 0.3; t.saturation = 0.3; break;                              // Vintage FET
        case 8: t.rms = true; t.minKneeDb = 10; break;                                       // Classic VCA (dbx)
        default: break;                                                                      // 0 Platinum Digital
    }
    return t;
}

class CompressorProcessor final : public IProcessor {
public:
    CompressorProcessor(const ProcessorRef& ref, double sr)
        : traits_(circuitTraits(static_cast<int>(std::lround(param(ref, "circuit"))))), threshold_(param(ref, "threshold")), ratio_(param(ref, "ratio")),
          mix_(param(ref, "mix") / 100.0), limiterOn_(param(ref, "limiter") >= 0.5), limiterThreshold_(dbToLin(param(ref, "limiterThreshold"))),
          distortion_(static_cast<int>(std::lround(param(ref, "outputDistortion")))), sr_(sr) {
        kneeDb_ = std::max(param(ref, "knee") * 20.0, traits_.minKneeDb);
        rms_ = param(ref, "peakRms") >= 0.5 || traits_.rms;
        const double release = param(ref, "release");
        attack_ = settling(param(ref, "attack") * traits_.attackScale, sr);
        release_ = settling(release, sr);
        releaseFast_ = settling(release * 0.5, sr);  // the two constants of the dual release: 1 : 3 around the release time
        releaseSlow_ = settling(release * 3.0, sr);
        releaseMs_ = release;
        dual_ = traits_.dualRelease || param(ref, "autoRelease") >= 0.5;
        rmsCoef_ = std::exp(-1.0 / (0.01 * sr));
        double makeup = param(ref, "gain");
        if (param(ref, "autoGain") >= 0.5) {
            // Auto Gain gives back what the reduction takes off a signal at the reference level: 0 dB (1) or -12 dB (2)
            const double reference = param(ref, "autoGain") >= 1.5 ? -12.0 : 0.0;
            makeup += (1.0 - 1.0 / ratio_) * std::max(0.0, reference - threshold_);
        }
        makeup_ = dbToLin(std::min(makeup, 40.0));
        inputGain_ = dbToLin(param(ref, "inputGain"));
        outputGain_ = dbToLin(param(ref, "outputGain"));
        limiterRelease_ = settling(50.0, sr);
        scState_ = static_cast<int>(std::lround(param(ref, "scState")));
        if (scState_ > 0) {  // the detector listens through the side chain filter
            const double f = param(ref, "scFreq"), q = param(ref, "scQ"), g = param(ref, "scGain");
            switch (static_cast<int>(std::lround(param(ref, "scMode")))) {
                case 0: key_.c = secondOrderPass(sr, f, q, false); break;
                case 1: {
                    const double w = 2 * 3.14159265358979323846 * std::clamp(f, 10.0, 0.45 * sr) / sr, alpha = std::sin(w) / (2.0 * std::max(q, 0.05)), a0 = 1 + alpha;
                    key_.c = {alpha / a0, 0.0, -alpha / a0, -2 * std::cos(w) / a0, (1 - alpha) / a0};
                    break;
                }
                case 3: key_.c = peaking(sr, f, g, q); break;
                case 4: key_.c = highShelf(sr, f, g, q); break;
                default: key_.c = secondOrderPass(sr, f, q, true); break;
            }
        }
    }
    void setSideChain(const float* l, const float* r) noexcept override { sideL_ = l; sideR_ = r; }
    void process(float* l, float* r, int frames) noexcept override {
        for (int i = 0; i < frames; ++i) {
            const double inL = l[i] * inputGain_, inR = r[i] * inputGain_;
            double keyL = sideL_ ? sideL_[i] : inL, keyR = sideR_ ? sideR_[i] : inR;  // the detector hears the side chain when there is one
            if (scState_ > 0) {
                keyL = key_.l(inL);
                keyR = key_.r(inR);
                if (scState_ == 1) {  // Listen: the filtered side chain signal is what comes out
                    l[i] = static_cast<float>(keyL);
                    r[i] = static_cast<float>(keyR);
                    continue;
                }
            }
            double det;
            if (rms_) {
                meanSquare_ = rmsCoef_ * meanSquare_ + (1.0 - rmsCoef_) * 0.5 * (keyL * keyL + keyR * keyR);
                det = std::sqrt(meanSquare_);
            } else {
                det = std::max(std::abs(keyL), std::abs(keyR));
            }
            const double over = linToDb(det) - threshold_;
            const double slope = 1.0 - 1.0 / ratio_;
            double target;  // the reduction asked for, in dB (0 or more)
            if (2 * over < -kneeDb_) target = 0;
            else if (kneeDb_ > 0 && 2 * std::abs(over) <= kneeDb_) target = slope * (over + kneeDb_ / 2) * (over + kneeDb_ / 2) / (2 * kneeDb_);
            else target = slope * over;
            if (dual_) {
                follow(fast_, target, releaseFast_);
                follow(slow_, target, releaseSlow_);
                reduction_ = 0.5 * (fast_ + slow_);
            } else if (traits_.optoRelease) {
                // the cell lets go more slowly the harder it was driven
                const double coef = settling(releaseMs_ * (1.0 + reduction_ / 6.0), sr_);
                follow(reduction_, target, coef);
            } else {
                follow(reduction_, target, release_);
            }
            const double gain = dbToLin(-reduction_) * makeup_;
            double wl = inL * gain, wr = inR * gain;
            if (limiterOn_) {
                const double peak = std::max(std::abs(wl), std::abs(wr));
                const double need = peak > limiterThreshold_ ? limiterThreshold_ / peak : 1.0;
                limiterGain_ = need < limiterGain_ ? need : limiterRelease_ * limiterGain_ + (1.0 - limiterRelease_) * 1.0;
                limiterGain_ = std::min(limiterGain_, need);
                wl *= limiterGain_;
                wr *= limiterGain_;
            }
            wl = shape(wl);
            wr = shape(wr);
            l[i] = static_cast<float>((inL * (1.0 - mix_) + wl * mix_) * outputGain_);
            r[i] = static_cast<float>((inR * (1.0 - mix_) + wr * mix_) * outputGain_);
        }
        blockReduction_ = static_cast<float>(reduction_);
    }
    float reductionDb() const noexcept override { return std::max(0.0f, blockReduction_); }
    nlohmann::json describe() const override { return {{"id", kProcCompressor}, {"reductionDb", reduction_}}; }

private:
    // deeper than now: the attack; shallower: the release (`coef` = 0 is instant)
    void follow(double& state, double target, double releaseCoef) const {
        const double coef = target > state ? attack_ : releaseCoef;
        state = coef * state + (1.0 - coef) * target;
    }
    double shape(double x) const {
        if (traits_.saturation > 0.0) x = x + traits_.saturation * (std::tanh(x) - x);  // a little rounding of the peaks
        switch (distortion_) {
            case 1: return x / std::sqrt(1.0 + x * x);                                      // soft
            case 2: return x / std::pow(1.0 + std::pow(std::abs(x), 6.0), 1.0 / 6.0);      // hard
            case 3: return std::clamp(x, -1.0, 1.0);                                        // clip
            default: return x;
        }
    }
    CircuitTraits traits_;
    double threshold_, ratio_, mix_;
    bool limiterOn_;
    double limiterThreshold_;
    int distortion_;
    double sr_;
    double inputGain_ = 1, outputGain_ = 1;
    int scState_ = 0;
    const float* sideL_ = nullptr;
    const float* sideR_ = nullptr;
    Section key_;
    double kneeDb_ = 0, attack_ = 0, release_ = 0, releaseFast_ = 0, releaseSlow_ = 0, releaseMs_ = 100, rmsCoef_ = 0, makeup_ = 1, limiterRelease_ = 0;
    bool rms_ = false, dual_ = false;
    double meanSquare_ = 0.0, reduction_ = 0.0, fast_ = 0.0, slow_ = 0.0, limiterGain_ = 1.0;
    float blockReduction_ = 0.0f;
};

// ---------------------------------------------------------------- Limiter

class LimiterProcessor final : public IProcessor {
public:
    // `truePeak`: the level is also looked for between the samples (a cubic through the neighbours, four times oversampled), so that what
    // comes out stays under the ceiling as a reconstructed signal too, not only as samples
    LimiterProcessor(const ProcessorRef& ref, double sr, bool truePeak = false)
        : truePeak_((truePeak || param(ref, "truePeak") >= 0.5) && !(ref.params.count("mode") && param(ref, "mode") < 0.5)),  // Legacy mode has no true peak detection ...
          softKnee_(param(ref, "softKnee") >= 0.5 && !(ref.params.count("mode") && param(ref, "mode") >= 0.5)),                // ... and Precision mode no soft knee (hard limiting)
          gain_(static_cast<float>(dbToLin(param(ref, "gain")))),
          ceiling_(static_cast<float>(dbToLin(param(ref, "ceiling")))), look_(std::max(1, static_cast<int>(std::lround(param(ref, "lookahead") * 0.001 * sr)))),
          release_(smoothing(param(ref, "release"), sr)), attack_(smoothing(std::min(0.25, param(ref, "lookahead") * 0.25), sr)),
          needs_(static_cast<std::size_t>(look_ + 1), 1.0f), delayL_(static_cast<std::size_t>(look_), 0.0f), delayR_(static_cast<std::size_t>(look_), 0.0f) {}
    int latencySamples() const override { return look_; }
    void process(float* l, float* r, int frames) noexcept override {
        for (int i = 0; i < frames; ++i) {
            const float inL = l[i] * gain_, inR = r[i] * gain_;
            float peak = std::max(std::abs(inL), std::abs(inR));
            if (truePeak_) {
                for (float t : {0.25f, 0.5f, 0.75f}) {  // between the two samples before this one, with this one as the neighbour after them
                    peak = std::max(peak, std::abs(cubicPoint(h_[0][2], h_[0][1], h_[0][0], inL, t)));
                    peak = std::max(peak, std::abs(cubicPoint(h_[1][2], h_[1][1], h_[1][0], inR, t)));
                }
                h_[0][2] = h_[0][1]; h_[0][1] = h_[0][0]; h_[0][0] = inL;
                h_[1][2] = h_[1][1]; h_[1][1] = h_[1][0]; h_[1][0] = inR;
            }
            float need = peak > ceiling_ ? ceiling_ / peak : 1.0f;
            if (softKnee_ && need >= 1.0f && peak > 0.0f) {  // the reduction starts 1.5 dB before the ceiling and joins the hard curve smoothly
                const double over = linToDb(peak) - linToDb(ceiling_);
                if (over > -1.5) need = static_cast<float>(dbToLin(-(over + 1.5) * (over + 1.5) / 6.0));
            }
            needs_[pos_ % needs_.size()] = need;
            float target = 1.0f;  // the strongest reduction asked for in the last lookahead: it is in place before the loud sample leaves
            for (const float n : needs_) target = std::min(target, n);
            const double coef = target < gain_state_ ? attack_ : release_;
            gain_state_ = static_cast<float>(coef * gain_state_ + (1 - coef) * target);
            const std::size_t d = pos_ % delayL_.size();
            const float outL = delayL_[d] * gain_state_, outR = delayR_[d] * gain_state_;
            delayL_[d] = inL;
            delayR_[d] = inR;
            ++pos_;
            l[i] = std::clamp(outL, -ceiling_, ceiling_);  // the last guard: nothing leaves above the ceiling
            r[i] = std::clamp(outR, -ceiling_, ceiling_);
        }
    }
    float reductionDb() const noexcept override { return std::max(0.0f, -fastDb(gain_state_)); }
    nlohmann::json describe() const override { return {{"id", kProcLimiter}}; }

private:
    // Catmull-Rom between b and c at t (0..1); a before b, d after c
    static float cubicPoint(float a, float b, float c, float d, float t) {
        return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
    }
    bool truePeak_, softKnee_;
    float h_[2][3] = {{0, 0, 0}, {0, 0, 0}};  // the last three input samples of each channel
    float gain_, ceiling_;
    int look_;
    double release_, attack_;
    std::vector<float> needs_, delayL_, delayR_;
    std::size_t pos_ = 0;
    float gain_state_ = 1.0f;
};

// The Adaptive Limiter: a true-peak limiter with a long lookahead; the mode sets how quickly it recovers (low, average, high)
ProcessorRef adaptiveLimiterRef(const ProcessorRef& ref) {
    ProcessorRef r;
    r.processorId = kProcLimiter;
    static const double release[3] = {200.0, 100.0, 40.0};
    const int mode = std::clamp(static_cast<int>(std::lround(param(ref, "mode"))), 0, 2);
    r.params = {{"gain", std::clamp(param(ref, "gain") + param(ref, "inputScale"), 0.0, 24.0)},
                {"lookahead", std::min(param(ref, "lookahead"), 10.0)}, {"softKnee", 1.0}, {"release", release[mode]}, {"ceiling", param(ref, "ceiling")}, {"truePeak", 1.0}};
    return r;
}

class AdaptiveLimiterProcessor final : public IProcessor {
public:
    AdaptiveLimiterProcessor(const ProcessorRef& ref, double sr) : limiter_(adaptiveLimiterRef(ref), sr, true), removeDc_(param(ref, "removeDc") >= 0.5), dc_(1.0 - std::exp(-2.0 * kPi * 5.0 / sr)) {}
    int latencySamples() const override { return limiter_.latencySamples(); }
    void process(float* l, float* r, int frames) noexcept override {
        if (removeDc_) {
            for (int i = 0; i < frames; ++i) {
                meanL_ += dc_ * (l[i] - meanL_);
                meanR_ += dc_ * (r[i] - meanR_);
                l[i] = static_cast<float>(l[i] - meanL_);
                r[i] = static_cast<float>(r[i] - meanR_);
            }
        }
        limiter_.process(l, r, frames);
    }
    nlohmann::json describe() const override { return {{"id", kProcAdaptiveLimiter}}; }

private:
    LimiterProcessor limiter_;
    bool removeDc_;
    double dc_, meanL_ = 0.0, meanR_ = 0.0;
};

// ---------------------------------------------------------------- Noise gate

class GateProcessor final : public IProcessor {
public:
    GateProcessor(const ProcessorRef& ref, double sr)
        : threshold_(param(ref, "threshold")), closeThreshold_(param(ref, "threshold") + param(ref, "hysteresis")), floor_(dbToLin(param(ref, "range"))),
          hold_(static_cast<int>(param(ref, "hold") * 0.001 * sr)), attack_(smoothing(param(ref, "attack"), sr)), release_(smoothing(param(ref, "release"), sr)),
          decay_(smoothing(5.0, sr)), look_(static_cast<int>(std::lround(param(ref, "lookahead") * 0.001 * sr))),
          keyFilter_(param(ref, "keyFilter") >= 0.5), ducker_(param(ref, "ducker") >= 0.5), delayL_(static_cast<std::size_t>(std::max(1, look_)), 0.0f), delayR_(static_cast<std::size_t>(std::max(1, look_)), 0.0f) {
        monitor_ = keyFilter_ && param(ref, "keyMonitor") >= 0.5;
        if (keyFilter_) {
            // Characteristics: the band pass uses both cutoffs, the low pass only the High Cutoff, the high pass only the Low Cutoff
            const int mode = static_cast<int>(std::lround(param(ref, "keyMode")));
            key_[0].c = secondOrderPass(sr, mode == 1 ? 20.0 : param(ref, "keyLowCut"), 0.7071, true);
            key_[1].c = secondOrderPass(sr, mode == 2 ? 20000.0 : param(ref, "keyHighCut"), 0.7071, false);
        }
    }
    int latencySamples() const override { return look_; }
    void setSideChain(const float* l, const float* r) noexcept override { sideL_ = l; sideR_ = r; }
    void process(float* l, float* r, int frames) noexcept override {
        for (int i = 0; i < frames; ++i) {
            double kl = sideL_ ? sideL_[i] : l[i], kr = sideR_ ? sideR_[i] : r[i];  // the detector hears the side chain when there is one
            if (keyFilter_) {  // the detector listens through the side-chain filter
                for (Section& s : key_) {
                    kl = s.l(kl);
                    kr = s.r(kr);
                }
            }
            const double x = std::max(std::abs(kl), std::abs(kr));
            env_ = std::max(x, env_ * decay_);
            const double db = linToDb(env_);
            if (db > threshold_) { open_ = true; held_ = hold_; }
            else if (open_ && db < closeThreshold_) { if (held_ > 0) --held_; else open_ = false; }  // the hysteresis, then the hold time
            // Gate: closed (down to the Reduction) while the signal is below the threshold. Ducker: the other way round, down while it is above
            const double target = (open_ != ducker_) ? 1.0 : floor_;
            const double coef = target > gain_ ? attack_ : release_;
            gain_ = coef * gain_ + (1 - coef) * target;
            float outL = l[i], outR = r[i];
            if (look_ > 0) {  // the audio waits as long as the detector looks ahead
                const std::size_t d = pos_ % delayL_.size();
                outL = delayL_[d];
                outR = delayR_[d];
                delayL_[d] = l[i];
                delayR_[d] = r[i];
                ++pos_;
            }
            if (monitor_) {  // Monitor: what the detector hears
                l[i] = static_cast<float>(kl);
                r[i] = static_cast<float>(kr);
                continue;
            }
            l[i] = static_cast<float>(outL * gain_);
            r[i] = static_cast<float>(outR * gain_);
        }
    }
    float reductionDb() const noexcept override { return gain_ < 1.0 ? static_cast<float>(-linToDb(std::max(gain_, 1e-6))) : 0.0f; }
    nlohmann::json describe() const override { return {{"id", kProcGate}, {"open", open_}}; }

private:
    double threshold_, closeThreshold_, floor_;
    int hold_;
    double attack_, release_, decay_;
    int look_;
    bool keyFilter_;
    bool monitor_ = false;
    bool ducker_;
    const float* sideL_ = nullptr;
    const float* sideR_ = nullptr;
    Section key_[2];
    std::vector<float> delayL_, delayR_;
    std::size_t pos_ = 0;
    double env_ = 0.0, gain_ = 1.0;
    bool open_ = false;
    int held_ = 0;
};

// ---------------------------------------------------------------- Delay

class DelayProcessor final : public IProcessor {
public:
    DelayProcessor(const ProcessorRef& ref, double sr)
        : delay_(std::max(1, static_cast<int>(param(ref, "time") * 0.001 * sr))), feedback_(static_cast<float>(param(ref, "feedback") / 100.0)),
          mix_(static_cast<float>(param(ref, "mix") / 100.0)), pingPong_(param(ref, "pingPong") >= 0.5),
          l_(static_cast<std::size_t>(delay_), 0.0f), r_(static_cast<std::size_t>(delay_), 0.0f) {}
    void process(float* l, float* r, int frames) noexcept override {
        for (int i = 0; i < frames; ++i) {
            const std::size_t p = pos_ % l_.size();
            const float dl = l_[p], dr = r_[p];  // what was written one delay ago
            l_[p] = l[i] + feedback_ * (pingPong_ ? dr : dl);
            r_[p] = r[i] + feedback_ * (pingPong_ ? dl : dr);
            ++pos_;
            l[i] = l[i] * (1 - mix_) + dl * mix_;
            r[i] = r[i] * (1 - mix_) + dr * mix_;
        }
    }
    nlohmann::json describe() const override { return {{"id", kProcDelay}, {"delay", delay_}}; }

private:
    int delay_;
    float feedback_, mix_;
    bool pingPong_;
    std::vector<float> l_, r_;
    std::size_t pos_ = 0;
};

// ---------------------------------------------------------------- Reverb (the Freeverb layout: parallel combs, series all-passes)

class Comb {
public:
    explicit Comb(int size) : buf_(static_cast<std::size_t>(std::max(size, 1)), 0.0f) {}
    float process(float in, float feedback, float damp) {
        const float out = buf_[pos_];
        store_ = out * (1 - damp) + store_ * damp;
        buf_[pos_] = in + store_ * feedback;
        if (++pos_ >= buf_.size()) pos_ = 0;
        return out;
    }

private:
    std::vector<float> buf_;
    std::size_t pos_ = 0;
    float store_ = 0.0f;
};

class AllPass {
public:
    explicit AllPass(int size) : buf_(static_cast<std::size_t>(std::max(size, 1)), 0.0f) {}
    float process(float in) {
        const float b = buf_[pos_];
        const float out = -in + b;
        buf_[pos_] = in + b * 0.5f;
        if (++pos_ >= buf_.size()) pos_ = 0;
        return out;
    }

private:
    std::vector<float> buf_;
    std::size_t pos_ = 0;
};

class ReverbProcessor final : public IProcessor {
public:
    ReverbProcessor(const ProcessorRef& ref, double sr) {
        static const int kComb[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int kAll[4] = {556, 441, 341, 225};
        const double scale = sr / 44100.0;
        for (int i = 0; i < 8; ++i) {
            combL_.emplace_back(static_cast<int>(kComb[i] * scale));
            combR_.emplace_back(static_cast<int>((kComb[i] + 23) * scale));  // the right side is a little longer: the stereo spread
        }
        for (int i = 0; i < 4; ++i) {
            allL_.emplace_back(static_cast<int>(kAll[i] * scale));
            allR_.emplace_back(static_cast<int>((kAll[i] + 23) * scale));
        }
        feedback_ = static_cast<float>(0.7 + 0.28 * param(ref, "size") / 100.0);
        damp_ = static_cast<float>(0.4 * param(ref, "damping") / 100.0);
        const double width = param(ref, "width") / 100.0, wet = param(ref, "mix") / 100.0 * 3.0;
        wet1_ = static_cast<float>(wet * (width / 2 + 0.5));
        wet2_ = static_cast<float>(wet * (1 - width) / 2);
        dry_ = static_cast<float>(1.0 - param(ref, "mix") / 100.0);
        pre_.assign(static_cast<std::size_t>(std::max(1, static_cast<int>(param(ref, "predelay") * 0.001 * sr))), 0.0f);
        preOn_ = param(ref, "predelay") > 0.0;
    }
    void process(float* l, float* r, int frames) noexcept override {
        for (int i = 0; i < frames; ++i) {
            float in = (l[i] + r[i]) * 0.015f;
            if (preOn_) { const float d = pre_[prePos_]; pre_[prePos_] = in; if (++prePos_ >= pre_.size()) prePos_ = 0; in = d; }
            float outL = 0, outR = 0;
            for (Comb& c : combL_) outL += c.process(in, feedback_, damp_);
            for (Comb& c : combR_) outR += c.process(in, feedback_, damp_);
            for (AllPass& a : allL_) outL = a.process(outL);
            for (AllPass& a : allR_) outR = a.process(outR);
            const float dl = l[i], dr = r[i];
            l[i] = dl * dry_ + outL * wet1_ + outR * wet2_;
            r[i] = dr * dry_ + outR * wet1_ + outL * wet2_;
        }
    }
    nlohmann::json describe() const override { return {{"id", kProcReverb}}; }

private:
    std::vector<Comb> combL_, combR_;
    std::vector<AllPass> allL_, allR_;
    std::vector<float> pre_;
    std::size_t prePos_ = 0;
    bool preOn_ = false;
    float feedback_ = 0.84f, damp_ = 0.2f, wet1_ = 0, wet2_ = 0, dry_ = 1;
};

// ---------------------------------------------------------------- Neural Saturation

// The ten model families of CMLSaturation, in Logic's enum order (docs/logic-reference/machine-learning-models.md)
constexpr const char* kSaturationFamilies[10] = {"RTRST", "RTRHT", "MTC", "MTM", "FEO", "FTO", "FS", "FSQ", "MKBO", "MKGerm"};
constexpr int kSaturationBlock = 256;  // a model block: the latency is two of them

class MlSaturationProcessor final : public IProcessor {
public:
    MlSaturationProcessor(const ProcessorRef& ref, double sr)
        : drive_(static_cast<float>(param(ref, "drive") / 100.0)), out_(static_cast<float>(dbToLin(param(ref, "output")))), mix_(static_cast<float>(param(ref, "mix") / 100.0)) {
        const int type = std::clamp(static_cast<int>(std::lround(param(ref, "type"))), 0, 9);
        const std::filesystem::path file = ml::findFamilyModel(kSaturationFamilies[type], sr, kSaturationBlock);
        if (!file.empty()) {
            left_ = ml::NeuralStream::create(file, kSaturationBlock);
            right_ = ml::NeuralStream::create(file, kSaturationBlock);
        }
        if (!left_ || !right_) {
            left_.reset();
            right_.reset();
            return;
        }
        delay_ = left_->latency();
        dryL_.assign(static_cast<std::size_t>(delay_), 0.0f);
        dryR_.assign(static_cast<std::size_t>(delay_), 0.0f);
        cond_.assign(8192, drive_);
        wetL_.assign(8192, 0.0f);
        wetR_.assign(8192, 0.0f);
    }
    int latencySamples() const override { return delay_; }
    void process(float* l, float* r, int frames) noexcept override {
        if (!left_) return;  // no model: the sound passes
        for (int at = 0; at < frames; at += 8192) {
            const int n = std::min(8192, frames - at);
            left_->process(l + at, cond_.data(), wetL_.data(), n);
            right_->process(r + at, cond_.data(), wetR_.data(), n);
            for (int i = 0; i < n; ++i) {
                const std::size_t d = pos_ % dryL_.size();
                const float dl = dryL_[d], dr = dryR_[d];  // the dry sound as late as the processed one
                dryL_[d] = l[at + i];
                dryR_[d] = r[at + i];
                ++pos_;
                l[at + i] = (dl * (1.0f - mix_) + wetL_[static_cast<std::size_t>(i)] * mix_) * out_;
                r[at + i] = (dr * (1.0f - mix_) + wetR_[static_cast<std::size_t>(i)] * mix_) * out_;
            }
        }
    }
    nlohmann::json describe() const override { return {{"id", kProcMlSaturation}, {"model", left_ != nullptr}}; }

private:
    float drive_, out_, mix_;
    std::unique_ptr<ml::NeuralStream> left_, right_;
    int delay_ = 0;
    std::vector<float> dryL_, dryR_, cond_, wetL_, wetR_;
    std::size_t pos_ = 0;
};

// ---------------------------------------------------------------- Mastering Assistant

// The correction EQ (eight bands from the analysis, scaled by Auto EQ), a character, the loudness gain and a limiter at -1 dB.
class MasteringProcessor final : public IProcessor {
public:
    MasteringProcessor(const ProcessorRef& ref, double sr)
        : bypass_(param(ref, "bypass") >= 0.5), excite_(param(ref, "excite") >= 0.5), character_(static_cast<int>(std::lround(param(ref, "character")))),
          limiter_(limiterRef(param(ref, "loudness") / 100.0), sr, true), compressor_(compressorRef(static_cast<int>(std::lround(param(ref, "character"))), -14.0 - param(ref, "gain")), sr) {
        const double amount = param(ref, "autoEq") / 100.0;
        for (int i = 0; i < kBands; ++i) {
            const double g = param(ref, ("eq" + std::to_string(i + 1)).c_str()) * amount;
            if (i == 0) bands_[0] = lowShelf(sr, 60.0, g);
            else if (i == kBands - 1) bands_[kBands - 1] = highShelf(sr, 11000.0, g);
            else bands_[static_cast<std::size_t>(i)] = peaking(sr, kCentres[static_cast<std::size_t>(i)], g, 0.9);
        }
        if (param(ref, "customEq") >= 0.5) {  // the Custom EQ: a low shelf, a broad mid band and a high shelf
            customOn_ = true;
            custom_[0] = lowShelf(sr, param(ref, "cLowFreq"), param(ref, "cLowGain"));
            custom_[1] = peaking(sr, param(ref, "cMidFreq"), param(ref, "cMidGain"), 0.7);
            custom_[2] = highShelf(sr, param(ref, "cHighFreq"), param(ref, "cHighGain"));
        }
        width_ = param(ref, "width") / 100.0;
        // the character colours the balance a little
        if (character_ == 1) { bands_[0] = lowShelf(sr, 80.0, param(ref, "eq1") * amount + 1.5); drive_ = 1.6f; }
        if (character_ == 2) extra_ = peaking(sr, 2500.0, 1.5, 0.8);
        // the Loudness macro (MADSP, decompiled): the compressor's input gain, the saturation blend and the limiter's gain and release each follow
        // their own piecewise-linear curve of the macro position, so most of them stay off until the macro is well above zero
        const double macro = std::clamp(param(ref, "loudness") / 100.0, -1.0, 1.0);
        compIn_ = static_cast<float>(dbToLin(macro <= 0.0 ? macro * 13.1 : macro * 12.0));
        wet_ = macro < 0.4 ? 0.0 : (macro - 0.4) / 0.6;
        totalGain_ = param(ref, "gain");
        gain_ = static_cast<float>(dbToLin(param(ref, "compensate") >= 0.5 ? param(ref, "compTrim") : totalGain_));  // compensation: the level the analysis measured
        // the Analog Model (Valve, Punch, Transparent = TSO, G_SO, S_SO; Clean has none): Logic's trained network, in the dynamics stage. Without the model files
        // the approximation above stays.
        if (character_ >= 1 && character_ <= 3) {
            static constexpr const char* kNames[3] = {"TSO", "G_SO", "S_SO"};
            static constexpr double kLinearGainDb[3] = {-2.1, -1.1, -4.0};  // the network's gain for a quiet signal (measured with sine tones)
            const std::filesystem::path file = ml::findAnalogModel(kNames[character_ - 1], sr);
            if (!file.empty()) {
                analogL_ = ml::NeuralStream::create(file, kAnalogBlock);
                analogR_ = ml::NeuralStream::create(file, kAnalogBlock);
            }
            if (analogL_ && analogR_) {
                // the networks are linear below about -30 dBFS and compress above: they are fed 12 dB lower, so that a mastered mix lands in the part that colours
                // without squashing, and the lost level is made up after them (the quiet-signal gain is made up too)
                analogPre_ = static_cast<float>(dbToLin(-12.0));
                analogPost_ = static_cast<float>(dbToLin(12.0 - kLinearGainDb[character_ - 1]));
                analogDelay_ = analogL_->latency();
                analogBuf_.assign(8192, 0.0f);
                analogOut_.assign(8192, 0.0f);
                drive_ = 1.0f;  // the network is the character: the tube-like soft saturation of the approximation is not added
            } else {
                analogL_.reset();
                analogR_.reset();
            }
        }
        // a delay as long as the limiter's, so that bypassing does not shift the sound
        delay_.assign(static_cast<std::size_t>(std::max(1, limiter_.latencySamples() + analogDelay_)), 0.0f);
        delayR_ = delay_;
        highpass_ = 1.0 - std::exp(-2.0 * kPi * 2500.0 / sr);
    }
    int latencySamples() const override { return limiter_.latencySamples() + analogDelay_; }
    void process(float* l, float* r, int frames) noexcept override {
        if (bypass_) {
            for (int i = 0; i < frames; ++i) {
                const std::size_t d = pos_ % delay_.size();
                const float ol = delay_[d], orr = delayR_[d];
                delay_[d] = l[i];
                delayR_[d] = r[i];
                ++pos_;
                l[i] = ol;
                r[i] = orr;
            }
            return;
        }
        for (int i = 0; i < frames; ++i) {
            double xl = l[i], xr = r[i];
            for (std::size_t b = 0; b < bands_.size(); ++b) { xl = biquad(bands_[b], z_[b], 0, xl); xr = biquad(bands_[b], z_[b], 2, xr); }
            if (character_ == 2) { xl = biquad(extra_, zx_, 0, xl); xr = biquad(extra_, zx_, 2, xr); }
            if (customOn_)
                for (std::size_t b = 0; b < custom_.size(); ++b) { xl = biquad(custom_[b], zc_[b], 0, xl); xr = biquad(custom_[b], zc_[b], 2, xr); }
            if (width_ != 1.0) {  // Spread: the side signal is scaled (0 leaves the mid only: mono)
                const double mid = 0.5 * (xl + xr), side = 0.5 * (xl - xr) * width_;
                xl = mid + side;
                xr = mid - side;
            }
            if (excite_) {  // saturated highs mixed back in
                lowL_ += highpass_ * (xl - lowL_);
                lowR_ += highpass_ * (xr - lowR_);
                xl += 0.12 * std::tanh(3.0 * (xl - lowL_));
                xr += 0.12 * std::tanh(3.0 * (xr - lowR_));
            }
            if (drive_ > 1.0f) {  // tube-like soft saturation
                xl = std::tanh(drive_ * xl) / std::tanh(static_cast<double>(drive_));
                xr = std::tanh(drive_ * xr) / std::tanh(static_cast<double>(drive_));
            }
            l[i] = static_cast<float>(xl);
            r[i] = static_cast<float>(xr);
        }
        if (compIn_ != 1.0f)
            for (int i = 0; i < frames; ++i) { l[i] *= compIn_; r[i] *= compIn_; }
        compressor_.process(l, r, frames);  // the dynamics are shaped at the level the mix has, then the loudness is made up and the limiter holds the peaks
        if (analogL_) {  // the Analog Model network, one stream per channel (the answer comes two blocks late: latencySamples)
            for (int at = 0; at < frames; at += 8192) {
                const int n = std::min(8192, frames - at);
                for (float* side : {l, r}) {
                    ml::NeuralStream& stream = side == l ? *analogL_ : *analogR_;
                    for (int i = 0; i < n; ++i) analogBuf_[static_cast<std::size_t>(i)] = side[at + i] * analogPre_;
                    stream.process(analogBuf_.data(), nullptr, analogOut_.data(), n);
                    for (int i = 0; i < n; ++i) side[at + i] = analogOut_[static_cast<std::size_t>(i)] * analogPost_;
                }
            }
        }
        for (int i = 0; i < frames; ++i) {  // the gain, and a soft clip that takes the tallest few dB off the peaks before the limiter has to hold the rest
            double yl = static_cast<double>(l[i]) * gain_, yr = static_cast<double>(r[i]) * gain_;
            if (wet_ > 0.0) {  // the wave shaper, blended in from the macro position 0.4 on
                yl += wet_ * (0.5 * std::tanh(2.0 * yl) - yl);
                yr += wet_ * (0.5 * std::tanh(2.0 * yr) - yr);
            }
            l[i] = static_cast<float>(kClip * std::tanh(yl / kClip));
            r[i] = static_cast<float>(kClip * std::tanh(yr / kClip));
        }
        limiter_.process(l, r, frames);
    }
    float reductionDb() const noexcept override { return std::max(compressor_.reductionDb(), 0.0f); }
    nlohmann::json describe() const override { return {{"id", kProcMastering}, {"bypass", bypass_}, {"gainDb", totalGain_}}; }

private:
    static constexpr int kAnalogBlock = 1024;  // the networks' block (the TSO, G and S files are `modelstreaming`, 1024 samples)
    std::unique_ptr<ml::NeuralStream> analogL_, analogR_;
    float analogPre_ = 1.0f, analogPost_ = 1.0f;
    int analogDelay_ = 0;
    std::vector<float> analogBuf_, analogOut_;
    static constexpr double kClip = 0.9;  // the soft clip's ceiling, about -0.9 dBFS
    static constexpr int kBands = 8;
    static constexpr std::array<double, kBands> kCentres{40, 100, 250, 630, 1600, 4000, 8000, 14000};
    // the limiter's input gain and release from the macro position (neutral at 0: Apple's 2.8 dB is part of the loudness the analysis already measures)
    static ProcessorRef limiterRef(double macro) {
        macro = std::clamp(macro, -1.0, 1.0);
        const double gain = macro < 0.6 ? 0.0 : 3.25 * (macro - 0.6);
        const double release = macro < 0.4 ? 2.0 : 2.0 + 44.0 * (macro - 0.4) / 0.6;
        ProcessorRef r;
        r.processorId = kProcLimiter;
        r.params = {{"gain", gain}, {"ceiling", -1.0}, {"release", release}, {"lookahead", 1.0}, {"softKnee", 0.0}, {"truePeak", 1.0}};
        return r;
    }
    // `levelLufs`: the loudness of the mix before the insert (the analysis stored the gain that brings it to -14), so the threshold sits a few dB above it
    static ProcessorRef compressorRef(int character, double levelLufs) {
        ProcessorRef r;
        r.processorId = kProcCompressor;
        // Clean: gentle; Valve: slow and warm; Punch: fast attack lets less through; Transparent: tight and light
        static const double above[4] = {4, 3, 2, 3}, ratio[4] = {1.6, 2.0, 2.5, 1.8}, attack[4] = {25, 40, 12, 8}, release[4] = {160, 220, 90, 120};
        const int c = std::clamp(character, 0, 3);
        const double threshold = std::clamp(levelLufs + above[c], -60.0, 0.0);
        r.params = {{"circuit", 0.0}, {"threshold", threshold}, {"ratio", ratio[c]}, {"attack", attack[c]}, {"release", release[c]}, {"autoRelease", 0.0},
                    {"gain", 0.0}, {"autoGain", 0.0}, {"knee", 0.4}, {"peakRms", 0.0}, {"outputDistortion", 0.0}, {"limiter", 0.0}, {"mix", 100.0}};
        return r;
    }
    static double biquad(const BiquadCoeffs& c, std::array<double, 4>& z, int off, double x) {
        const double y = c.b0 * x + z[static_cast<std::size_t>(off)];
        z[static_cast<std::size_t>(off)] = c.b1 * x - c.a1 * y + z[static_cast<std::size_t>(off) + 1];
        z[static_cast<std::size_t>(off) + 1] = c.b2 * x - c.a2 * y;
        return y;
    }
    bool bypass_, excite_;
    int character_;
    LimiterProcessor limiter_;
    CompressorProcessor compressor_;
    std::array<BiquadCoeffs, kBands> bands_;
    std::array<std::array<double, 4>, kBands> z_{};
    BiquadCoeffs extra_;
    std::array<double, 4> zx_{};
    bool customOn_ = false;
    std::array<BiquadCoeffs, 3> custom_;
    std::array<std::array<double, 4>, 3> zc_{};
    double width_ = 1.0;
    float drive_ = 1.0f, gain_ = 1.0f, compIn_ = 1.0f;
    double wet_ = 0.0;
    double totalGain_ = 0.0, highpass_ = 0.2, lowL_ = 0.0, lowR_ = 0.0;
    std::vector<float> delay_, delayR_;
    std::size_t pos_ = 0;
};

}  // namespace

std::unique_ptr<IProcessor> makeBuiltinEffect(const ProcessorRef& ref, double sampleRate) {
    const std::string& id = ref.processorId;
    if (id == kProcEq) return std::make_unique<EqProcessor>(ref, sampleRate);
    if (id == kProcMatchEq) return std::make_unique<MatchEqProcessor>(ref, sampleRate);
    if (id == kProcMlSaturation) return std::make_unique<MlSaturationProcessor>(ref, sampleRate);
    if (id == kProcCompressor) return std::make_unique<CompressorProcessor>(ref, sampleRate);
    if (id == kProcLimiter) return std::make_unique<LimiterProcessor>(ref, sampleRate);
    if (id == kProcAdaptiveLimiter) return std::make_unique<AdaptiveLimiterProcessor>(ref, sampleRate);
    if (id == kProcGate) return std::make_unique<GateProcessor>(ref, sampleRate);
    if (id == kProcDelay) return std::make_unique<DelayProcessor>(ref, sampleRate);
    if (id == kProcReverb) return std::make_unique<ReverbProcessor>(ref, sampleRate);
    if (id == kProcMastering) return std::make_unique<MasteringProcessor>(ref, sampleRate);
    return nullptr;
}

double eqResponseDb(const ProcessorRef& ref, double sampleRate, double freq) {
    if (param(ref, "hq") >= 0.5) sampleRate *= 2.0;  // HQ: the filters run at twice the rate
    const double w = 2 * kPi * freq / sampleRate;
    double db = param(ref, "masterGain");
    for (const BiquadCoeffs& c : eqSections(ref, sampleRate)) db += 20.0 * std::log10(std::max(std::abs(biquadResponse(c, w)), 1e-12));
    return db;
}

}  // namespace lpc::audio
