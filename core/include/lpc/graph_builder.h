#pragma once
#include <memory>
#include <unordered_map>
#include <vector>

#include "lpc/audio/messages.h"
#include "lpc/audio/render_graph.h"
#include "lpc/media_store.h"
#include "lpc/plugin_host.h"
#include "lpc/model.h"

namespace lpc {

struct EdgeDelays {
    int output = 0;
    std::vector<int> sends;  // in the order of the track's sends
    bool operator==(const EdgeDelays&) const = default;
};

// Plug-in delay compensation. Every edge (a track's output and each of its sends) gets the delay that aligns the signals
// arriving at its target; totalLatency is the delay between the timeline and the master output.
struct PdcPlan {
    std::unordered_map<Uuid, EdgeDelays> edges;  // every track except the master
    int totalLatency = 0;
};
PdcPlan computePdc(const Project& project, IPluginHost* plugins);

std::vector<Uuid> processingOrder(const Project& project);
audio::StripParams stripParamsOf(const Strip& strip);

std::unique_ptr<audio::TrackConfig> buildConfig(const Project& project, const Track& track, MediaStore& media,
                                                IPluginHost* plugins = nullptr, const PdcPlan* pdc = nullptr);
std::unique_ptr<audio::TrackNode> buildNode(const Project& project, const Track& track, MediaStore& media,
                                            IPluginHost* plugins = nullptr, const PdcPlan* pdc = nullptr);
// A SetConfig for every track (used when a plug-in finishes loading).
std::vector<audio::AudioMsg> refreshMessages(const Project& project, MediaStore& media, IPluginHost* plugins, PdcPlan* planOut = nullptr);

// `planOut`, when given, receives the delay plan the messages were built with. `diffToMessages` takes in `plan` the plan of
// `before` (null: no plug-in latency, which only makes it rebuild more) and leaves there the plan of `after`; it never asks the
// plug-in host about `before`, because that would make the host reload instances for the old state.
std::vector<audio::AudioMsg> initialMessages(const Project& project, MediaStore& media, IPluginHost* plugins = nullptr, PdcPlan* planOut = nullptr);
std::vector<audio::AudioMsg> diffToMessages(const Project& before, const Project& after, MediaStore& media, IPluginHost* plugins = nullptr, PdcPlan* plan = nullptr);

}  // namespace lpc
