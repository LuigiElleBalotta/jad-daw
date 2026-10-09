#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lpc::audio {

// The sounds a count or click can use: slot 0 is the built-in blip, 1..32 are the beat numbers ("1", "2", ...), then the
// subdivisions "e", "&", "a" and the group fillers "la", "li".
inline constexpr int kClickSlots = 38;
inline constexpr int kSlotE = 33, kSlotAnd = 34, kSlotA = 35, kSlotLa = 36, kSlotLi = 37;

// The metronome's beats in absolute frames, sorted; accent marks the first beat of a bar, slot picks the sound. Built on the project
// thread from the tempo map and the counting mode and handed to the audio thread, which only reads it.
struct ClickTrack {
    std::vector<std::int64_t> frames;
    std::vector<std::uint8_t> accent;
    std::vector<std::uint8_t> slot;
};

// The samples loaded for the slots (mono, at the engine's rate); an empty slot plays the built-in blip.
struct ClickKit {
    std::array<std::vector<float>, kClickSlots> sample;
    std::array<float, kClickSlots> gain;
    ClickKit() { gain.fill(1.0f); }
};

// What the user chose: how the bar is counted and the sample file of each slot (empty = built-in blip).
struct ClickSettings {
    std::string mode = "beats";  // "beats": 1 2 3 4; "eighths": 1 & 2 &; "sixteenths": 1 e & a; "grouped": 1 la li 2 la li
    std::string grouping;        // "grouped": the size of each group, like "3+2+2"; empty = groups of three in 6/8, 9/8, 12/8, else beats
    std::array<std::string, kClickSlots> files;
    std::array<float, kClickSlots> gain;
    ClickSettings() { gain.fill(1.0f); }
};

}  // namespace lpc::audio
