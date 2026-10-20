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
    std::vector<std::uint8_t> level;  // kLevelBar..kLevelDivision of each beat; missing (a count-in): the bar for an accent, else a beat
};

// The four accent levels of Logic's Metronome Settings and the Klopfgeist row of each: a note (the pitch of the click, 60 = C3 as Logic names it),
// a velocity (the level) and whether that level sounds.
inline constexpr int kLevelBar = 0, kLevelGroup = 1, kLevelBeat = 2, kLevelDivision = 3, kClickLevels = 4;
struct KnockLevel {
    int note = 84;
    int velocity = 112;
    bool on = true;
    bool operator==(const KnockLevel&) const = default;
};
// Logic's defaults: Bar G5/127, Group D#5/119, Beat C5/112, Division C2/60 (off)
inline std::array<KnockLevel, kClickLevels> defaultKnockLevels() { return {{{91, 127, true}, {87, 119, true}, {84, 112, true}, {48, 60, false}}}; }

// The samples loaded for the slots (mono, at the engine's rate); an empty slot plays the built-in blip.
struct ClickKit {
    std::array<std::vector<float>, kClickSlots> sample;
    std::array<float, kClickSlots> gain;
    std::array<KnockLevel, kClickLevels> levels = defaultKnockLevels();
    float tonality = 0.5f;  // Klopfgeist Tone, 0..1
    float volume = 0.8f;    // Klopfgeist Volume, 0..1
    ClickKit() { gain.fill(1.0f); }
};

// What the user chose: how the bar is counted and the sample file of each slot (empty = built-in blip).
struct ClickSettings {
    std::string mode = "beats";  // "beats": 1 2 3 4; "eighths": 1 & 2 &; "sixteenths": 1 e & a; "grouped": 1 la li 2 la li
    std::string grouping;        // "grouped": the size of each group, like "3+2+2"; empty = groups of three in 6/8, 9/8, 12/8, else beats
    std::array<std::string, kClickSlots> files;
    std::array<float, kClickSlots> gain;
    std::array<KnockLevel, kClickLevels> levels = defaultKnockLevels();  // the Klopfgeist, Logic's own click
    float tonality = 0.5f;
    float volume = 0.8f;
    ClickSettings() { gain.fill(1.0f); }
};

}  // namespace lpc::audio
