#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lpc/plugin_host.h"

namespace lpc {

class JucePluginHost final : public IPluginHost {
public:
    JucePluginHost();
    ~JucePluginHost() override;

    void setCatalogue(std::vector<PluginDescriptor> descriptors);  // any thread
    std::vector<PluginDescriptor> catalogue() const override;

    std::shared_ptr<audio::IProcessor> acquire(const InsertSlot& slot, const ProcessorRef& ref, double sampleRate, int maxBlock) override;
    void setWanted(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) override;
    void prune(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) override;
    std::string captureState(const InsertSlot& slot) override;
    void setReadyListener(std::function<void(const InsertSlot&)> listener) override;

    // Message thread only.
    bool openEditor(const InsertSlot& slot);
    bool editorOpen(const InsertSlot& slot) const;
    void closeAllEditors();
    void setEditorClosedListener(std::function<void(const InsertSlot&)> listener);
    void releaseAll();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;  // shared with the callbacks posted to the message thread, so they can outlive the host safely
};

}  // namespace lpc
