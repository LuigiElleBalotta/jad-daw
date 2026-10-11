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

// What the user did to one note (Flex Pitch). The neutral values leave the note exactly as it was recorded.
struct NoteEdit {
    double semitones = 0.0;   // the note moved up or down (dragged on the pitch axis)
    double fineCents = 0.0;   // fine pitch, -50 .. +50
    double correct = 0.0;     // 0 .. 1: how much of the slow wander of the pitch around the note is taken out
    double vibrato = 1.0;     // 1 as recorded, 0 flat, 2 twice as deep
    double driftIn = 0.0;     // 0 .. 1: how much of the drift into the note (its first moments) is taken out
    double driftOut = 0.0;    // 0 .. 1: the same at the end
    double gainDb = 0.0;
    double formantCents = 0.0;  // the timbre moved up or down without moving the pitch
    bool isNeutral() const {
        return semitones == 0.0 && fineCents == 0.0 && correct == 0.0 && vibrato == 1.0 && driftIn == 0.0 && driftOut == 0.0 && gainDb == 0.0 && formantCents == 0.0;
    }
};

// The sound with the notes edited (TD-PSOLA on the pitch marks of the pitch track, on every channel alike). `edits` goes with
// analysis.notes, one per note (missing ones are neutral). Only frames [from, to) are returned (to < 0: up to the end), so that one note
// can be heard at once while it is dragged. Notes that are neutral, and everything outside the notes, are copied unchanged.
WavData renderFlexPitch(const WavData& src, const PitchAnalysis& analysis, const std::vector<NoteEdit>& edits, std::int64_t from = 0, std::int64_t to = -1);

double midiToHz(double midi);
double hzToMidi(double hz);

}  // namespace lpc
