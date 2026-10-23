#pragma once
#include <memory>

#include "lpc/audio/processors.h"
#include "lpc/media_store.h"
#include "lpc/model.h"

namespace lpc::audio {

// The Sampler instrument: plays the zones of the instrument's state with the notes it gets (linear interpolation, 32 voices, attack and release in ms).
// The sound of every zone is read into memory when it is made (on the project thread). Returns a silent instrument when no zone can be played.
std::shared_ptr<IInstrument> makeSamplerInstrument(const ProcessorRef& instrument, const Project& project, MediaStore& media, double sampleRate);

}  // namespace lpc::audio
