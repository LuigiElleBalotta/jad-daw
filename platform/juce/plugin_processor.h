#pragma once
#include <memory>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "lpc/audio/processors.h"

namespace lpc {

// Adapter from a JUCE plug-in instance to the engine's IProcessor. process() and latencySamples() are for the audio and
// project threads; everything else is for the message thread.
class PluginProcessor final : public audio::IProcessor {
public:
    // Applies the stereo layout and the state, prepares the instance. nullptr when the plug-in refuses stereo in and out.
    // The returned object is destroyed on the message thread, whichever thread drops the last reference.
    static std::shared_ptr<PluginProcessor> create(std::unique_ptr<juce::AudioPluginInstance> instance, const std::string& stateBase64,
                                                   double sampleRate, int maxBlock);

    void process(float* l, float* r, int frames) noexcept override;
    int latencySamples() const override { return latency_; }
    nlohmann::json describe() const override { return {{"plugin", name_}, {"latency", latency_}}; }

    // The model keeps plug-in state as standard base64 (padded); JUCE's own MemoryBlock encoding is a different alphabet.
    static std::string encodeState(const juce::MemoryBlock& block);
    static bool decodeState(const std::string& base64, juce::MemoryBlock& out);

    std::string captureState();                     // base64 of the plug-in's saved state
    bool hasEditor() const { return instance_->hasEditor(); }
    juce::AudioProcessorEditor* createEditor();     // the caller owns it and must delete it before this object goes
    const std::string& name() const { return name_; }

private:
    PluginProcessor(std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlock);
    ~PluginProcessor() override;

    std::unique_ptr<juce::AudioPluginInstance> instance_;
    juce::MidiBuffer midi_;
    int maxBlock_;
    int latency_ = 0;
    std::string name_;
};

}  // namespace lpc
