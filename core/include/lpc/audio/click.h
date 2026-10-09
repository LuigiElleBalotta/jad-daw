#pragma once
#include <cstdint>
#include <vector>

namespace lpc::audio {

// The metronome's beats in absolute frames, sorted; accent marks the first beat of a bar. Built on the project thread from the
// tempo map and handed to the audio thread, which only reads it.
struct ClickTrack {
    std::vector<std::int64_t> frames;
    std::vector<std::uint8_t> accent;
};

}  // namespace lpc::audio
