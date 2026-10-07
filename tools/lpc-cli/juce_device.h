#pragma once
#include <memory>

#include "lpc/device.h"

namespace lpc::cli {

std::unique_ptr<IAudioDevice> makeJuceAudioDevice();

}  // namespace lpc::cli
