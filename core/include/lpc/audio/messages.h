#pragma once
#include <cstdint>
#include <type_traits>

#include "lpc/uuid.h"

namespace lpc::audio {

// Strip values as the audio thread uses them. gain is linear (not dB).
struct StripParams {
    float gain = 1.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
};

// A heap object handed between threads. Whoever receives it last (always the project thread)
// calls destroy(). The audio thread never calls destroy().
struct Owned {
    void* ptr = nullptr;
    void (*deleter)(void*) = nullptr;
    void destroy() {
        if (ptr && deleter) deleter(ptr);
        ptr = nullptr;
    }
};

template <typename T>
Owned makeOwned(T* object) {
    return Owned{object, [](void* p) { delete static_cast<T*>(p); }};
}

enum class MsgKind : std::uint8_t { AddTrack, RemoveTrack, SetStrip, SetConfig, Reorder, Play, Stop, Locate, SetLoop, SetClick, SetClickKit, StartRecord, StopRecord, SetMonitor, SetLiveTarget, SetLowLatency };

// Project thread -> audio thread. obj meaning per kind:
//   AddTrack: TrackNode*, SetConfig: TrackConfig*, Reorder: std::vector<Uuid>* (processing order),
//   SetLiveTarget: track = the instrument track that plays the live MIDI (null: none),
//   StopRecord: stops capturing but not the transport (frame unused), SetMonitor: track = the track, frame = first input channel + 1 (0 = off),
//   frame2 = second input channel + 1 (0 = the first one on both sides),
//   StartRecord: ClickTrack* (the count-in clicks from 0), frame = count-in length in frames; the transport starts after it
//   and the input is captured from then until Stop,
//   SetLowLatency: frame = the longest latency (frames) an insert may have on a monitored track before it is bypassed (0 = off),
//   SetClickKit: ClickKit* (the samples of the click sounds),
//   SetClick: ClickTrack* (null keeps the current one); frame = 1 switches the metronome on, 0 off
struct AudioMsg {
    MsgKind kind = MsgKind::Stop;
    std::uint64_t seq = 0;
    Uuid track;
    StripParams strip;
    Owned obj;
    std::int64_t frame = 0;   // Locate target; SetLoop start
    std::int64_t frame2 = 0;  // SetLoop end (end <= start switches the loop off)
};

// Audio thread -> project thread.
struct Feedback {
    Owned garbage;
};

static_assert(std::is_trivially_copyable_v<AudioMsg>);
static_assert(std::is_trivially_copyable_v<Feedback>);

}  // namespace lpc::audio
