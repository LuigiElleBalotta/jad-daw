#include "lpc/audio/effects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

#include "lpc/effect_specs.h"
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

double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
double linToDb(double lin) { return 20.0 * std::log10(std::max(lin, 1e-9)); }
// the coefficient of a one-pole smoother that reaches 63 % in `ms` milliseconds
double smoothing(double ms, double sampleRate) { return std::exp(-1.0 / (std::max(ms, 0.01) * 0.001 * sampleRate)); }

// ---------------------------------------------------------------- Channel EQ

struct BiquadCoeffs {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

// the RBJ cookbook filters
BiquadCoeffs lowShelf(double sr, double f, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0), w = 2 * kPi * std::min(f, 0.45 * sr) / sr, c = std::cos(w), s = std::sin(w);
    const double alpha = s / 2.0 * std::sqrt(2.0), sq = 2.0 * std::sqrt(A) * alpha;  // shelf slope 1
    const double a0 = (A + 1) + (A - 1) * c + sq;
    return {A * ((A + 1) - (A - 1) * c + sq) / a0, 2 * A * ((A - 1) - (A + 1) * c) / a0, A * ((A + 1) - (A - 1) * c - sq) / a0,
            -2 * ((A - 1) + (A + 1) * c) / a0, ((A + 1) + (A - 1) * c - sq) / a0};
}
BiquadCoeffs highShelf(double sr, double f, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0), w = 2 * kPi * std::min(f, 0.45 * sr) / sr, c = std::cos(w), s = std::sin(w);
    const double alpha = s / 2.0 * std::sqrt(2.0), sq = 2.0 * std::sqrt(A) * alpha;
    const double a0 = (A + 1) - (A - 1) * c + sq;
    return {A * ((A + 1) + (A - 1) * c + sq) / a0, -2 * A * ((A - 1) + (A + 1) * c) / a0, A * ((A + 1) + (A - 1) * c - sq) / a0,
            2 * ((A - 1) - (A + 1) * c) / a0, ((A + 1) - (A - 1) * c - sq) / a0};
}
BiquadCoeffs peaking(double sr, double f, double gainDb, double q) {
    const double A = std::pow(10.0, gainDb / 40.0), w = 2 * kPi * std::min(f, 0.45 * sr) / sr, c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1 + alpha / A;
    return {(1 + alpha * A) / a0, -2 * c / a0, (1 - alpha * A) / a0, -2 * c / a0, (1 - alpha / A) / a0};
}

std::array<BiquadCoeffs, 5> eqBands(const ProcessorRef& ref, double sr) {
    return {lowShelf(sr, param(ref, "lowFreq"), param(ref, "lowGain")),
            peaking(sr, param(ref, "m1Freq"), param(ref, "m1Gain"), param(ref, "m1Q")),
            peaking(sr, param(ref, "m2Freq"), param(ref, "m2Gain"), param(ref, "m2Q")),
            peaking(sr, param(ref, "m3Freq"), param(ref, "m3Gain"), param(ref, "m3Q")),
            highShelf(sr, param(ref, "highFreq"), param(ref, "highGain"))};
}

class EqProcessor final : public IProcessor {
public:
    EqProcessor(const ProcessorRef& ref, double sr) : bands_(eqBands(ref, sr)), out_(static_cast<float>(dbToLin(param(ref, "outGain")))) {}
    void process(float* l, float* r, int frames) noexcept override {
        for (int i = 0; i < frames; ++i) {
            double xl = l[i], xr = r[i];
            for (std::size_t b = 0; b < bands_.size(); ++b) {  // direct form II transposed
                const BiquadCoeffs& c = bands_[b];
                double yl = c.b0 * xl + z_[b][0], yr = c.b0 * xr + z_[b][2];
                z_[b][0] = c.b1 * xl - c.a1 * yl + z_[b][1];
                z_[b][1] = c.b2 * xl - c.a2 * yl;
                z_[b][2] = c.b1 * xr - c.a1 * yr + z_[b][3];
                z_[b][3] = c.b2 * xr - c.a2 * yr;
                xl = yl;
                xr = yr;
            }
            l[i] = static_cast<float>(xl) * out_;
            r[i] = static_cast<float>(xr) * out_;
        }
    }
    nlohmann::json describe() const override { return {{"id", kProcEq}}; }

private:
    std::array<BiquadCoeffs, 5> bands_;
    std::array<std::array<double, 4>, 5> z_{};
    float out_;
};

// ---------------------------------------------------------------- Compressor

