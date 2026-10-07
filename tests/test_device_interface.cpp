#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <memory>
#include "lpc/audio/engine.h"
#include "lpc/device.h"

using namespace lpc;

namespace {

// A device that never touches hardware: the "device thread" is the test thread calling pump().
class FakeDevice final : public IAudioDevice {
public:
    bool open(double sampleRate, int bufferSize, IAudioCallback& cb, std::string& error) override {
        if (sampleRate != 48000.0) {
            error = "unsupported sample rate";
            return false;
        }
        rate_ = sampleRate;
        block_ = bufferSize;
        cb_ = &cb;
        return true;
    }
    void close() override { cb_ = nullptr; }
    double sampleRate() const override { return rate_; }
    int bufferSize() const override { return block_; }
    void pump(float* l, float* r) { if (cb_) cb_->process(l, r, block_); }

private:
    IAudioCallback* cb_ = nullptr;
    double rate_ = 0.0;
    int block_ = 0;
};

struct EngineCallback final : IAudioCallback {
    explicit EngineCallback(audio::AudioEngine& e) : engine(e) {}
    void process(float* l, float* r, int n) noexcept override { engine.processBlock(l, r, n); }
    audio::AudioEngine& engine;
};

}  // namespace

TEST_CASE("device: an engine can be driven through the device interface", "[device]") {
    audio::AudioEngine engine(48000.0);
    EngineCallback cb(engine);
    FakeDevice dev;
    std::string error;
    REQUIRE(dev.open(48000.0, 128, cb, error));
    audio::AudioMsg play;
    play.kind = audio::MsgKind::Play;
    REQUIRE(engine.postMessage(play));
    std::vector<float> l(128), r(128);
    dev.pump(l.data(), r.data());
    REQUIRE(engine.playing());
    REQUIRE(engine.positionFrames() == 128);
    dev.close();
}

TEST_CASE("device: opening with an unsupported rate reports an error", "[device]") {
    audio::AudioEngine engine(44100.0);
    EngineCallback cb(engine);
    FakeDevice dev;
    std::string error;
    REQUIRE_FALSE(dev.open(44100.0, 128, cb, error));
    REQUIRE_FALSE(error.empty());
}
