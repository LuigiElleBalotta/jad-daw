#pragma once
#include <memory>
#include <optional>
#include <vector>

#include "lpc/command.h"

namespace lpc {

class UndoStack {
public:
    // Applies the command and records it. On failure nothing is recorded and the project is unchanged.
    std::optional<CommandError> execute(Project& project, CommandPtr command);
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    std::optional<CommandError> undo(Project& project);
    std::optional<CommandError> redo(Project& project);
    void clear();

private:
    struct Entry {
        std::shared_ptr<const Command> forward;
        std::shared_ptr<const Command> inverse;
    };
    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
};

}  // namespace lpc
