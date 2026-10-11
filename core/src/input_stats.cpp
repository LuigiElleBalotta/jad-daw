#include "lpc/input_stats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lpc {

namespace {
double toDb(double linear) { return linear > 1e-10 ? 20.0 * std::log10(linear) : -200.0; }
double round1(double v) { return std::round(v * 10.0) / 10.0; }
}  // namespace

void InputLevelStats::push(float peak, double seconds) {
    if (!(seconds > 0.0)) return;
    peaks_.push_back(std::clamp(peak, 0.0f, 16.0f));
    seconds_ += seconds;
    secondsPerReading_ = seconds_ / static_cast<double>(peaks_.size());
    while (peaks_.size() > capacity_) {
        peaks_.pop_front();
        seconds_ -= secondsPerReading_;
    }
}

InputSummary InputLevelStats::summary() const {
    InputSummary s;
    if (peaks_.empty()) return s;
    s.seconds = seconds_;
    std::vector<float> sorted(peaks_.begin(), peaks_.end());
    std::sort(sorted.begin(), sorted.end());
    s.peakDb = round1(toDb(sorted.back()));
    for (float p : sorted)
        if (p >= 0.99f) ++s.clippedReadings;
    s.signal = s.peakDb > -70.0;
    if (!s.signal) return s;
    s.loudDb = round1(toDb(sorted[std::min(sorted.size() - 1, static_cast<std::size_t>(static_cast<double>(sorted.size()) * 0.95))]));
    std::vector<float> above;
    for (float p : sorted)
        if (toDb(p) > -100.0) above.push_back(p);
    if (!above.empty()) s.noiseFloorDb = round1(toDb(above[above.size() / 10]));
    return s;
}

InputAdvice adviseInput(const InputSummary& s) {
    InputAdvice a;
    char buf[260];
    auto say = [&](const char* text) { a.messages.emplace_back(text); };
    if (s.seconds <= 0.0) {
        say("No readings yet: arm the track, then play or sing for a few seconds.");
        return a;
    }
    if (!s.signal) {
        say("No signal at the input (nothing above -70 dBFS): check the cable, the input selected on the track, the preamp gain and phantom power for a condenser.");
        return a;
    }
    constexpr double kTarget = -10.0;  // dBFS peak
    if (s.clippedReadings > 0) {
        std::snprintf(buf, sizeof buf, "The input clipped (%d readings at full scale): turn the gain down by at least %.0f dB and play the loudest part again; a clipped take cannot be repaired.", s.clippedReadings,
                      std::max(6.0, std::ceil(s.peakDb - kTarget)));
        a.messages.emplace_back(buf);
        a.gainChangeDb = -std::max(6.0, std::ceil(s.peakDb - kTarget));
    } else if (s.peakDb > -3.0) {
        std::snprintf(buf, sizeof buf, "The peak is %.1f dBFS: too close to full scale, one louder note will clip; turn the gain down by about %.0f dB.", s.peakDb, std::ceil(s.peakDb - kTarget));
        a.messages.emplace_back(buf);
        a.gainChangeDb = -std::ceil(s.peakDb - kTarget);
    } else if (s.peakDb < -24.0) {
        std::snprintf(buf, sizeof buf, "The level is low (peak %.1f dBFS): turn the gain up by about %.0f dB so the peaks reach about -10 dBFS; the converter's noise matters less the closer the signal is to full scale.", s.peakDb,
                      std::floor(kTarget - s.peakDb));
        a.messages.emplace_back(buf);
        a.gainChangeDb = std::floor(kTarget - s.peakDb);
    } else if (s.peakDb < -14.0) {
        std::snprintf(buf, sizeof buf, "A little low (peak %.1f dBFS): up to %.0f dB more gain is fine; peaks around -12 to -6 dBFS are a good target.", s.peakDb, std::floor(kTarget - s.peakDb));
        a.messages.emplace_back(buf);
        a.gainChangeDb = std::floor(kTarget - s.peakDb);
    } else {
        std::snprintf(buf, sizeof buf, "The level is good (peak %.1f dBFS, usually %.1f): there is headroom and the signal is far from the noise.", s.peakDb, s.loudDb);
        a.messages.emplace_back(buf);
    }
    if (s.noiseFloorDb > -200.0) {
        const double snr = s.loudDb - s.noiseFloorDb;
        if (s.noiseFloorDb > -50.0 && snr < 30.0 && s.seconds > 2.0) {
            std::snprintf(buf, sizeof buf, "The floor between the loud parts is high (%.0f dBFS, %.0f dB under the signal): hiss, hum or room noise; check the cables and grounding, move the microphone or turn the source up instead of the preamp.", s.noiseFloorDb, snr);
            a.messages.emplace_back(buf);
        }
    }
    return a;
}

}  // namespace lpc
