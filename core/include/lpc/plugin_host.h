#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lpc/audio/processors.h"
#include "lpc/model.h"

namespace lpc {

struct PluginDescriptor {
    std::string id;       // "vst3:<32 hex>"
    std::string name, vendor, version, path;
    std::string native;   // opaque data of the host that found it (JUCE: the plug-in description as XML)
    bool operator==(const PluginDescriptor&) const = default;
};

struct InsertSlot {
    Uuid track;
    int index = 0;
    bool operator==(const InsertSlot&) const = default;
};

class IPluginHost {
public:
    virtual ~IPluginHost() = default;
    virtual std::vector<PluginDescriptor> catalogue() const = 0;
    // Project thread. Never blocks. The live, prepared processor behind `slot`, reused while the insert's id and state are
    // unchanged; nullptr while it is not available (loading, missing, failed). A first call starts loading in the background
    // and the ready listener fires when the instance exists.
    virtual std::shared_ptr<audio::IProcessor> acquire(const InsertSlot& slot, const ProcessorRef& ref, double sampleRate, int maxBlock) = 0;
    // Project thread. Drops every instance not listed (slot and the ref it must match).
    virtual void prune(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) = 0;
    // UI thread. Serialises the live state (base64) and remembers it as "already in the model" so the command that stores
    // it does not make the next acquire reload the instance. Empty string when there is no live instance.
    virtual std::string captureState(const InsertSlot& slot) = 0;
    virtual void setReadyListener(std::function<void(const InsertSlot&)> listener) = 0;  // any thread
};

// Builds one insert. Built-ins as before; "vst3:" ids come from the host and are a pass-through (MissingPluginProcessor)
// while the host has nothing for them. nullptr for any other unknown id. Project thread.
std::unique_ptr<audio::IProcessor> makeInsert(const ProcessorRef& ref, IPluginHost* host, const InsertSlot& slot, double sampleRate, int maxBlock);

}  // namespace lpc
