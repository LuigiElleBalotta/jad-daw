#pragma once
#include <deque>
#include <string>
#include <vector>

namespace lpc {

// What the input of a recording looked like over a few seconds: the readings are the peak of each short interval (the meter's), so the peak, the usual loud
// level and the floor between them can be told apart. Used by the recording assistant (input level check, gain staging).
struct InputSummary {
    double seconds = 0.0;           // how much time the readings cover
    double peakDb = -200.0;         // the highest reading
    double loudDb = -200.0;         // the level most loud readings reach (95th percentile): where the performance usually peaks
    double noiseFloorDb = -200.0;   // the level between the loud parts (10th percentile of the readings above digital silence; -200: no signal at all)
    int clippedReadings = 0;        // readings at full scale (>= 0.99)
    bool signal = false;            // something above -70 dBFS was heard
};

class InputLevelStats {
public:
    explicit InputLevelStats(std::size_t capacity = 6000) : capacity_(capacity) {}
    // One reading: the linear peak since the previous one, covering `seconds`.
    void push(float peak, double seconds);
    void reset() { peaks_.clear(); seconds_ = 0.0; }
    InputSummary summary() const;

private:
    std::size_t capacity_;
    std::deque<float> peaks_;
    double seconds_ = 0.0;
    double secondsPerReading_ = 0.0;
};

struct InputAdvice {
    double gainChangeDb = 0.0;          // what to change at the preamp or the input gain to reach the target (negative: turn down); 0 when it is fine
    std::vector<std::string> messages;  // plain language, most important first
};

// Gain staging for 24-bit tracking: peaks between about -12 and -6 dBFS leave headroom and keep the noise of the chain far below the signal.
InputAdvice adviseInput(const InputSummary& s);

}  // namespace lpc
