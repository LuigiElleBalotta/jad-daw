#include "lpc/undo_stack.h"

namespace lpc {

std::optional<CommandError> UndoStack::execute(Project& project, CommandPtr command) {
    ApplyResult r = command->apply(project);
    if (!r.ok()) return r.error;
    undo_.push_back({std::shared_ptr<const Command>(std::move(command)), std::shared_ptr<const Command>(std::move(r.inverse))});
    redo_.clear();
    return std::nullopt;
}

void UndoStack::coalesceFrom(std::size_t mark) {
    if (mark >= undo_.size() || undo_.size() - mark < 2) return;
    Entry merged{undo_.back().forward, undo_[mark].inverse};
    undo_.resize(mark);
    undo_.push_back(std::move(merged));
}

std::optional<CommandError> UndoStack::undo(Project& project) {
    if (undo_.empty()) return CommandError{"nothing_to_undo", "undo history is empty"};
    ApplyResult r = undo_.back().inverse->apply(project);
    if (!r.ok()) return r.error;
    redo_.push_back(undo_.back());
    undo_.pop_back();
    return std::nullopt;
}

std::optional<CommandError> UndoStack::redo(Project& project) {
    if (redo_.empty()) return CommandError{"nothing_to_redo", "redo history is empty"};
    ApplyResult r = redo_.back().forward->apply(project);
    if (!r.ok()) return r.error;
    undo_.push_back(redo_.back());
    redo_.pop_back();
    return std::nullopt;
}

void UndoStack::clear() {
    undo_.clear();
    redo_.clear();
}

}  // namespace lpc
