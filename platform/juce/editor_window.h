#pragma once
#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace lpc {

// A native window around a plug-in's editor. Message thread only. `onClose` runs when the user closes the window; the owner
// must destroy the window afterwards, not from inside the callback.
class EditorWindow final : public juce::DocumentWindow {
public:
    EditorWindow(juce::AudioProcessorEditor* editor, const juce::String& title, std::function<void()> onClose);
    void closeButtonPressed() override {
        if (onClose_) onClose_();
    }

private:
    std::function<void()> onClose_;
};

}  // namespace lpc
