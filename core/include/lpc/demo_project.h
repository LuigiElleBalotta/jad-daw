#pragma once
#include <filesystem>

#include "lpc/model.h"

namespace lpc {

// A small deterministic project (fixed ids): a sine "Keys" instrument playing four chords over two bars at
// 120 bpm with a post-fader send to a "Reverb Bus", and a 220 Hz "Tone" audio track. When `dir` is not empty
// the media file dir/audio/tone.wav is written (create the project folder with saveProject separately).
Project makeDemoProject(const std::filesystem::path& dir);

}  // namespace lpc
