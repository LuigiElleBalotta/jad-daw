#pragma once
#include <memory>
#include <vector>

#include "lpc/audio/messages.h"
#include "lpc/audio/render_graph.h"
#include "lpc/media_store.h"
#include "lpc/model.h"

namespace lpc {

std::vector<Uuid> processingOrder(const Project& project);
audio::StripParams stripParamsOf(const Strip& strip);

std::unique_ptr<audio::TrackConfig> buildConfig(const Project& project, const Track& track, MediaStore& media);
std::unique_ptr<audio::TrackNode> buildNode(const Project& project, const Track& track, MediaStore& media);

std::vector<audio::AudioMsg> initialMessages(const Project& project, MediaStore& media);
std::vector<audio::AudioMsg> diffToMessages(const Project& before, const Project& after, MediaStore& media);

}  // namespace lpc
