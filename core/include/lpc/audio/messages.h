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

enum class MsgKind : std::uint8_t { AddTrack, RemoveTrack, SetStrip, SetConfig, Reorder, Play, Stop, Locate, SetLoop, SetClick };

// Project thread -> audio thread. obj meaning per kind:
//   AddTrack: TrackNode*, SetConfig: TrackConfig*, Reorder: std::vector<Uuid>* (processing order),
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
