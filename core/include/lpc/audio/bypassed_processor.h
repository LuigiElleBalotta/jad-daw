#pragma once
#include <algorithm>
#include <memory>
#include <vector>

#include "lpc/audio/processors.h"
#include "lpc/audio/render_graph.h"

namespace lpc::audio {

// An insert that is switched off: the signal passes unchanged, delayed by the latency of the processor it replaces so that
// delay compensation (which still counts that latency) keeps every track in time. The inner processor is kept, not run.
class BypassedProcessor final : public IProcessor {
public:
    explicit BypassedProcessor(std::unique_ptr<IProcessor> inner)
        : inner_(std::move(inner)), delay_(inner_->latencySamples()), l_(kMaxBlock), r_(kMaxBlock) {}
    void prepare(double sampleRate, int maxBlock) override { inner_->prepare(sampleRate, maxBlock); }
    int latencySamples() const override { return inner_->latencySamples(); }
    void process(float* l, float* r, int frames) noexcept override {
        if (delay_.frames() == 0) return;
        for (int done = 0; done < frames; done += kMaxBlock) {
            const int n = std::min(kMaxBlock, frames - done);
            std::copy_n(l + done, n, l_.begin());
            std::copy_n(r + done, n, r_.begin());
            delay_.process(l_.data(), r_.data(), l + done, r + done, n);
        }
    }
    nlohmann::json describe() const override { return {{"bypassed", true}, {"inner", inner_->describe()}}; }

private:
    std::unique_ptr<IProcessor> inner_;
    DelayLine delay_;
    std::vector<float> l_, r_;
};

}  // namespace lpc::audio
