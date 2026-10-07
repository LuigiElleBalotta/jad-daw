#pragma once
#include <memory>

#include "lpc/device.h"

namespace lpc {

std::unique_ptr<IAudioDevice> makeJuceAudioDevice();

}  // namespace lpc
