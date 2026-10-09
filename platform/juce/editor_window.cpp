#include "editor_window.h"

namespace lpc {

EditorWindow::EditorWindow(juce::AudioProcessorEditor* editor, const juce::String& title, std::function<void()> onClose)
    : DocumentWindow(title, juce::Colours::darkgrey, DocumentWindow::closeButton), onClose_(std::move(onClose)) {
    setUsingNativeTitleBar(true);
    setContentOwned(editor, true);
    setResizable(editor->isResizable(), false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

}  // namespace lpc
