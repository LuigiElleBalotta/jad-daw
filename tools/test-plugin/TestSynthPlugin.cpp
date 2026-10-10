#include <array>
#include <cmath>

#include <juce_audio_processors/juce_audio_processors.h>

namespace {

// A VST3 instrument for the host tests: every held note is a sine of its pitch whose level follows the velocity (no envelope, so the
// offsets of the note events can be read straight from the output). It has no audio input and a stereo output.
class TestSynthProcessor final : public juce::AudioProcessor {
public:
    TestSynthProcessor() : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    const juce::String getName() const override { return "LPC Test Synth"; }
    void prepareToPlay(double sampleRate, int) override {
        rate_ = sampleRate;
        releaseResources();
    }
    void releaseResources() override {
        for (auto& v : voices_) v = {};
    }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        return layouts.getMainInputChannelSet().isDisabled() &&
               (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono());
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        buffer.clear();
        auto it = midi.begin();
        for (int i = 0; i < n; ++i) {
            while (it != midi.end() && (*it).samplePosition <= i) {
                const juce::MidiMessage m = (*it).getMessage();
                if (m.isNoteOn()) voices_[static_cast<size_t>(m.getNoteNumber())] = {true, m.getFloatVelocity(), 0.0};
                else if (m.isNoteOff()) voices_[static_cast<size_t>(m.getNoteNumber())].on = false;
                else if (m.isController() && m.getControllerNumber() == 64) lastSustain_ = m.getControllerValue();
                ++it;
            }
            float sample = 0.0f;
            for (size_t note = 0; note < voices_.size(); ++note) {
                Voice& v = voices_[note];
                if (!v.on) continue;
                const double hz = 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
                sample += 0.2f * v.level * static_cast<float>(std::sin(v.phase));
                v.phase += 2.0 * juce::MathConstants<double>::pi * hz / rate_;
            }
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.setSample(ch, i, sample);
        }
        midi.clear();
    }

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override { dest.replaceAll("synth", 5); }
    void setStateInformation(const void*, int) override {}

private:
    struct Voice {
        bool on = false;
        float level = 0.0f;
        double phase = 0.0;
    };
    std::array<Voice, 128> voices_{};
    double rate_ = 48000.0;
    int lastSustain_ = 0;
};

}  // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestSynthProcessor(); }
