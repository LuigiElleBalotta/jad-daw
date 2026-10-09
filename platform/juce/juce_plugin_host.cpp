#include "juce_plugin_host.h"

#include <map>
#include <mutex>
#include <optional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "editor_window.h"
#include "plugin_processor.h"

namespace lpc {

namespace {
std::string keyOf(const InsertSlot& s) { return s.track.toString() + "/" + std::to_string(s.index); }
}  // namespace

struct JucePluginHost::Impl {
    struct Entry {
        std::string id, state;
        std::shared_ptr<PluginProcessor> proc;
        bool pending = false;
        bool failed = false;
        std::uint64_t generation = 0;
        InsertSlot slot;
        std::string label;
    };

    mutable std::mutex mutex;  // entries, catalogue, listener
    std::map<std::string, Entry> entries;
    std::vector<PluginDescriptor> catalogue;
    std::function<void(const InsertSlot&)> ready;
    std::uint64_t nextGeneration = 0;
    bool alive = true;

    // message thread only
    juce::AudioPluginFormatManager formats;
    std::map<std::string, std::unique_ptr<EditorWindow>> editors;
    std::function<void(const InsertSlot&)> editorClosed;

    Impl() { formats.addFormat(new juce::VST3PluginFormat()); }

    std::optional<juce::PluginDescription> describe(const std::string& id) const {
        std::lock_guard lock(mutex);
        for (const PluginDescriptor& d : catalogue) {
            if (d.id != id) continue;
            if (auto xml = juce::XmlDocument::parse(juce::String(d.native))) {
                juce::PluginDescription desc;
                if (desc.loadFromXml(*xml)) return desc;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }

    void notifyReady(const InsertSlot& slot) {
        std::function<void(const InsertSlot&)> listener;
        {
            std::lock_guard lock(mutex);
            listener = ready;
        }
        if (listener) listener(slot);
    }

    void closeEditor(const std::string& key) { editors.erase(key); }  // message thread

    // Message thread. Loads one instance and publishes it unless a newer request replaced this one.
    void load(const InsertSlot& slot, const ProcessorRef& ref, const juce::PluginDescription& desc, double sampleRate, int maxBlock,
              std::uint64_t generation) {
        juce::String error;
        std::unique_ptr<juce::AudioPluginInstance> instance = formats.createPluginInstance(desc, sampleRate, maxBlock, error);
        std::shared_ptr<PluginProcessor> proc = PluginProcessor::create(std::move(instance), ref.state, sampleRate, maxBlock);
        bool published = false;
        {
            std::lock_guard lock(mutex);
            auto it = entries.find(keyOf(slot));
            if (it != entries.end() && it->second.generation == generation) {
                it->second.pending = false;
                it->second.failed = !proc;
                it->second.proc = proc;
                published = true;
            }
        }
        if (published && proc) notifyReady(slot);
    }
};

JucePluginHost::JucePluginHost() : impl_(std::make_shared<Impl>()) {}

JucePluginHost::~JucePluginHost() {
    // Callbacks posted earlier hold a shared_ptr to Impl; they see `alive == false` and do nothing.
    {
        std::lock_guard lock(impl_->mutex);
        impl_->alive = false;
        impl_->ready = nullptr;
    }
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    if (manager && manager->isThisTheMessageThread()) impl_->editors.clear();
}

void JucePluginHost::setCatalogue(std::vector<PluginDescriptor> descriptors) {
    std::lock_guard lock(impl_->mutex);
    impl_->catalogue = std::move(descriptors);
}

std::vector<PluginDescriptor> JucePluginHost::catalogue() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->catalogue;
}

void JucePluginHost::setReadyListener(std::function<void(const InsertSlot&)> listener) {
    std::lock_guard lock(impl_->mutex);
    impl_->ready = std::move(listener);
}

void JucePluginHost::setEditorClosedListener(std::function<void(const InsertSlot&)> listener) { impl_->editorClosed = std::move(listener); }

std::shared_ptr<audio::IProcessor> JucePluginHost::acquire(const InsertSlot& slot, const ProcessorRef& ref, double sampleRate, int maxBlock) {
    const auto desc = impl_->describe(ref.processorId);
    if (!desc) return nullptr;
    const std::string key = keyOf(slot);
    std::uint64_t generation = 0;
    bool closeOldEditor = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->alive) return nullptr;
        Impl::Entry& e = impl_->entries[key];
        const bool same = e.id == ref.processorId && e.state == ref.state;
        if (same && e.proc) return e.proc;
        if (same && (e.pending || e.failed)) return nullptr;
        closeOldEditor = e.proc != nullptr;
        e.id = ref.processorId;
        e.state = ref.state;
        e.slot = slot;
        e.label = ref.label;
        e.proc.reset();
        e.pending = true;
        e.failed = false;
        generation = e.generation = ++impl_->nextGeneration;
    }

