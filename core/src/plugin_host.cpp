#include "lpc/plugin_host.h"

#include "lpc/processor_ids.h"

namespace lpc {

std::unique_ptr<audio::IProcessor> makeInsert(const ProcessorRef& ref, IPluginHost* host, const InsertSlot& slot, double sampleRate, int maxBlock) {
    if (!isVst3Id(ref.processorId)) return audio::makeEffect(ref);
    if (host) {
        if (std::shared_ptr<audio::IProcessor> live = host->acquire(slot, ref, sampleRate, maxBlock))
            return std::make_unique<audio::SharedProcessor>(std::move(live));
    }
    return std::make_unique<audio::MissingPluginProcessor>();
}

}  // namespace lpc
