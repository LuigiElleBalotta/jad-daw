#include "lpc/audio/object_panner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "lpc/effect_specs.h"
#include "lpc/processor_ids.h"

namespace lpc::audio {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHeadRadius = 0.0875;  // m
constexpr double kSoundSpeed = 343.0;   // m/s

double clamped(const ProcessorRef& ref, const char* name) {
    const EffectSpec* spec = findEffectSpec(kProcObjectPanner);
    const EffectParam* p = spec ? spec->find(name) : nullptr;
    if (!p) return 0.0;
    const auto it = ref.params.find(name);
    return std::clamp(it == ref.params.end() ? p->def : it->second, p->min, p->max);
}

}  // namespace

EarCues earCues(double x, double y, double z, double size) {
    x = std::clamp(x, -1.0, 1.0);
    y = std::clamp(y, -1.0, 1.0);
    z = std::clamp(z, 0.0, 1.0);
    size = std::clamp(size, 0.0, 1.0);
    const double azimuth = std::atan2(x, y);                         // 0 in front, positive to the right
    const double elevation = std::atan2(z, std::hypot(x, y) + 1e-9);  // 0 on the floor plane, pi/2 straight up
    // the side the sound comes from, as seen by the ears (a sound straight above or in front is in the middle)
    double lateral = std::asin(std::clamp(std::sin(azimuth) * std::cos(elevation), -1.0, 1.0));
    lateral *= 1.0 - 0.7 * size;  // a big object is not placed so sharply
    const double itd = kHeadRadius / kSoundSpeed * (std::abs(lateral) + std::sin(std::abs(lateral)));  // Woodworth
    const double shadow = std::abs(std::sin(lateral));                                                  // 0 centre .. 1 at a side
    const double distance = std::sqrt(x * x + y * y + z * z);
    const double level = 1.0 / std::max(1.0, distance);
    EarCues c;
    const double farGain = level * std::pow(10.0, -6.0 * shadow / 20.0);
    const double farCut = 20000.0 * std::pow(2500.0 / 20000.0, shadow);  // 20 kHz in the middle, 2.5 kHz when the ear is turned away
    if (lateral > 0) {  // to the right: the left ear is the far one
        c.delayLeft = itd;
        c.gainLeft = farGain;
        c.cutoffLeft = farCut;
        c.gainRight = level;
    } else {
        c.delayRight = itd;
        c.gainRight = farGain;
        c.cutoffRight = farCut;
        c.gainLeft = level;
    }
    return c;
}

namespace {

class ObjectPanner final : public IProcessor {
public:
    ObjectPanner(const ProcessorRef& ref, double sampleRate) : sampleRate_(sampleRate), line_(512, 0.0f) {
        target_[0] = clamped(ref, "x");
        target_[1] = clamped(ref, "y");
        target_[2] = clamped(ref, "z");
        target_[3] = clamped(ref, "size");
        mode_ = clamped(ref, "mode");
        for (int i = 0; i < 4; ++i) now_[i] = target_[i];
        smoothing_ = 1.0 - std::exp(-1.0 / (0.01 * sampleRate_));  // 10 ms
        cutoffCoef(20000.0, lpL_.a);
        lpR_.a = lpL_.a;
        cues_ = earCues(now_[0], now_[1], now_[2], now_[3]);
        cur_ = cues_;
    }

    // The automated value (0..1) of a parameter, in the order of the spec: x, y, z, size, mode.
    void setParameter(int index, float normalized) noexcept override {
        if (index < 0 || index > 4) return;
        const double n = std::clamp<double>(normalized, 0.0, 1.0);
        switch (index) {
            case 0: target_[0] = -1.0 + 2.0 * n; break;
            case 1: target_[1] = -1.0 + 2.0 * n; break;
            case 2: target_[2] = n; break;
            case 3: target_[3] = n; break;
            default: mode_ = n >= 0.5 ? 1.0 : 0.0; break;
        }
    }

