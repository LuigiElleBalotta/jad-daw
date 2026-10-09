#include "juce_device.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

namespace lpc {

namespace {

class JuceAudioDevice final : public IAudioDevice, private juce::AudioIODeviceCallback {
public:
    ~JuceAudioDevice() override { close(); }

    bool open(double sampleRate, int bufferSize, IAudioCallback& callback, std::string& error) override {
        callback_ = &callback;
        const juce::String initError = manager_.initialiseWithDefaultDevices(0, 2);
        if (initError.isNotEmpty()) {
            error = initError.toStdString();
            return false;
        }
        juce::AudioDeviceManager::AudioDeviceSetup setup = manager_.getAudioDeviceSetup();
        setup.sampleRate = sampleRate;
        setup.bufferSize = bufferSize;
        const juce::String setupError = manager_.setAudioDeviceSetup(setup, true);
        if (setupError.isNotEmpty()) {
            error = setupError.toStdString();
            return false;
        }
        juce::AudioIODevice* device = manager_.getCurrentAudioDevice();
        if (device == nullptr) {
            error = "no audio output device is available";
            return false;
        }
        rate_ = device->getCurrentSampleRate();
        block_ = device->getCurrentBufferSizeSamples();
        if (std::abs(rate_ - sampleRate) > 0.5) {
            error = "the audio device runs at " + std::to_string(static_cast<int>(rate_)) + " Hz but the project needs " +
                    std::to_string(static_cast<int>(sampleRate)) + " Hz; change the device sample rate in the system sound settings";
            manager_.closeAudioDevice();
            return false;
        }
        scratchL_.assign(kScratch, 0.0f);
        scratchR_.assign(kScratch, 0.0f);
        manager_.addAudioCallback(this);
        return true;
    }

    void close() override {
        manager_.removeAudioCallback(this);
        manager_.closeAudioDevice();
    }

    double sampleRate() const override { return rate_; }
    int bufferSize() const override { return block_; }

private:
    static constexpr int kScratch = 8192;

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override {
        if (numOutputs <= 0) return;
        if (numInputs > 0 && inputs[0] != nullptr) callback_->input(inputs[0], numInputs > 1 && inputs[1] != nullptr ? inputs[1] : inputs[0], numSamples);
        if (numOutputs >= 2) {
            callback_->process(outputs[0], outputs[1], numSamples);
            for (int c = 2; c < numOutputs; ++c) juce::FloatVectorOperations::clear(outputs[c], numSamples);
            return;
        }
        for (int done = 0; done < numSamples;) {  // mono device: mix both channels down
            const int n = std::min(numSamples - done, kScratch);
            callback_->process(scratchL_.data(), scratchR_.data(), n);
            for (int i = 0; i < n; ++i) outputs[0][done + i] = 0.5f * (scratchL_[static_cast<std::size_t>(i)] + scratchR_[static_cast<std::size_t>(i)]);
            done += n;
        }
    }

    juce::AudioDeviceManager manager_;
    IAudioCallback* callback_ = nullptr;
    double rate_ = 0.0;
    int block_ = 0;
    std::vector<float> scratchL_, scratchR_;
};

}  // namespace

std::unique_ptr<IAudioDevice> makeJuceAudioDevice() { return std::make_unique<JuceAudioDevice>(); }

}  // namespace lpc
