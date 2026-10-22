#pragma once
// Live loudness, true peak and stereo correlation of a stream, for the meters of the Mastering Assistant window: the UI thread feeds it what the spectrum tap
// collected (the audio thread never runs it). Loudness is BS.1770: K-weighting, 400 ms momentary and 3 s short-term windows hopping 100 ms, gated integration.
#include <array>
#include <cmath>
#include <cstddef>
#include <deque>
#include <vector>

#include "lpc/loudness.h"

namespace lpc::audio {

class LiveMeter {
public:
    explicit LiveMeter(double sampleRate);
    void reset();
    void process(const float* l, const float* r, std::size_t frames);

    double momentary() const;    // LUFS, -200 before 400 ms have come in
    double shortTerm() const;    // LUFS, -200 before 3 s have come in
    double integrated() const;   // LUFS-I over everything since reset()
    double truePeakDb() const { return truePeak_ > 0.0 ? 20.0 * std::log10(truePeak_) : -200.0; }
    double correlation() const;  // -1 (opposite) .. 1 (identical), over the last few hundred milliseconds; 0 for silence

private:
    double energyOf(std::size_t hops) const;
    double rate_;
    std::size_t hop_;
    KWeighting kl_, kr_;
    double acc_ = 0.0;
    std::size_t inHop_ = 0;
    std::deque<double> hops_;          // the last 30 hops: mean square of the K-weighted channels summed
    std::vector<double> blocks_;       // every 400 ms window since reset, for the integrated figure
    double hist_[2][4] = {};           // the last four raw samples per channel, for the true peak
    double truePeak_ = 0.0;
    double sumLR_ = 0.0, sumLL_ = 0.0, sumRR_ = 0.0, decay_;
};

}  // namespace lpc::audio
