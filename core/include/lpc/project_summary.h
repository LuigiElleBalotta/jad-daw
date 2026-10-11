#pragma once
#include <nlohmann/json.hpp>

#include "lpc/model.h"

namespace lpc {

// The project as a mixer sees it, as JSON: tempo, markers, every track with fader, pan, mute, solo, output, inserts with parameters, sends and regions.
// Shared by `lpc-cli snapshot` and the running app's live link.
nlohmann::json projectSummary(const Project& p);

}  // namespace lpc
