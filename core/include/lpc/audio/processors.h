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

// 16-voice sine synth with 2 ms attack and 5 ms release. Mono voice, written to both channels.
class SineSynth {
public:
    explicit SineSynth(double sampleRate);
    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept;
    void allNotesOff() noexcept;                           // immediate silence
    void releaseAll() noexcept;                            // every sounding voice fades out (5 ms)
    void render(float* l, float* r, int frames) noexcept;  // adds into l and r

private:
    struct Voice {
        bool active = false;
        bool releasing = false;
        std::uint8_t note = 0;
        float amp = 0.0f;
        float env = 0.0f;
        double phase = 0.0;
        double inc = 0.0;
    };
    std::array<Voice, 16> voices_{};
    std::size_t stealNext_ = 0;
    double sampleRate_;
    float attackStep_;
    float releaseStep_;
};

// Creates an insert effect from a model reference; nullptr when the processor id is unknown.
// Project thread only (allocates).
std::unique_ptr<IProcessor> makeEffect(const ProcessorRef& ref);

}  // namespace lpc::audio
