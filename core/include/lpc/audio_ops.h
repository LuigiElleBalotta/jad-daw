#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "lpc/wav.h"

namespace lpc {

// Offline operations on audio (interleaved floats). They never touch a file: the caller reads a region's part of a file into a WavData,
// applies one, and writes the result as a new file.

// The largest absolute sample, 0 for no audio.
float peakOf(const WavData& d);

// Scales so that the peak reaches `targetDb` dBFS; silence stays silence. Returns the gain applied in dB.
double normalize(WavData& d, double targetDb = -0.3);

void applyGain(WavData& d, double gainDb);
void reverse(WavData& d);
// A quarter-sine ramp over the first (in = true) or last `frames` frames.
void fade(WavData& d, bool in, std::int64_t frames);
// Zeroes frames [from, to).
void silence(WavData& d, std::int64_t from, std::int64_t to);

// The same sound `ratio` times as long (2 = twice as long, half as fast) at the same pitch (WSOLA: overlapped windows joined where they
// match best). ratio is kept between 0.25 and 4.
WavData timeStretch(const WavData& d, double ratio);

// The same length at another pitch: stretched by the pitch ratio, then played back at the matching speed. semitones in [-24, 24].
WavData pitchShift(const WavData& d, double semitones);

// Where the sound is: [start, end) frame ranges whose level stays above `thresholdDb` (measured over 10 ms), gaps shorter than
// `minSilenceMs` do not split a sound, sounds shorter than `minSoundMs` are dropped, and every range is widened by `padMs` on both sides.
std::vector<std::pair<std::int64_t, std::int64_t>> findSounds(const WavData& d, double thresholdDb, double minSilenceMs = 100.0, double minSoundMs = 20.0, double padMs = 10.0);

}  // namespace lpc
