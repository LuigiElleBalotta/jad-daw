#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lpc/command.h"

namespace lpc {

class UndoStack {
public:
    // Applies the command and records it. On failure nothing is recorded and the project is unchanged.
    std::optional<CommandError> execute(Project& project, CommandPtr command);
    bool canUndo() const { return !undo_.empty(); }
    std::size_t size() const { return undo_.size(); }
    // The steps recorded since the history had `mark` entries become one step: undoing it restores what the first of them
    // changed, redoing it applies the last. Meant for a gesture that sets the same parameter many times (a fader drag).
    void coalesceFrom(std::size_t mark);
    bool canRedo() const { return !redo_.empty(); }
    std::optional<CommandError> undo(Project& project);
    std::optional<CommandError> redo(Project& project);
    void clear();
    // What each step did, oldest first, in words ("Move region", "3 changes"...): the steps that can be undone and the ones that can be redone
    // (the redo list is in the order they would be redone).
    std::vector<std::string> undoLabels() const;
    std::vector<std::string> redoLabels() const;

private:
    struct Entry {
        std::shared_ptr<const Command> forward;
        std::shared_ptr<const Command> inverse;
    };
    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
};

}  // namespace lpc
