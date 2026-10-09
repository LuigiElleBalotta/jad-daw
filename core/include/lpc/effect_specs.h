#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace lpc {

// One parameter of a built-in effect: what the UI shows and what a command may set.
struct EffectParam {
    std::string name;   // the key in ProcessorRef::params
    std::string label;
    std::string unit;   // "dB", "Hz", "ms", "%", "" ...
    double min = 0, max = 1, def = 0;
    bool logarithmic = false;  // a slider moves it by ratio (frequencies, times)
};

struct EffectSpec {
    std::string id;     // "builtin.eq"
    std::string name;   // "Channel EQ"
    std::string group;  // the menu it is listed under: "EQ", "Dynamics", "Delay & Reverb", "Utility"
    std::vector<EffectParam> params;
    const EffectParam* find(std::string_view name) const {
        for (const EffectParam& p : params)
            if (p.name == name) return &p;
        return nullptr;
    }
};

// Every built-in effect (the gain, the channel EQ, compressor, limiter, gate, delay and reverb).
const std::vector<EffectSpec>& effectSpecs();
const EffectSpec* findEffectSpec(std::string_view id);

// The built-in instruments: "builtin.sine" (no parameters) and "builtin.synth". Their parameters are edited like an effect's.
const std::vector<EffectSpec>& instrumentSpecs();
const EffectSpec* findInstrumentSpec(std::string_view id);

}  // namespace lpc
