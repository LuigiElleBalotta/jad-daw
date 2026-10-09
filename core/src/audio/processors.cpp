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

SineSynth::SineSynth(double sampleRate)
    : sampleRate_(sampleRate),
      attackStep_(static_cast<float>(1.0 / (0.002 * sampleRate))),
      releaseStep_(static_cast<float>(1.0 / (0.005 * sampleRate))) {}

void SineSynth::noteOn(std::uint8_t note, std::uint8_t velocity) noexcept {
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
    v->releasing = false;
    v->note = note;
    v->env = 0.0f;
    v->amp = 0.2f * static_cast<float>(velocity) / 127.0f;
    v->phase = 0.0;
    v->inc = kTwoPi * 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0) / sampleRate_;
}

void SineSynth::noteOff(std::uint8_t note) noexcept {
    for (Voice& v : voices_) {
        if (v.active && !v.releasing && v.note == note) {
            v.releasing = true;
            return;
        }
    }
}

void SineSynth::releaseAll() noexcept {
    for (Voice& v : voices_)
        if (v.active) v.releasing = true;
}

void SineSynth::allNotesOff() noexcept {
    for (Voice& v : voices_) v = Voice{};
}

void SineSynth::render(float* l, float* r, int frames) noexcept {
    for (Voice& v : voices_) {
        if (!v.active) continue;
        for (int i = 0; i < frames; ++i) {
            if (v.releasing) {
                v.env -= releaseStep_;
                if (v.env <= 0.0f) {
                    v.active = false;
                    break;
                }
            } else if (v.env < 1.0f) {
                v.env = std::min(1.0f, v.env + attackStep_);
            }
            const float s = static_cast<float>(std::sin(v.phase)) * v.amp * v.env;
            v.phase += v.inc;
            if (v.phase >= kTwoPi) v.phase -= kTwoPi;
            l[i] += s;
            r[i] += s;
        }
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
