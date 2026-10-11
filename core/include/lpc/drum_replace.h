#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "lpc/wav.h"

namespace lpc {

// Drum Replacement: the hits of a drum recording are found and a drum sound is put on each one, instead of the original (Replace) or with it (Double).
struct DrumHit {
    std::int64_t frame = 0;  // where the hit begins
    double level = 1.0;      // its peak relative to the loud hits of the recording (about 0..1.5)
};

enum class DrumSound { Kick, Snare, Tom, HiHat, Clap };
DrumSound drumSoundFromName(const std::string& name);  // "kick", "snare", "tom", "hihat", "clap"; unknown names give a snare

// The built-in sounds are made here (no sample files are shipped): a mono sound at `rate`, the hit beginning at frame 0.
WavData synthDrum(DrumSound sound, int rate);

struct DrumReplaceParams {
    bool replace = true;      // false: Double, the original stays under the new sound
    double mix = 1.0;         // Replace: how much of the original is taken away at each hit (1: all of it)
    double levelDb = 0.0;     // of the new sound
    double velocity = 0.7;    // 0: every hit as loud as the sound; 1: as loud as the original hit was (relative to the loud ones)
    double sensitivity = 0.5; // for finding the hits
    double minGapMs = 60.0;   // hits closer than this are one
    double duckMs = 90.0;     // Replace: how long after a hit the original stays reduced
};

std::vector<DrumHit> findDrumHits(const WavData& audio, double sensitivity, double minGapMs);
// `sample` is mono or has the channels of `audio`; the result has the length and channels of `audio`.
WavData replaceDrums(const WavData& audio, const std::vector<DrumHit>& hits, const WavData& sample, const DrumReplaceParams& params);

}  // namespace lpc
