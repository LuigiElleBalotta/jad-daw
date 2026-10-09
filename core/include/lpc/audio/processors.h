#pragma once
#include <array>
#include <cstdint>
#include <memory>

#include <nlohmann/json.hpp>

#include "lpc/model.h"

namespace lpc::audio {

// In-place stereo effect. process() runs on the audio thread.
class IProcessor {
public:
    virtual ~IProcessor() = default;
    // Project thread, before the processor is handed to the audio thread.
    virtual void prepare(double /*sampleRate*/, int /*maxBlock*/) {}
    // Latency in frames, read on the project thread when the graph is built.
    virtual int latencySamples() const { return 0; }
    virtual void process(float* l, float* r, int frames) noexcept = 0;
    // The gain reduction (dB, 0 or more) of the last block, for the strip's meter; read on the audio thread after process().
    virtual float reductionDb() const noexcept { return 0.0f; }
    virtual nlohmann::json describe() const = 0;  // not real-time; used by tests
};

class GainProcessor final : public IProcessor {
public:
    explicit GainProcessor(float gainDb);
    void process(float* l, float* r, int frames) noexcept override;
    nlohmann::json describe() const override;

private:
    float gain_;
};

// Stands in for a plug-in that is not available (missing, still loading, failed): the signal passes unchanged.
class MissingPluginProcessor final : public IProcessor {
public:
    void process(float*, float*, int) noexcept override {}
    nlohmann::json describe() const override {
        nlohmann::json j = nlohmann::json::object();
        j["missing"] = true;
        return j;
    }
};

// An insert backed by a processor that outlives the config that uses it (a live plug-in instance).
class SharedProcessor final : public IProcessor {
public:
    explicit SharedProcessor(std::shared_ptr<IProcessor> inner) : inner_(std::move(inner)) {}
    void process(float* l, float* r, int frames) noexcept override { inner_->process(l, r, frames); }
    int latencySamples() const override { return inner_->latencySamples(); }
    nlohmann::json describe() const override { return inner_->describe(); }

private:
    std::shared_ptr<IProcessor> inner_;
};

// What the synth sounds like (the parameters of the "builtin.synth" instrument; "builtin.sine" is a preset of them).
struct SynthParams {
    int wave = 0;               // 0 sine, 1 triangle, 2 saw, 3 square
    float attackMs = 2.0f, decayMs = 0.0f, sustain = 1.0f, releaseMs = 5.0f;  // sustain 0..1
    float cutoffHz = 20000.0f;  // one-pole low-pass on the sum of the voices (20 kHz: open)
    float level = 0.2f;         // linear
    bool operator==(const SynthParams&) const = default;
};

// 16-voice polyphonic synth: one oscillator per voice, an ADSR envelope, a low-pass filter. Mono, written to both channels.
class Synth {
public:
    explicit Synth(double sampleRate);
    void setParams(const SynthParams& p) noexcept;         // takes effect at once; sounding voices keep their phase
    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept;
    void allNotesOff() noexcept;                           // immediate silence
    void releaseAll() noexcept;                            // every sounding voice fades out (its release time)
    bool active() const noexcept;                          // a voice is sounding
    void render(float* l, float* r, int frames) noexcept;  // adds into l and r

private:
    enum class Stage : std::uint8_t { Attack, Decay, Sustain, Release };
    struct Voice {
        bool active = false;
        Stage stage = Stage::Attack;
        std::uint8_t note = 0;
        float amp = 0.0f;
        float env = 0.0f;
        double phase = 0.0;  // 0..1
        double inc = 0.0;    // phase per sample
    };
    void updateRates() noexcept;
    std::array<Voice, 16> voices_{};
    std::size_t stealNext_ = 0;
    double sampleRate_;
    SynthParams params_;
    float attackStep_ = 0, decayStep_ = 0, releaseStep_ = 0, filterCoef_ = 1.0f;
    float filterState_ = 0.0f;
};

// Creates an insert effect from a model reference; nullptr when the processor id is unknown.
// Project thread only (allocates).
std::unique_ptr<IProcessor> makeEffect(const ProcessorRef& ref, double sampleRate = 48000.0);

}  // namespace lpc::audio
