#pragma once
#include <functional>
#include <memory>
#include <optional>
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
    bool instrument = false;  // a software instrument (notes in, sound out), not an effect
    bool operator==(const PluginDescriptor&) const = default;
};

inline constexpr int kInstrumentSlot = -1;  // InsertSlot{track, kInstrumentSlot}: the instrument of an instrument track

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
    // Project thread, before a rebuild asks for instances: the slots the project holds now (slot and the ref it must match).
    // A live instance whose slot is listed with the same id and state stays there: no other slot may take it over. An instance
    // whose slot is no longer listed may be taken over by the slot that now holds the same plug-in and state (a moved insert).
    virtual void setWanted(const std::vector<std::pair<InsertSlot, ProcessorRef>>& /*live*/) {}
    // Project thread. Drops every instance not listed (slot and the ref it must match).
    virtual void prune(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) = 0;
    // UI thread. Serialises the live state (base64) and remembers it as "already in the model" so the command that stores
    // it does not make the next acquire reload the instance. nullopt when there is no live instance; an empty string is a
    // live plug-in whose state is legitimately empty.
    virtual std::optional<std::string> captureState(const InsertSlot& slot) = 0;
    virtual void setReadyListener(std::function<void(const InsertSlot&)> listener) = 0;  // any thread
    // The instrument of an instrument track lives in the slot with index kInstrumentSlot. Same contract as acquire(): never blocks,
    // nullptr while the plug-in is loading, missing or failed.
    virtual std::shared_ptr<audio::IInstrument> acquireInstrument(const InsertSlot& /*slot*/, const ProcessorRef& /*ref*/, double /*sampleRate*/,
                                                                  int /*maxBlock*/) {
        return nullptr;
    }
};

// Builds one insert. Built-ins as before; "vst3:" ids come from the host and are a pass-through (MissingPluginProcessor)
// while the host has nothing for them. nullptr for any other unknown id. Project thread.
std::unique_ptr<audio::IProcessor> makeInsert(const ProcessorRef& ref, IPluginHost* host, const InsertSlot& slot, double sampleRate, int maxBlock);

}  // namespace lpc
