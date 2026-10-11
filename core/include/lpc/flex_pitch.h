#pragma once
#include <cstdint>
#include <vector>

#include "lpc/wav.h"

namespace lpc {

// Flex Pitch analysis of monophonic audio: a pitch track (pYIN: YIN with a distribution of thresholds) and the notes in it (the pYIN
// "MonoNote" hidden Markov model with attack / stable / silent states, decoded by Viterbi). See docs/logic-reference/flex-algorithms.md.

struct PitchOptions {
    double minHz = 40.0;    // lowest pitch that can be found (the pitch track and the note states reach down to it)
    double maxHz = 1600.0;  // highest pitch that can be found
    double meanThreshold = 0.15;  // mean of the Beta(2, b) prior over the YIN thresholds
};

// One note found in the audio. Positions are frames of the analysed audio.
struct FlexNote {
    std::int64_t start = 0;
    std::int64_t length = 0;
    double pitch = 0.0;  // MIDI note number, fractional (the median of the pitch track over the note)
};

struct PitchAnalysis {
    int sampleRate = 0;
    int hop = 0;                 // frames between two entries of `midi`
    std::vector<float> midi;     // the pitch track as fractional MIDI note numbers; 0 where the sound is not pitched
    std::vector<FlexNote> notes;
    std::int64_t frames = 0;     // length of the analysed audio

    // The pitch track at frame position `pos` (linear interpolation between entries; 0 where either side is unpitched).
    double midiAt(std::int64_t pos) const;
};

// `d` may have any number of channels (they are averaged).
PitchAnalysis analysePitch(const WavData& d, const PitchOptions& options = {});

double midiToHz(double midi);
double hzToMidi(double hz);

}  // namespace lpc
