#include "lpc/effect_specs.h"

#include "lpc/processor_ids.h"

namespace lpc {

namespace {

EffectParam p(const char* name, const char* label, const char* unit, double min, double max, double def, bool log = false) {
    return EffectParam{name, label, unit, min, max, def, log};
}

std::vector<EffectSpec> build() {
    std::vector<EffectSpec> v;
    v.push_back({kProcGain, "Gain", "Utility", {p("gainDb", "Gain", "dB", -96, 24, 0)}});
    v.push_back({kProcEq, "Channel EQ", "EQ",
                 {p("lowFreq", "Low Shelf Freq", "Hz", 20, 1000, 100, true), p("lowGain", "Low Shelf Gain", "dB", -18, 18, 0),
                  p("m1Freq", "Band 1 Freq", "Hz", 60, 8000, 300, true), p("m1Gain", "Band 1 Gain", "dB", -18, 18, 0), p("m1Q", "Band 1 Q", "", 0.2, 10, 1, true),
                  p("m2Freq", "Band 2 Freq", "Hz", 200, 12000, 1500, true), p("m2Gain", "Band 2 Gain", "dB", -18, 18, 0), p("m2Q", "Band 2 Q", "", 0.2, 10, 1, true),
                  p("m3Freq", "Band 3 Freq", "Hz", 1000, 16000, 5000, true), p("m3Gain", "Band 3 Gain", "dB", -18, 18, 0), p("m3Q", "Band 3 Q", "", 0.2, 10, 1, true),
                  p("highFreq", "High Shelf Freq", "Hz", 1000, 20000, 8000, true), p("highGain", "High Shelf Gain", "dB", -18, 18, 0),
                  p("outGain", "Output Gain", "dB", -24, 24, 0)}});
    v.push_back({kProcCompressor, "Compressor", "Dynamics",
                 {p("threshold", "Threshold", "dB", -60, 0, -18), p("ratio", "Ratio", ":1", 1, 20, 3, true), p("attack", "Attack", "ms", 0.1, 200, 10, true),
                  p("release", "Release", "ms", 10, 2000, 100, true), p("knee", "Knee", "dB", 0, 24, 6), p("makeup", "Make-up Gain", "dB", 0, 24, 0),
                  p("mix", "Mix", "%", 0, 100, 100)}});
    v.push_back({kProcLimiter, "Limiter", "Dynamics",
                 {p("gain", "Input Gain", "dB", 0, 24, 0), p("ceiling", "Ceiling", "dB", -12, 0, -0.3), p("release", "Release", "ms", 10, 500, 100, true)}});
    v.push_back({kProcGate, "Noise Gate", "Dynamics",
                 {p("threshold", "Threshold", "dB", -80, 0, -50), p("range", "Reduction", "dB", -80, 0, -80), p("attack", "Attack", "ms", 0.1, 50, 1, true),
                  p("hold", "Hold", "ms", 0, 500, 20), p("release", "Release", "ms", 5, 2000, 100, true)}});
    v.push_back({kProcDelay, "Delay", "Delay & Reverb",
                 {p("time", "Time", "ms", 1, 2000, 250, true), p("feedback", "Feedback", "%", 0, 95, 35), p("mix", "Mix", "%", 0, 100, 30),
                  p("pingPong", "Ping-Pong", "", 0, 1, 0)}});
    v.push_back({kProcReverb, "Reverb", "Delay & Reverb",
                 {p("size", "Room Size", "%", 0, 100, 50), p("damping", "Damping", "%", 0, 100, 50), p("width", "Width", "%", 0, 100, 100),
                  p("predelay", "Pre-Delay", "ms", 0, 200, 0), p("mix", "Mix", "%", 0, 100, 25)}});
    return v;
}

std::vector<EffectSpec> buildInstruments() {
    std::vector<EffectSpec> v;
    v.push_back({kProcSine, "Sine", "Instruments", {}});
    v.push_back({kProcSynth, "Synth", "Instruments",
                 {p("wave", "Waveform (0 sine, 1 triangle, 2 saw, 3 square)", "", 0, 3, 2), p("attack", "Attack", "ms", 0.5, 2000, 5, true),
                  p("decay", "Decay", "ms", 1, 5000, 200, true), p("sustain", "Sustain", "%", 0, 100, 70), p("release", "Release", "ms", 1, 5000, 150, true),
                  p("cutoff", "Filter Cutoff", "Hz", 100, 20000, 12000, true), p("level", "Level", "dB", -24, 6, -12)}});
    return v;
}

}  // namespace

const std::vector<EffectSpec>& instrumentSpecs() {
    static const std::vector<EffectSpec> specs = buildInstruments();
    return specs;
}

const EffectSpec* findInstrumentSpec(std::string_view id) {
    for (const EffectSpec& s : instrumentSpecs())
        if (s.id == id) return &s;
    return nullptr;
}

bool isKnownInstrument(std::string_view id) { return findInstrumentSpec(id) != nullptr; }

const std::vector<EffectSpec>& effectSpecs() {
    static const std::vector<EffectSpec> specs = build();
    return specs;
}

const EffectSpec* findEffectSpec(std::string_view id) {
    for (const EffectSpec& s : effectSpecs())
        if (s.id == id) return &s;
    return nullptr;
}

bool isKnownEffect(std::string_view id) { return findEffectSpec(id) != nullptr; }

}  // namespace lpc
