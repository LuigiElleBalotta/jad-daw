#include "lpc/effect_specs.h"

#include <string>

#include "lpc/match_eq.h"
#include "lpc/processor_ids.h"

namespace lpc {

namespace {

EffectParam p(const char* name, const char* label, const char* unit, double min, double max, double def, bool log = false) {
    return EffectParam{name, label, unit, min, max, def, log};
}

// The Channel EQ of Logic Pro 11.2: low cut, low shelf, four peaks, high shelf, high cut. Defaults are Logic's own (#default.pst).
std::vector<EffectParam> channelEqParams() {
    std::vector<EffectParam> v;
    struct Band {
        const char* key;
        const char* label;
        double freq, q;
        bool cut;
    };
    static const Band bands[] = {{"lc", "Low Cut", 20, 0.71, true},    {"ls", "Low Shelf", 75, 1.0, false}, {"p1", "Peak 1", 100, 0.6, false},
                                 {"p2", "Peak 2", 250, 0.3, false},     {"p3", "Peak 3", 1040, 0.41, false}, {"p4", "Peak 4", 2500, 0.2, false},
                                 {"hs", "High Shelf", 7500, 1.0, false}, {"hc", "High Cut", 20000, 0.71, true}};
    for (const Band& b : bands) {
        const std::string k = b.key, l = b.label;
        v.push_back(p((k + "On").c_str(), (l + " On").c_str(), "", 0, 1, (k == "lc" || k == "hc") ? 0 : 1));
        v.push_back(p((k + "Freq").c_str(), (l + " Frequency").c_str(), "Hz", 20, 20000, b.freq, true));
        if (b.cut) v.push_back(p((k + "Slope").c_str(), (l + " Slope").c_str(), "x6 dB/oct", 1, 8, k == "lc" ? 2 : 4));
        else v.push_back(p((k + "Gain").c_str(), (l + " Gain").c_str(), "dB", -24, 24, 0));
        v.push_back(p((k + "Q").c_str(), (l + " Q").c_str(), "", 0.1, b.cut ? 12 : 20, b.q, true));
    }
    v.push_back(p("masterGain", "Master Gain", "dB", -24, 24, 0));
    v.push_back(p("processing", "Processing (0 stereo, 1 left, 2 right, 3 mid, 4 side)", "", 0, 4, 0));
    v.push_back(p("hq", "HQ (the filters run at twice the sample rate)", "", 0, 1, 0));
    return v;
}

std::vector<EffectSpec> build() {
    std::vector<EffectSpec> v;
    v.push_back({kProcGain, "Gain", "Utility", {p("gainDb", "Gain", "dB", -96, 24, 0)}});
    v.push_back({kProcEq, "Channel EQ", "EQ", channelEqParams()});
    // Compressor: the circuit values are the ones stored in Logic's presets (0 Platinum Digital, 1 Class A R, 2 Class A U, 3 Vintage VCA,
    // 4 Studio VCA, 5 Studio FET, 6 Vintage Opto, 7 Vintage FET, 8 Classic VCA); the defaults are Logic's #default.pst.
    v.push_back({kProcCompressor, "Compressor", "Dynamics",
                 {p("circuit", "Circuit Type", "", 0, 8, 0), p("threshold", "Threshold", "dB", -60, 0, -20), p("ratio", "Ratio", ":1", 1, 100, 2.1, true),
                  p("attack", "Attack", "ms", 0, 250, 15), p("release", "Release", "ms", 5, 5000, 51, true), p("autoRelease", "Auto Release", "", 0, 1, 1),
                  p("gain", "Gain", "dB", -24, 24, 0), p("autoGain", "Auto Gain (0 off, 1 from 0 dB, 2 from -12 dB)", "", 0, 2, 2), p("knee", "Knee", "", 0, 1, 0.7),
                  p("peakRms", "Detection (0 peak, 1 RMS)", "", 0, 1, 1), p("outputDistortion", "Output Distortion (0 off, 1 soft, 2 hard, 3 clip)", "", 0, 3, 0),
                  p("limiter", "Limiter", "", 0, 1, 0), p("limiterThreshold", "Limiter Threshold", "dB", -12, 0, 0), p("scState", "Side Chain Filter (0 off, 1 listen, 2 on)", "", 0, 2, 0), p("scMode", "Side Chain Mode (0 LP, 1 BP, 2 HP, 3 Par EQ, 4 Hi Shelf)", "", 0, 4, 2),
                  p("scFreq", "Side Chain Frequency", "Hz", 16.35, 19912, 1000, true), p("scQ", "Side Chain Q", "", 0.5, 5, 0.71, true), p("scGain", "Side Chain Gain", "dB", -24, 24, 0), p("mix", "Mix", "%", 0, 100, 100),
                  p("inputGain", "Input Gain", "dB", -30, 30, 0), p("outputGain", "Output Gain", "dB", -30, 30, 0)}});
    v.push_back({kProcLimiter, "Limiter", "Dynamics",
                 {p("gain", "Gain", "dB", -20, 20, 0), p("lookahead", "Lookahead", "ms", 0, 10, 5), p("softKnee", "Soft Knee", "", 0, 1, 1),
                  p("release", "Release", "ms", 2, 2000, 250, true), p("ceiling", "Output Level", "dB", -10, 1, 0), p("truePeak", "True Peak Detection", "", 0, 1, 1)}});
    v.push_back({kProcAdaptiveLimiter, "Adaptive Limiter", "Dynamics",
                 {p("inputScale", "Input Scale", "dB", -12, 12, 0), p("gain", "Gain", "dB", 0, 12, 3), p("ceiling", "Out Ceiling", "dB", -2, 0, 0),
                  p("mode", "Mode (0 low, 1 average, 2 high)", "", 0, 2, 1), p("lookahead", "Lookahead", "ms", 0, 100, 50), p("removeDc", "Remove DC Offset", "", 0, 1, 1)}});
    v.push_back({kProcGate, "Noise Gate", "Dynamics",
                 {p("threshold", "Threshold", "dB", -100, 0, -50), p("hysteresis", "Hysteresis", "dB", -20, 0, -3), p("range", "Reduction", "dB", -100, 0, -100),
                  p("attack", "Attack", "ms", 0, 500, 3), p("hold", "Hold", "ms", 0, 5000, 40), p("release", "Release", "ms", 1, 5000, 10, true),
                  p("lookahead", "Lookahead", "ms", 0, 10, 0), p("keyFilter", "Side Chain Filter", "", 0, 1, 0), p("keyLowCut", "Low Cut", "Hz", 20, 20000, 20, true),
                  p("keyHighCut", "High Cut", "Hz", 20, 20000, 20000, true), p("ducker", "Ducker (0 gate, 1 ducker)", "", 0, 1, 0),
                  p("keyMode", "Characteristics (0 bandpass, 1 lowpass, 2 highpass)", "", 0, 2, 0), p("keyMonitor", "Monitor", "", 0, 1, 0)}});
    v.push_back({kProcDelay, "Delay", "Delay & Reverb",
                 {p("time", "Time", "ms", 1, 2000, 250, true), p("feedback", "Feedback", "%", 0, 95, 35), p("mix", "Mix", "%", 0, 100, 30),
                  p("pingPong", "Ping-Pong", "", 0, 1, 0)}});
    v.push_back({kProcReverb, "Reverb", "Delay & Reverb",
                 {p("size", "Room Size", "%", 0, 100, 50), p("damping", "Damping", "%", 0, 100, 50), p("width", "Width", "%", 0, 100, 100),
                  p("predelay", "Pre-Delay", "ms", 0, 200, 0), p("mix", "Mix", "%", 0, 100, 25)}});
    // Match EQ: a correction curve on third-octave bands (the Learn action fills b0..b30 from a reference curve and the signal's own spectrum)
    {
        std::vector<EffectParam> m{p("amount", "Amount", "%", 0, 200, 100), p("fadeLow", "Fade Low Extremes", "Hz", 20, 500, 40, true),
                                   p("fadeHigh", "Fade High Extremes", "Hz", 2000, 20000, 12000, true)};
        for (int i = 0; i < kMatchEqBands; ++i)
            m.push_back(p(("b" + std::to_string(i)).c_str(), ("Band " + std::to_string(i + 1) + " Correction").c_str(), "dB", -24, 24, 0));
        v.push_back({kProcMatchEq, "Match EQ", "EQ", std::move(m)});
    }
    // Saturation with Logic's own neural audio models (CMLSaturation: ten families, `drive` is the `cond` input of the network). It needs onnxruntime and the converted
    // models; without them the insert passes the sound through.
    v.push_back({kProcMlSaturation, "Neural Saturation", "Distortion",
                 {p("type", "Model (0-9: RTRST RTRHT MTC MTM FEO FTO FS FSQ MKBO MKGerm)", "", 0, 9, 4), p("drive", "Drive", "%", 0, 100, 50), p("output", "Output", "dB", -24, 12, 0),
                  p("mix", "Mix", "%", 0, 100, 100)}});
    // Mastering Assistant: the parameters eq1..eq8 and gain are what the analysis found (the Reanalyze button writes them)
    {
        std::vector<EffectParam> m{p("loudness", "Loudness", "%", -100, 100, 0), p("autoEq", "Auto EQ", "%", 0, 150, 100), p("excite", "Excite", "", 0, 1, 0),
                                   p("character", "Character (0-3)", "", 0, 3, 0), p("bypass", "Bypass", "", 0, 1, 0),
                                   p("compensate", "Loudness Compensation", "", 0, 1, 0), p("gain", "Analysed Gain", "dB", -24, 36, 0), p("compTrim", "Compensation Trim", "dB", -12, 12, 0)};
        for (int i = 1; i <= 8; ++i) m.push_back(p(("eq" + std::to_string(i)).c_str(), ("Analysed EQ " + std::to_string(i)).c_str(), "dB", -12, 12, 0));
        v.push_back({kProcMastering, "Mastering Assistant", "Mastering", std::move(m)});
    }
    return v;
}

std::vector<EffectSpec> buildInstruments() {
    std::vector<EffectSpec> v;
    v.push_back({kProcSine, "Sine", "Instruments", {}});
    v.push_back({kProcExternalMidi, "External MIDI", "Instruments", {p("channel", "MIDI Channel", "", 1, 16, 1)}});
    v.push_back({kProcDrums, "Drum Kit", "Instruments", {p("level", "Level", "dB", -24, 6, -6)}});
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