    auto impl = impl_;
    auto* manager = juce::MessageManager::getInstance();
    if (manager->isThisTheMessageThread()) {
        if (closeOldEditor) impl->closeEditor(key);
        impl->load(slot, ref, *desc, sampleRate, maxBlock, generation);
        std::lock_guard lock(impl->mutex);
        const auto it = impl->entries.find(key);
        return it != impl->entries.end() && it->second.generation == generation ? it->second.proc : nullptr;
    }
    juce::MessageManager::callAsync([impl, slot, ref, d = *desc, sampleRate, maxBlock, generation, key, closeOldEditor] {
        {
            std::lock_guard lock(impl->mutex);
            if (!impl->alive) return;
        }
        if (closeOldEditor) impl->closeEditor(key);
        impl->load(slot, ref, d, sampleRate, maxBlock, generation);
    });
    return nullptr;
}

void JucePluginHost::prune(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) {
    std::vector<std::string> dropped;
    {
        std::lock_guard lock(impl_->mutex);
        for (auto it = impl_->entries.begin(); it != impl_->entries.end();) {
            bool keep = false;
            for (const auto& [slot, ref] : live)
                if (keyOf(slot) == it->first && ref.processorId == it->second.id) keep = true;
            if (keep) {
                ++it;
            } else {
                dropped.push_back(it->first);
                it = impl_->entries.erase(it);
            }
        }
    }
    if (dropped.empty()) return;
    auto impl = impl_;
    juce::MessageManager::callAsync([impl, dropped] {
        for (const std::string& key : dropped) impl->closeEditor(key);
    });
}

std::string JucePluginHost::captureState(const InsertSlot& slot) {
    std::shared_ptr<PluginProcessor> proc;
    {
        std::lock_guard lock(impl_->mutex);
        const auto it = impl_->entries.find(keyOf(slot));
        if (it == impl_->entries.end() || !it->second.proc) return {};
        proc = it->second.proc;
    }
    const std::string state = proc->captureState();
    std::lock_guard lock(impl_->mutex);
    if (const auto it = impl_->entries.find(keyOf(slot)); it != impl_->entries.end() && it->second.proc == proc) it->second.state = state;
    return state;
}

bool JucePluginHost::openEditor(const InsertSlot& slot) {
    const std::string key = keyOf(slot);
    if (auto it = impl_->editors.find(key); it != impl_->editors.end()) {
        it->second->toFront(true);
        return true;
    }
    std::shared_ptr<PluginProcessor> proc;
    std::string label;
    {
        std::lock_guard lock(impl_->mutex);
        const auto it = impl_->entries.find(key);
        if (it == impl_->entries.end() || !it->second.proc) return false;
        proc = it->second.proc;
        label = it->second.label;
    }
    juce::AudioProcessorEditor* editor = proc->createEditor();
    if (!editor) return false;
    const juce::String title = label.empty() ? juce::String(proc->name()) : juce::String(label);
    auto weak = std::weak_ptr<Impl>(impl_);
    impl_->editors[key] = std::make_unique<EditorWindow>(editor, title, [weak, key, slot] {
        // never delete a window from inside its own callback: close it on the next message-loop turn
        juce::MessageManager::callAsync([weak, key, slot] {
            if (auto impl = weak.lock()) {
                impl->closeEditor(key);
                if (impl->editorClosed) impl->editorClosed(slot);
            }
        });
    });
    return true;
}

bool JucePluginHost::editorOpen(const InsertSlot& slot) const { return impl_->editors.count(keyOf(slot)) > 0; }

void JucePluginHost::closeAllEditors() { impl_->editors.clear(); }

void JucePluginHost::releaseAll() {
    closeAllEditors();
    std::lock_guard lock(impl_->mutex);
    impl_->entries.clear();
}

}  // namespace lpc
