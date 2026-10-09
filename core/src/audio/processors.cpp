#include "lpc/audio/processors.h"

#include <algorithm>
#include <cmath>

#include "lpc/audio/effects.h"
#include "lpc/processor_ids.h"

namespace lpc::audio {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
}  // namespace

GainProcessor::GainProcessor(float gainDb) : gain_(std::pow(10.0f, gainDb / 20.0f)) {}

void GainProcessor::process(float* l, float* r, int frames) noexcept {
    for (int i = 0; i < frames; ++i) {
        l[i] *= gain_;
        r[i] *= gain_;
    }
}

nlohmann::json GainProcessor::describe() const { return {{"id", kProcGain}, {"gain", gain_}}; }

Synth::Synth(double sampleRate) : sampleRate_(sampleRate) { updateRates(); }

void Synth::updateRates() noexcept {
    attackStep_ = static_cast<float>(1.0 / (std::max(0.05, static_cast<double>(params_.attackMs)) * 0.001 * sampleRate_));
    decayStep_ = static_cast<float>(1.0 / (std::max(0.05, static_cast<double>(params_.decayMs)) * 0.001 * sampleRate_));
    releaseStep_ = static_cast<float>(1.0 / (std::max(0.05, static_cast<double>(params_.releaseMs)) * 0.001 * sampleRate_));
    const double fc = std::clamp(static_cast<double>(params_.cutoffHz), 20.0, 0.45 * sampleRate_);
    filterCoef_ = params_.cutoffHz >= 19999.0f ? 1.0f : static_cast<float>(1.0 - std::exp(-kTwoPi * fc / sampleRate_));
}

void Synth::setParams(const SynthParams& p) noexcept {
    if (p == params_) return;
    params_ = p;
    updateRates();
}

bool Synth::active() const noexcept {
    for (const Voice& v : voices_)
        if (v.active) return true;
    return false;
}

void Synth::noteOn(std::uint8_t note, std::uint8_t velocity) noexcept {
    Voice* v = nullptr;
    for (Voice& candidate : voices_) {
        if (!candidate.active) {
            v = &candidate;
            break;
        }
    }
    if (!v) {  // all voices busy: steal in round-robin order
        v = &voices_[stealNext_];
        stealNext_ = (stealNext_ + 1) % voices_.size();
    }
    v->active = true;
    v->stage = Stage::Attack;
    v->note = note;
    v->env = 0.0f;
    v->amp = params_.level * static_cast<float>(velocity) / 127.0f;
    v->phase = 0.0;
    v->inc = 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0) / sampleRate_;
}

void Synth::noteOff(std::uint8_t note) noexcept {
    for (Voice& v : voices_) {
        if (v.active && v.stage != Stage::Release && v.note == note) {
            v.stage = Stage::Release;
            return;
        }
    }
}

void Synth::releaseAll() noexcept {
    for (Voice& v : voices_)
        if (v.active) v.stage = Stage::Release;
}

void Synth::allNotesOff() noexcept {
    for (Voice& v : voices_) v = Voice{};
    filterState_ = 0.0f;
}

namespace {
// the band-limited step (PolyBLEP) that tames the edges of the saw and square
inline double polyBlep(double t, double dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}
}  // namespace

void Synth::render(float* l, float* r, int frames) noexcept {
    float mix[512];
    const int n = std::min(frames, 512);
    std::fill_n(mix, n, 0.0f);
    for (Voice& v : voices_) {
        if (!v.active) continue;
        for (int i = 0; i < n; ++i) {
            switch (v.stage) {
                case Stage::Attack:
                    v.env += attackStep_;
                    if (v.env >= 1.0f) { v.env = 1.0f; v.stage = Stage::Decay; }
                    break;
                case Stage::Decay:
                    v.env -= decayStep_ * (1.0f - params_.sustain);
                    if (v.env <= params_.sustain) { v.env = params_.sustain; v.stage = Stage::Sustain; }
                    break;
                case Stage::Sustain: v.env = params_.sustain; break;
                case Stage::Release:
                    v.env -= releaseStep_;
                    if (v.env <= 0.0f) { v.active = false; v.env = 0.0f; }
                    break;
            }
            if (!v.active) break;
            double s;
            switch (params_.wave) {
                case 1: s = 4.0 * std::abs(v.phase - 0.5) - 1.0; break;                                    // triangle
                case 2: s = 2.0 * v.phase - 1.0 - polyBlep(v.phase, v.inc); break;                        // saw
                case 3: s = (v.phase < 0.5 ? 1.0 : -1.0) + polyBlep(v.phase, v.inc) - polyBlep(std::fmod(v.phase + 0.5, 1.0), v.inc); break;  // square
                default: s = std::sin(kTwoPi * v.phase); break;                                           // sine
            }
            mix[i] += static_cast<float>(s) * v.amp * v.env;
            v.phase += v.inc;
            if (v.phase >= 1.0) v.phase -= 1.0;
        }
    }
    for (int i = 0; i < n; ++i) {
        filterState_ += filterCoef_ * (mix[i] - filterState_);
        const float out = filterCoef_ >= 1.0f ? mix[i] : filterState_;
        l[i] += out;
        r[i] += out;
    }
}

std::unique_ptr<IProcessor> makeEffect(const ProcessorRef& ref, double sampleRate) {
    if (ref.processorId == kProcGain) {
        const auto it = ref.params.find("gainDb");
        return std::make_unique<GainProcessor>(it == ref.params.end() ? 0.0f : static_cast<float>(it->second));
    }
    return makeBuiltinEffect(ref, sampleRate);
}

}  // namespace lpc::audio
