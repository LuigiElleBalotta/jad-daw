#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <set>
#include "lpc/session_player.h"

using namespace lpc;

TEST_CASE("session players: a chord progression is read, with slash chords, split bars and repeats", "[session]") {
    std::string error;
    const auto chords = parseChords("Am F C/E G7 %  C.G -", 4, error);
    REQUIRE(error.empty());
    REQUIRE(chords.size() == 8);
    CHECK(chords[0].root == 9);
    CHECK(chords[0].tones == std::vector<int>{3, 7});
    CHECK(chords[2].bass == 4);
    CHECK(chords[3].tones == std::vector<int>{4, 7, 10});
    CHECK(chords[4].root == 7);               // % repeats the G7
    CHECK(chords[5].beats == 2.0);            // C.G: two chords share a bar
    CHECK(chords[6].root == 7);
    CHECK(chords[7].root == -1);              // a bar of rest
    const auto flat = parseChords("Bb Ebm7 F#sus4", 3, error);
    REQUIRE(error.empty());
    CHECK(flat[0].root == 10);
    CHECK(flat[1].root == 3);
    CHECK(flat[2].root == 6);
    CHECK(flat[0].beats == 3.0);
    CHECK(parseChords("Am H", 4, error).empty());
    CHECK(!error.empty());
    CHECK(parseChords("", 4, error).empty());
}

TEST_CASE("session players: the bass follows the roots and the drummer plays whole bars", "[session]") {
    std::string error;
    const auto chords = parseChords("C G Am F", 4, error);
    REQUIRE(error.empty());
    SessionParams p;
    p.kind = SessionKind::Bass;
    p.style = "root";
    const auto bass = generateSession(chords, p);
    REQUIRE(bass.size() == 8);   // two notes a bar
    CHECK(bass[0].note % 12 == 0);
    CHECK(bass[2].note % 12 == 7);
    CHECK(bass[4].note % 12 == 9);
    for (const auto& n : bass) CHECK((n.note >= 28 && n.note < 52));
    CHECK(bass[2].start == 4 * kPPQ);
    p.style = "walking";
    CHECK(generateSession(chords, p).size() == 16);   // four a bar

    p.kind = SessionKind::Drummer;
    p.style = "rock";
    p.intensity = 0.5;
    const auto drums = generateSession(chords, p);
    std::set<int> kinds;
    for (const auto& n : drums) kinds.insert(n.note);
    CHECK(kinds.count(36) == 1);   // kick
    CHECK(kinds.count(38) == 1);   // snare
    CHECK(kinds.count(42) == 1);   // hat
    CHECK(std::count_if(drums.begin(), drums.end(), [](const MidiNote& n) { return n.note == 38 && n.start < 4 * kPPQ; }) >= 2);
    // the same seed gives the same part, another seed another velocities
    p.seed = 1;
    CHECK(generateSession(chords, p) == drums);
    p.seed = 9;
    CHECK(generateSession(chords, p) != drums);
}

TEST_CASE("session players: the keys keep to the chord tones and move little from chord to chord", "[session]") {
    std::string error;
    const auto chords = parseChords("C F G C", 4, error);
    REQUIRE(error.empty());
    SessionParams p;
    p.kind = SessionKind::Keys;
    p.style = "block";
    const auto keys = generateSession(chords, p);
    REQUIRE(keys.size() == 12);   // a triad a bar
    for (const auto& n : keys) CHECK((n.note >= 48 && n.note < 90));
    // the first chord C E G
    std::set<int> first;
    for (std::size_t i = 0; i < 3; ++i) first.insert(keys[i].note % 12);
    CHECK(first == std::set<int>{0, 4, 7});
    // voice-leading: the average pitch of the chords stays within a fifth
    double previous = 0;
    for (std::size_t c = 0; c < 4; ++c) {
        double avg = 0;
        for (std::size_t i = 0; i < 3; ++i) avg += keys[c * 3 + i].note / 3.0;
        if (c) CHECK(std::abs(avg - previous) < 7.0);
        previous = avg;
    }
    p.style = "arpeggio";
    CHECK(generateSession(chords, p).size() >= 32);
    p.style = "comp";
    CHECK(!generateSession(chords, p).empty());
    for (SessionKind k : {SessionKind::Drummer, SessionKind::Bass, SessionKind::Keys})
        for (const std::string& style : sessionStyles(k)) {
            p.kind = k;
            p.style = style;
            CHECK(!generateSession(chords, p).empty());
        }
}

#include "lpc/audio/processors.h"

TEST_CASE("session players: the Drum Kit instrument sounds on General MIDI drum notes and ignores the others", "[session]") {
    audio::Synth synth(48000.0);
    audio::SynthParams sp;
    sp.drums = true;
    sp.level = 0.5f;
    synth.setParams(sp);
    std::vector<float> l(4800, 0.0f), r(4800, 0.0f);
    synth.noteOn(36, 110);
    CHECK(synth.active());
    synth.render(l.data(), r.data(), 512);
    float peak = 0;
    for (int i = 0; i < 512; ++i) peak = std::max(peak, std::abs(l[static_cast<std::size_t>(i)]));
    CHECK(peak > 0.05f);
    audio::Synth quiet(48000.0);
    quiet.setParams(sp);
    quiet.noteOn(20, 110);                     // not a drum
    CHECK(!quiet.active());
    synth.allNotesOff();
    CHECK(!synth.active());
}
