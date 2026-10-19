#pragma once
#include <vector>

namespace lpc {

// Match EQ works on third-octave bands: 31 centres from 20 Hz to 20 kHz (1 kHz * 10^((i - 17) / 10)). In Logic Pro 11.2 the correction is a
// parametric biquad bank driven by a target curve (CMatchEQFX, docs/logic-reference/mixer.md); here the bank is one peaking filter per band.
inline constexpr int kMatchEqBands = 31;
double matchEqBandFreq(int band);

// A curve given on an arbitrary frequency axis (freqHz ascending, magDb the level) averaged in each third-octave band (in the power domain).
std::vector<double> matchEqBands(const std::vector<double>& freqHz, const std::vector<double>& magDb);

// The correction that makes `source` sound like `target` (both 31 band levels in dB): the difference of the two curves with their mean level
// taken out, scaled by `amount` (1 = full), faded to nothing below `fadeLowHz` and above `fadeHighHz` (Logic's Fade Extremes: the correction
// must not chase the reference where it has no real content) and limited to +-maxDb.
std::vector<double> matchEqCorrection(const std::vector<double>& source, const std::vector<double>& target, double amount = 1.0, double fadeLowHz = 40.0,
                                      double fadeHighHz = 12000.0, double maxDb = 18.0);

}  // namespace lpc
