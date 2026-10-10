#include "plugin_processor.h"

#include <algorithm>

namespace lpc {

PluginProcessor::PluginProcessor(std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlock)
    : instance_(std::move(instance)), maxBlock_(maxBlock), name_(instance_->getName().toStdString()) {}

PluginProcessor::~PluginProcessor() { instance_->releaseResources(); }

std::shared_ptr<PluginProcessor> PluginProcessor::create(std::unique_ptr<juce::AudioPluginInstance> instance, const std::string& stateBase64,
                                                         double sampleRate, int maxBlock, bool instrument) {
    if (!instance) return nullptr;
    juce::AudioProcessor::BusesLayout layout;
    if (instrument) {
        // An instrument needs no audio input (a bus it has stays closed) and sounds on its first output bus, in stereo or in mono.
        for (int i = 0; i < instance->getBusCount(true); ++i) layout.inputBuses.add(juce::AudioChannelSet::disabled());
        bool ok = false;
        for (const juce::AudioChannelSet& main : {juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono()}) {
            layout.outputBuses.clear();
            for (int i = 0; i < instance->getBusCount(false); ++i) layout.outputBuses.add(i == 0 ? main : juce::AudioChannelSet::disabled());
            if (instance->getBusCount(false) >= 1 && instance->setBusesLayout(layout)) {
                ok = true;
                break;
            }
        }
        if (!ok) {  // some instruments keep a stereo input bus open: leave the inputs as they are and try once more
            layout.inputBuses.clear();
            layout.outputBuses.clear();
            for (int i = 0; i < instance->getBusCount(true); ++i)
                layout.inputBuses.add(i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
            for (int i = 0; i < instance->getBusCount(false); ++i)
                layout.outputBuses.add(i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
            if (instance->getBusCount(false) < 1 || !instance->setBusesLayout(layout)) return nullptr;
        }
    } else {
        for (int i = 0; i < instance->getBusCount(true); ++i)
            layout.inputBuses.add(i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
        for (int i = 0; i < instance->getBusCount(false); ++i)
            layout.outputBuses.add(i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
        if (instance->getBusCount(true) < 1 || instance->getBusCount(false) < 1 || !instance->setBusesLayout(layout)) return nullptr;
    }

    if (!stateBase64.empty()) {
        juce::MemoryBlock block;
        if (decodeState(stateBase64, block) && block.getSize() > 0)
            instance->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    }
    const int ins = instance->getTotalNumInputChannels(), outs = instance->getTotalNumOutputChannels();
    instance->setPlayConfigDetails(instrument ? ins : 2, instrument ? outs : 2, sampleRate, maxBlock);
    instance->prepareToPlay(sampleRate, maxBlock);

    auto* raw = new PluginProcessor(std::move(instance), maxBlock);
    raw->instrument_ = instrument;
    if (instrument) {
        raw->work_.setSize(std::max(1, std::max(raw->instance_->getTotalNumInputChannels(), raw->instance_->getTotalNumOutputChannels())), maxBlock);
        raw->midi_.ensureSize(8192);
    }
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

void PluginProcessor::render(float* l, float* r, int frames, const audio::MidiEvent* events, int count) noexcept {
    juce::ScopedNoDenormals noDenormals;
    int next = 0;
    for (int done = 0; done < frames; done += maxBlock_) {
        const int n = std::min(maxBlock_, frames - done);
        work_.clear();
        midi_.clear();
        for (; next < count && events[next].offset < done + n; ++next) {
            const audio::MidiEvent& e = events[next];
            const juce::uint8 bytes[3] = {e.status, e.data1, e.data2};
            const int size = (e.status & 0xf0) == 0xc0 || (e.status & 0xf0) == 0xd0 ? 2 : 3;
            midi_.addEvent(bytes, size, std::max(0, e.offset - done));
        }
        juce::AudioBuffer<float> view(work_.getArrayOfWritePointers(), work_.getNumChannels(), n);
        instance_->processBlock(view, midi_);
        const float* outL = work_.getReadPointer(0);
        const float* outR = work_.getNumChannels() > 1 ? work_.getReadPointer(1) : outL;
        std::copy(outL, outL + n, l + done);
        std::copy(outR, outR + n, r + done);
    }
}

void PluginProcessor::setParameter(int index, float normalized) noexcept {
    const auto& params = instance_->getParameters();
    if (index >= 0 && index < params.size()) params[index]->setValue(juce::jlimit(0.0f, 1.0f, normalized));
}

std::vector<std::pair<int, std::string>> PluginProcessor::automatableParameters() const {
    std::vector<std::pair<int, std::string>> out;
    const auto& params = instance_->getParameters();
    for (int i = 0; i < params.size() && out.size() < 1024; ++i)
        if (params[i]->isAutomatable() && !params[i]->isMetaParameter()) out.emplace_back(i, params[i]->getName(64).toStdString());
    return out;
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
