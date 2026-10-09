#pragma once
#include <cstdint>
#include <vector>

#include "lpc/media_store.h"
#include "lpc/model.h"
#include "lpc/plugin_host.h"

namespace lpc {

struct RenderOptions {
    std::int64_t startFrame = 0;
    std::int64_t frames = -1;  // -1: from startFrame to the end of the last region, plus tailSeconds
    double tailSeconds = 0.5;
    int blockSize = 256;       // clamped to [1, 65536]
    IPluginHost* plugins = nullptr;  // hosts the "vst3:" inserts; the output is aligned for their latency
};

struct RenderResult {
    std::vector<float> interleaved;  // stereo
    int sampleRate = 0;
    std::int64_t frames = 0;
};

// End of the last region of any track, in frames (0 for a project without regions).
std::int64_t projectEndFrame(const Project& project);

// Renders with the real engine and no device. Use a MediaStore with streaming = false: memory sources make
// the result deterministic (a streaming source could report an underrun when disk is slow).
RenderResult renderOffline(const Project& project, MediaStore& media, const RenderOptions& options = {});

}  // namespace lpc
