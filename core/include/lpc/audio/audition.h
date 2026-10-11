#pragma once
#include <vector>

namespace lpc::audio {

// A short sound to be heard at once, whatever the transport does (Flex Pitch: the note under the mouse; one block per side, at the
// engine's rate). Built on the project thread and handed to the audio thread, which only reads it.
struct AuditionClip {
    std::vector<float> left, right;
};

}  // namespace lpc::audio
