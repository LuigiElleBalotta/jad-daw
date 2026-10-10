#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lpc/tempo_map.h"
#include "lpc/uuid.h"

namespace lpc {

enum class TrackKind { Audio, Midi, Instrument, Aux, Bus, Master };
enum class TimeBase { Musical, Absolute };

struct MidiNote {
    Ticks start = 0;  // relative to the region start
    Ticks length = 0;
    std::uint8_t note = 60;
    std::uint8_t velocity = 100;
    bool muted = false;  // Mute Notes: kept in the region, not played
    bool operator==(const MidiNote&) const = default;
};

// A MIDI message that is not a note, in a region: a control change (status 0xB0: data1 = controller, data2 = value), channel aftertouch
// (0xD0: data1 = pressure) or a pitch bend (0xE0: data1 = low 7 bits, data2 = high 7 bits; 0x2000 is the centre).
struct MidiControl {
    Ticks tick = 0;  // relative to the region start
    std::uint8_t status = 0xB0, data1 = 0, data2 = 0;
    bool operator==(const MidiControl&) const = default;
};

struct Region {
    Uuid id;
    TimeBase timeBase = TimeBase::Musical;
    std::int64_t start = 0;   // ticks (Musical) or microseconds (Absolute)
    std::int64_t length = 0;  // same unit as start
    Uuid mediaId;             // null for MIDI regions
    std::int64_t sourceOffsetFrames = 0;
    float gainDb = 0.0f;
    std::int64_t fadeIn = 0, fadeOut = 0;  // audio: the ramps at the ends, in the unit of start (each at most as long as the region)
    bool muted = false;  // Mute Regions: kept in the track, not played
    std::string takeGroup;  // regions of one track that share it are takes of the same passage: one plays (the others are muted)
    int transpose = 0;           // MIDI regions, played: semitones added to every note
    int velocityOffset = 0;      // MIDI regions, played: added to every velocity
    Ticks quantize = 0;          // MIDI regions, played: > 0 moves the start of every note to the nearest multiple of this many ticks
    std::int64_t loopLength = 0;  // > 0: the first loopLength (same unit as length, at most length) repeat until the end of the region
    std::vector<MidiNote> notes;
    std::vector<MidiControl> controls;  // MIDI regions: sorted by tick
    bool operator==(const Region&) const = default;
};

struct ProcessorRef {
    std::string processorId;  // e.g. "builtin.gain"
    std::map<std::string, double> params;
    std::string state;  // opaque, base64 for real plugins
    std::string label;  // display name of a plug-in (informational; empty for built-ins)
    bool bypass = false;  // the insert passes the signal unchanged (a plug-in keeps its latency)
    bool operator==(const ProcessorRef&) const = default;
};

struct Send {
    Uuid id;
    Uuid targetTrackId;
    float levelDb = 0.0f;
    bool preFader = false;
    bool operator==(const Send&) const = default;
};

struct Strip {
    float gainDb = 0.0f;
    float pan = 0.0f;  // -1 (left) .. +1 (right)
    bool mute = false;
    bool solo = false;
    std::vector<ProcessorRef> inserts;
    std::vector<Send> sends;
    Uuid output;  // null = master
    int input = 0;  // the audio input the track records: 0 = inputs 1 and 2 as stereo, n >= 1 = input n as mono
    bool operator==(const Strip&) const = default;
};

struct AutomationPoint {
    Ticks tick = 0;
    double value = 0.0;
    bool operator==(const AutomationPoint&) const = default;
};

struct AutomationLane {
    Uuid id;
    std::string target;
    std::vector<AutomationPoint> points;
    bool operator==(const AutomationLane&) const = default;
};

// How the notes of an instrument track are shaped when they play (the Inspector's Transpose, Velocity, Key Limit and Velocity Limit).
struct MidiShaping {
    int transpose = 0;                 // semitones
    int velocity = 0;                  // added to every velocity
    int keyLow = 0, keyHigh = 127;     // notes outside are not played
    int velocityLow = 1, velocityHigh = 127;  // velocities are brought inside
    bool operator==(const MidiShaping&) const = default;
};

struct Track {
    Uuid id;
    TrackKind kind = TrackKind::Audio;
    std::string name;
    std::string color;
    Strip strip;
    std::optional<ProcessorRef> instrument;
    std::string patchId;  // the built-in patch applied to the track; empty when none
    bool showInTracks = true;  // false: a bus or aux that lives in the Mixer only, not in the Tracks area
    MidiShaping midi;  // instrument tracks
    double delayMs = 0.0;  // audio and instrument tracks: the regions play this many milliseconds later (earlier when negative)
    std::string automationMode = "read";  // "off" (the lanes are ignored), "read", "touch", "latch" or "write" (the last three record fader moves)
    std::vector<Region> regions;
    std::vector<AutomationLane> automation;
    bool operator==(const Track&) const = default;
};

struct Marker {
    Uuid id;
    Ticks tick = 0;
    std::string name;
    bool operator==(const Marker&) const = default;
};

// Tracks whose strips move together (Mix > Group Settings): a change of one member's volume, pan, mute or solo reaches the others.
struct Group {
    Uuid id;
    std::string name;
    std::vector<Uuid> members;  // tracks (never the master), each in at most one group
    bool volume = true, pan = false, mute = true, solo = true, selection = true;
    bool operator==(const Group&) const = default;
};

struct MediaItem {
    Uuid id;
    std::string path;  // relative to the project folder, forward slashes
    std::string hash;
    int sampleRate = 0;
    int channels = 0;
    std::int64_t frames = 0;
    bool operator==(const MediaItem&) const = default;
};

struct Project {
    std::string name = "Untitled";
    int sampleRate = 48000;
    TempoMap tempoMap;
    std::vector<Marker> markers;
    std::vector<Group> groups;
    std::vector<Track> tracks;  // tracks[0] is the master track in a new project
    std::vector<MediaItem> mediaPool;

    Project();
    explicit Project(Uuid masterId);

    Track* findTrack(const Uuid& id);
    const Track* findTrack(const Uuid& id) const;
    Track* findTrackOfRegion(const Uuid& regionId, std::size_t* regionIndex = nullptr);
    const MediaItem* findMedia(const Uuid& id) const;
    const Track* master() const;

    bool operator==(const Project&) const = default;
};

}  // namespace lpc
