#include <catch2/catch_test_macros.hpp>

#include "lpc/midi_file.h"
#include "lpc/tempo_map.h"

using namespace lpc;

TEST_CASE("midi file: write then parse gives the same notes, tempo and signature", "[midi][file]") {
    MidiFileData d;
    d.bpm = 90;
    d.numerator = 3;
    d.denominator = 4;
    MidiFileTrack a;
    a.name = "Lead";
    a.channel = 2;
    a.notes = {MidiNote{0, 480, 60, 100, false}, MidiNote{960, 240, 64, 80, false}, MidiNote{960, 960, 67, 127, false}};
    d.tracks.push_back(a);
    const auto bytes = writeMidiFile(d);
    const MidiFileData back = parseMidiFile(bytes);
    REQUIRE(back.tracks.size() == 1);
    REQUIRE(back.tracks[0].name == "Lead");
    REQUIRE(back.tracks[0].channel == 2);
    REQUIRE(back.tracks[0].notes.size() == 3);
    REQUIRE(back.tracks[0].notes[0].length == 480);
    REQUIRE(back.tracks[0].notes[1].start == 960);
    REQUIRE(back.tracks[0].notes[2].velocity == 127);
    REQUIRE(back.bpm > 89.99);
    REQUIRE(back.bpm < 90.01);
    REQUIRE(back.numerator == 3);
    REQUIRE(back.denominator == 4);
}

TEST_CASE("midi file: other divisions are converted, running status and note-on velocity 0 are read", "[midi][file]") {
    // format 0, 1 track, 480 ticks per quarter: C4 for a quarter (running status, note-on 0 ends it), then E4 for half a quarter
    const std::vector<std::uint8_t> f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0,
                                         'M', 'T', 'r', 'k', 0, 0, 0, 19,
                                         0x00, 0x90, 60, 90,   // on
                                         0x83, 0x60, 60, 0,    // 480 later: on with velocity 0 (running status)
                                         0x00, 64, 70,         // E4 on straight away (running status)
                                         0x81, 0x70, 64, 0,    // 240 later: off
                                         0x00, 0xFF, 0x2F, 0x00};
    const MidiFileData d = parseMidiFile(f);
    REQUIRE(d.tracks.size() == 1);
    REQUIRE(d.tracks[0].notes.size() == 2);
    REQUIRE(d.tracks[0].notes[0].note == 60);
    REQUIRE(d.tracks[0].notes[0].length == kPPQ);             // 480 file ticks = a quarter note
    REQUIRE(d.tracks[0].notes[1].start == kPPQ);
    REQUIRE(d.tracks[0].notes[1].length == kPPQ / 2);
}

TEST_CASE("midi file: damaged and unsupported files are refused with a message", "[midi][file]") {
    REQUIRE_THROWS_AS(parseMidiFile({}), std::runtime_error);
    REQUIRE_THROWS_AS(parseMidiFile({'R', 'I', 'F', 'F', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), std::runtime_error);
    const std::vector<std::uint8_t> smpte = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0xE7, 0x28};
    REQUIRE_THROWS_AS(parseMidiFile(smpte), std::runtime_error);
    const std::vector<std::uint8_t> cut = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0, 'M', 'T', 'r', 'k', 0, 0, 0, 3, 0x00, 0x90, 60};
    REQUIRE_THROWS_AS(parseMidiFile(cut), std::runtime_error);
}

TEST_CASE("midi file: a note-on left open ends with the track and one chunk with two channels gives two tracks", "[midi][file]") {
    const std::vector<std::uint8_t> f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x03, 0xC0,  // 960 per quarter
                                         'M', 'T', 'r', 'k', 0, 0, 0, 17,
                                         0x00, 0x90, 60, 100,
                                         0x00, 0x91, 62, 100,
                                         0x87, 0x40, 0xFF, 0x2F, 0x00,  // 960 ticks later: the end, both notes still on
                                         0x00, 0xFF, 0x2F, 0x00};
    // the length field above counts 17 bytes; the extra end event is ignored once the first end of track is read
    const MidiFileData d = parseMidiFile(f);
    REQUIRE(d.tracks.size() == 2);
    REQUIRE(d.tracks[0].channel == 0);
    REQUIRE(d.tracks[1].channel == 1);
    REQUIRE(d.tracks[0].notes[0].length == 960);
}

TEST_CASE("midi file: controllers, aftertouch and pitch bend are written and read", "[midi][file][controls]") {
    MidiFileData d;
    MidiFileTrack t;
    t.name = "Keys";
    t.notes = {MidiNote{0, 960, 60, 100, false}};
    t.controls = {MidiControl{0, 0xB0, 64, 127}, MidiControl{480, 0xE0, 0x00, 0x60}, MidiControl{480, 0xD0, 50, 0}, MidiControl{960, 0xB0, 64, 0}};
    d.tracks.push_back(t);
    const MidiFileData back = parseMidiFile(writeMidiFile(d));
    REQUIRE(back.tracks.size() == 1);
    REQUIRE(back.tracks[0].notes.size() == 1);
    REQUIRE(back.tracks[0].controls.size() == 4);
    REQUIRE(back.tracks[0].controls[0] == MidiControl{0, 0xB0, 64, 127});
    bool bend = false, touch = false;
    for (const MidiControl& c : back.tracks[0].controls) {
        if (c.status == 0xE0 && c.data2 == 0x60) bend = true;
        if (c.status == 0xD0 && c.data1 == 50) touch = true;
    }
    REQUIRE(bend);
    REQUIRE(touch);
    REQUIRE(back.tracks[0].controls.back() == MidiControl{960, 0xB0, 64, 0});
}

TEST_CASE("midi file: all-notes-off and the other channel mode messages are not controllers", "[midi][file][controls]") {
    const std::vector<std::uint8_t> f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x03, 0xC0, 'M', 'T', 'r', 'k', 0, 0, 0, 17,
                                         0x00, 0x90, 60, 100,
                                         0x00, 0xB0, 123, 0,   // all notes off: not kept
                                         0x00, 0xB0, 1, 64,    // modulation: kept
                                         0x83, 0x60, 0x80, 60, 0};
    const MidiFileData d = parseMidiFile(f);
    REQUIRE(d.tracks.size() == 1);
    REQUIRE(d.tracks[0].controls.size() == 1);
    REQUIRE(d.tracks[0].controls[0].data1 == 1);
}
