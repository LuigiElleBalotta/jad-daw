#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "lpc/model.h"

namespace lpc {

// One track of a Standard MIDI File: the notes of one MIDI channel with absolute positions in project ticks (kPPQ per quarter note).
struct MidiFileTrack {
    std::string name;
    int channel = 0;              // 0..15
    std::vector<MidiNote> notes;  // sorted by start; note.start is absolute
    std::vector<MidiControl> controls;  // control changes, channel aftertouch and pitch bends; tick is absolute
};

struct MidiFileData {
    std::vector<MidiFileTrack> tracks;
    double bpm = 0;                      // the first tempo of the file; 0 when it has none
    int numerator = 0, denominator = 0;  // the first time signature; 0 when it has none
};

// Reads a format 0 or 1 file (format 2 is read as 1) with a ticks-per-quarter division. A chunk with several channels gives one track per
// channel. Throws std::runtime_error with a message that says what is wrong.
MidiFileData parseMidiFile(const std::vector<std::uint8_t>& bytes);

// Writes a format 1 file: a conductor track (tempo, time signature) and one track per entry; positions are written as they are.
std::vector<std::uint8_t> writeMidiFile(const MidiFileData& data);

}  // namespace lpc
