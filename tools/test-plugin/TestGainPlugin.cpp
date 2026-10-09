#include <array>
#include <cstring>

#include <juce_audio_processors/juce_audio_processors.h>

namespace {

constexpr int kLatency = 32;

class TestGainProcessor final : public juce::AudioProcessor {
public:
    TestGainProcessor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        addParameter(gain_ = new juce::AudioParameterFloat(juce::ParameterID{"gain", 1}, "Gain",
                                                           juce::NormalisableRange<float>(0.0f, 2.0f), 1.0f));
        setLatencySamples(kLatency);
    }

    const juce::String getName() const override { return "LPC Test Gain"; }
    void prepareToPlay(double, int) override { releaseResources(); }
    void releaseResources() override {
        for (auto& d : delay_) d.fill(0.0f);
        pos_ = 0;
    }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
               layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        const float g = gain_->get();
        const int n = buffer.getNumSamples();
        for (int ch = 0; ch < 2 && ch < buffer.getNumChannels(); ++ch) {
            float* data = buffer.getWritePointer(ch);
            int p = pos_;
            for (int i = 0; i < n; ++i) {
                const float in = data[i];
                data[i] = delay_[static_cast<size_t>(ch)][static_cast<size_t>(p)] * g;
                delay_[static_cast<size_t>(ch)][static_cast<size_t>(p)] = in;
                p = (p + 1) % kLatency;
            }
        }
        pos_ = (pos_ + n) % kLatency;
    }

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override {
        const float g = gain_->get();
        dest.setSize(sizeof(float));
        std::memcpy(dest.getData(), &g, sizeof(float));
    }
    void setStateInformation(const void* data, int size) override {
        if (size != static_cast<int>(sizeof(float))) return;
        float g = 1.0f;
        std::memcpy(&g, data, sizeof(float));
        *gain_ = g;
    }

private:
    juce::AudioParameterFloat* gain_ = nullptr;
    std::array<std::array<float, kLatency>, 2> delay_{};
    int pos_ = 0;
};

}  // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestGainProcessor(); }