    void process(float* l, float* r, int frames) noexcept override {
        const std::size_t mask = line_.size() - 1;
        for (int i = 0; i < frames; ++i) {
            for (int k = 0; k < 4; ++k) now_[k] += (target_[k] - now_[k]) * smoothing_;
            const float mono = 0.5f * (l[i] + r[i]);
            if (mode_ < 0.5) {  // a stereo pan from the left/right place (the same law as the strips: -3 dB in the middle)
                const double angle = (std::clamp(now_[0], -1.0, 1.0) + 1.0) * 0.25 * kPi;
                l[i] = mono * static_cast<float>(std::cos(angle)) * 1.4142135f;
                r[i] = mono * static_cast<float>(std::sin(angle)) * 1.4142135f;
                continue;
            }
            // the cues are recomputed every 16 frames, and the values in between follow them smoothly
            if ((counter_++ & 15) == 0) cues_ = earCues(now_[0], now_[1], now_[2], now_[3]);
            const double s = smoothing_ * 4.0;
            cur_.delayLeft += (cues_.delayLeft - cur_.delayLeft) * s;
            cur_.delayRight += (cues_.delayRight - cur_.delayRight) * s;
            cur_.gainLeft += (cues_.gainLeft - cur_.gainLeft) * s;
            cur_.gainRight += (cues_.gainRight - cur_.gainRight) * s;
            cur_.cutoffLeft += (cues_.cutoffLeft - cur_.cutoffLeft) * s;
            cur_.cutoffRight += (cues_.cutoffRight - cur_.cutoffRight) * s;
            line_[head_ & mask] = mono;
            const float dl = tap(cur_.delayLeft * sampleRate_), dr = tap(cur_.delayRight * sampleRate_);
            ++head_;
            cutoffCoef(cur_.cutoffLeft, lpL_.a);
            cutoffCoef(cur_.cutoffRight, lpR_.a);
            float outL = lpL_.run(dl), outR = lpR_.run(dr);
            // behind the head the top end is dull, above it lifts
            const double behind = std::clamp(-now_[1], 0.0, 1.0), above = now_[2];
            const float lowL = backL_ += static_cast<float>(0.35) * (outL - backL_), lowR = backR_ += static_cast<float>(0.35) * (outR - backR_);
            outL = lowL + (outL - lowL) * static_cast<float>(1.0 - 0.55 * behind + 0.35 * above);
            outR = lowR + (outR - lowR) * static_cast<float>(1.0 - 0.55 * behind + 0.35 * above);
            l[i] = outL * static_cast<float>(cur_.gainLeft);
            r[i] = outR * static_cast<float>(cur_.gainRight);
        }
    }

    nlohmann::json describe() const override { return {{"id", kProcObjectPanner}, {"x", target_[0]}, {"y", target_[1]}, {"z", target_[2]}, {"size", target_[3]}, {"mode", mode_}}; }

private:
    struct OnePole {
        double a = 1.0;
        float z = 0.0f;
        float run(float in) noexcept { return z += static_cast<float>(a) * (in - z); }
    };
    void cutoffCoef(double hz, double& a) const noexcept { a = 1.0 - std::exp(-2.0 * kPi * std::min(hz, 0.45 * sampleRate_) / sampleRate_); }
    float tap(double delayFrames) const noexcept {
        const std::size_t mask = line_.size() - 1;
        const double d = std::clamp(delayFrames, 0.0, static_cast<double>(line_.size() - 2));
        const std::size_t whole = static_cast<std::size_t>(d);
        const float frac = static_cast<float>(d - static_cast<double>(whole));
        const float a = line_[(head_ - whole) & mask], b = line_[(head_ - whole - 1) & mask];
        return a + (b - a) * frac;
    }

    double sampleRate_;
    double smoothing_ = 0.0;
    std::array<double, 4> target_{}, now_{};
    double mode_ = 1.0;
    std::vector<float> line_;
    std::size_t head_ = 0;
    unsigned counter_ = 0;
    EarCues cues_, cur_;
    OnePole lpL_, lpR_;
    float backL_ = 0.0f, backR_ = 0.0f;
};

}  // namespace

std::unique_ptr<IProcessor> makeObjectPanner(const ProcessorRef& ref, double sampleRate) { return std::make_unique<ObjectPanner>(ref, sampleRate); }

}  // namespace lpc::audio