class CompressorProcessor final : public IProcessor {
public:
    CompressorProcessor(const ProcessorRef& ref, double sr)
        : threshold_(param(ref, "threshold")), ratio_(param(ref, "ratio")), knee_(param(ref, "knee")), makeup_(param(ref, "makeup")),
          mix_(param(ref, "mix") / 100.0), attack_(smoothing(param(ref, "attack"), sr)), release_(smoothing(param(ref, "release"), sr)) {}
    void process(float* l, float* r, int frames) noexcept override {
        for (int i = 0; i < frames; ++i) {
            const double level = linToDb(std::max(std::abs(static_cast<double>(l[i])), std::abs(static_cast<double>(r[i]))));
            const double over = level - threshold_;
            double target;  // the gain change in dB (0 or less)
            if (2 * over < -knee_) target = 0;
            else if (knee_ > 0 && 2 * std::abs(over) <= knee_) target = (1.0 / ratio_ - 1.0) * (over + knee_ / 2) * (over + knee_ / 2) / (2 * knee_);
            else target = over * (1.0 / ratio_ - 1.0);
            const double coef = target < reduction_ ? attack_ : release_;  // deeper: attack, shallower: release
            reduction_ = coef * reduction_ + (1 - coef) * target;
            const float gain = static_cast<float>(dbToLin(reduction_ + makeup_));
            l[i] = static_cast<float>(l[i] * (1 - mix_) + l[i] * gain * mix_);
            r[i] = static_cast<float>(r[i] * (1 - mix_) + r[i] * gain * mix_);
        }
        blockReduction_ = static_cast<float>(-reduction_);
    }
    float reductionDb() const noexcept override { return std::max(0.0f, blockReduction_); }
    nlohmann::json describe() const override { return {{"id", kProcCompressor}, {"reductionDb", reduction_}}; }

private:
    double threshold_, ratio_, knee_, makeup_, mix_, attack_, release_;
    double reduction_ = 0.0;
    float blockReduction_ = 0.0f;
};

// ---------------------------------------------------------------- Limiter

class LimiterProcessor final : public IProcessor {
public:
    LimiterProcessor(const ProcessorRef& ref, double sr)
        : gain_(static_cast<float>(dbToLin(param(ref, "gain")))), ceiling_(static_cast<float>(dbToLin(param(ref, "ceiling")))),
          look_(std::max(1, static_cast<int>(0.001 * sr))), release_(smoothing(param(ref, "release"), sr)), attack_(smoothing(0.25, sr)),
          needs_(static_cast<std::size_t>(look_ + 1), 1.0f), delayL_(static_cast<std::size_t>(look_), 0.0f), delayR_(static_cast<std::size_t>(look_), 0.0f) {}
    int latencySamples() const override { return look_; }
    void process(float* l, float* r, int frames) noexcept override {
        for (int i = 0; i < frames; ++i) {
            const float inL = l[i] * gain_, inR = r[i] * gain_;
            const float peak = std::max(std::abs(inL), std::abs(inR));
            needs_[pos_ % needs_.size()] = peak > ceiling_ ? ceiling_ / peak : 1.0f;
            float target = 1.0f;  // the strongest reduction asked for in the last millisecond: it is in place before the loud sample leaves
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
    nlohmann::json describe() const override { return {{"id", kProcLimiter}}; }

private:
    float gain_, ceiling_;
    int look_;
    double release_, attack_;
    std::vector<float> needs_, delayL_, delayR_;
    std::size_t pos_ = 0;
    float gain_state_ = 1.0f;
};

// ---------------------------------------------------------------- Noise gate

class GateProcessor final : public IProcessor {
public:
    GateProcessor(const ProcessorRef& ref, double sr)
        : threshold_(param(ref, "threshold")), floor_(dbToLin(param(ref, "range"))), hold_(static_cast<int>(param(ref, "hold") * 0.001 * sr)),
          attack_(smoothing(param(ref, "attack"), sr)), release_(smoothing(param(ref, "release"), sr)), decay_(smoothing(5.0, sr)) {}
    void process(float* l, float* r, int frames) noexcept override {
        for (int i = 0; i < frames; ++i) {
            const double x = std::max(std::abs(static_cast<double>(l[i])), std::abs(static_cast<double>(r[i])));
            env_ = std::max(x, env_ * decay_);
            const double db = linToDb(env_);
            if (db > threshold_) { open_ = true; held_ = hold_; }
            else if (open_ && db < threshold_ - 3.0) { if (held_ > 0) --held_; else open_ = false; }  // 3 dB of hysteresis, then the hold time
            const double target = open_ ? 1.0 : floor_;
            const double coef = target > gain_ ? attack_ : release_;
            gain_ = coef * gain_ + (1 - coef) * target;
            l[i] = static_cast<float>(l[i] * gain_);
            r[i] = static_cast<float>(r[i] * gain_);
        }
    }
    nlohmann::json describe() const override { return {{"id", kProcGate}, {"open", open_}}; }

private:
    double threshold_, floor_;
    int hold_;
    double attack_, release_, decay_;
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

}  // namespace

std::unique_ptr<IProcessor> makeBuiltinEffect(const ProcessorRef& ref, double sampleRate) {
    const std::string& id = ref.processorId;
    if (id == kProcEq) return std::make_unique<EqProcessor>(ref, sampleRate);
    if (id == kProcCompressor) return std::make_unique<CompressorProcessor>(ref, sampleRate);
    if (id == kProcLimiter) return std::make_unique<LimiterProcessor>(ref, sampleRate);
    if (id == kProcGate) return std::make_unique<GateProcessor>(ref, sampleRate);
    if (id == kProcDelay) return std::make_unique<DelayProcessor>(ref, sampleRate);
    if (id == kProcReverb) return std::make_unique<ReverbProcessor>(ref, sampleRate);
    return nullptr;
}

double eqResponseDb(const ProcessorRef& ref, double sampleRate, double freq) {
    const double w = 2 * kPi * freq / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2 * w);
    double db = param(ref, "outGain");
    for (const BiquadCoeffs& c : eqBands(ref, sampleRate)) {
        const std::complex<double> num = c.b0 + c.b1 * z1 + c.b2 * z2, den = 1.0 + c.a1 * z1 + c.a2 * z2;
        db += 20.0 * std::log10(std::max(std::abs(num / den), 1e-12));
    }
    return db;
}

}  // namespace lpc::audio
