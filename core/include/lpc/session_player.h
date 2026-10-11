#pragma once
#include <string>
#include <vector>

#include "lpc/model.h"

namespace lpc {

// Session Players: a part (drums, bass or keys) made from a chord progression, a style and how hard it plays. Our own generators, not Apple's:
// patterns written by rule, with a seeded humanizing of the velocities, and deterministic for a given seed.
enum class SessionKind { Drummer, Bass, Keys };

struct SessionChord {
    int root = 0;            // pitch class 0..11 (C = 0)
    std::vector<int> tones;  // the chord tones above the root, in semitones (3, 7 for a minor triad)
    int bass = -1;           // slash chord: the pitch class in the bass, -1 for the root
    double beats = 4.0;      // how long it lasts
    std::string name;
};

// One chord per bar, separated by spaces; '.' splits a bar between chords ("C.G"), '%' repeats the last chord, "-" is a bar of rest.
// Chords: a letter A-G, '#' or 'b', then maj, m, min, dim, aug, sus2, sus4, 7, maj7, m7, 6, 9, add9 (any of them), and "/X" for the bass.
// Empty `error` means it parsed. `beatsPerBar` sets how long a bar lasts.
std::vector<SessionChord> parseChords(const std::string& text, int beatsPerBar, std::string& error);

struct SessionParams {
    SessionKind kind = SessionKind::Drummer;
    std::string style = "rock";  // Drummer: rock, pop, funk, ballad, shuffle. Bass: root, eighths, walking, funk. Keys: block, arpeggio, comp, pad.
    double intensity = 0.6;      // 0..1: how loud and how busy
    double swing = 0.0;          // 0..1: the off-beat eighths late
    int beatsPerBar = 4;
    unsigned seed = 1;
    bool fills = true;           // Drummer: a fill every fourth bar
};

// The notes of the part, in ticks from the start of the region, one chord per entry of `chords` laid one after the other.
std::vector<MidiNote> generateSession(const std::vector<SessionChord>& chords, const SessionParams& params);
const std::vector<std::string>& sessionStyles(SessionKind kind);

}  // namespace lpc
