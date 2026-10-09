#include "plugin_processor.h"

#include <algorithm>

namespace lpc {

PluginProcessor::PluginProcessor(std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlock)
    : instance_(std::move(instance)), maxBlock_(maxBlock), name_(instance_->getName().toStdString()) {}

PluginProcessor::~PluginProcessor() { instance_->releaseResources(); }

std::shared_ptr<PluginProcessor> PluginProcessor::create(std::unique_ptr<juce::AudioPluginInstance> instance, const std::string& stateBase64,
                                                         double sampleRate, int maxBlock) {
    if (!instance) return nullptr;
    juce::AudioProcessor::BusesLayout layout;
    for (int i = 0; i < instance->getBusCount(true); ++i)
        layout.inputBuses.add(i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
    for (int i = 0; i < instance->getBusCount(false); ++i)
        layout.outputBuses.add(i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
    if (instance->getBusCount(true) < 1 || instance->getBusCount(false) < 1 || !instance->setBusesLayout(layout)) return nullptr;

    if (!stateBase64.empty()) {
        juce::MemoryBlock block;
        if (decodeState(stateBase64, block) && block.getSize() > 0)
            instance->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    }
    instance->setPlayConfigDetails(2, 2, sampleRate, maxBlock);
    instance->prepareToPlay(sampleRate, maxBlock);

    auto* raw = new PluginProcessor(std::move(instance), maxBlock);
    raw->latency_ = std::max(0, raw->instance_->getLatencySamples());
    return std::shared_ptr<PluginProcessor>(raw, [](PluginProcessor* p) {
        auto* manager = juce::MessageManager::getInstanceWithoutCreating();
        if (!manager || manager->isThisTheMessageThread()) delete p;
        else juce::MessageManager::callAsync([p] { delete p; });
    });
}

void PluginProcessor::process(float* l, float* r, int frames) noexcept {
    juce::ScopedNoDenormals noDenormals;
    for (int done = 0; done < frames; done += maxBlock_) {
        const int n = std::min(maxBlock_, frames - done);
        float* channels[2] = {l + done, r + done};
        juce::AudioBuffer<float> buffer(channels, 2, n);  // wraps the caller's memory: no allocation
        midi_.clear();
        instance_->processBlock(buffer, midi_);
    }
}

std::string PluginProcessor::encodeState(const juce::MemoryBlock& block) {
    return juce::Base64::toBase64(block.getData(), block.getSize()).toStdString();
}

bool PluginProcessor::decodeState(const std::string& base64, juce::MemoryBlock& out) {
    juce::MemoryOutputStream stream(out, false);
    return juce::Base64::convertFromBase64(stream, juce::String(base64));
}

std::string PluginProcessor::captureState() {
    juce::MemoryBlock block;
    instance_->getStateInformation(block);
    return encodeState(block);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return instance_->hasEditor() ? instance_->createEditor() : nullptr; }

}  // namespace lpc
