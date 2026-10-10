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

namespace lpc {

namespace {

std::string humanize(const std::string& type) {
    static const std::pair<const char*, const char*> names[] = {
        {"add_track", "Add track"}, {"remove_track", "Delete track"}, {"set_strip", "Channel strip"}, {"set_tempo", "Tempo"}, {"remove_tempo", "Delete tempo change"},
        {"set_signature", "Time signature"}, {"remove_signature", "Delete signature change"}, {"set_track_props", "Track properties"},
        {"add_media", "Add audio file"}, {"remove_media", "Remove audio file"}, {"add_region", "Add region"}, {"remove_region", "Delete region"},
        {"move_region", "Move region"}, {"replace_region", "Edit region"}, {"resize_region", "Resize region"}, {"split_region", "Split region"},
        {"join_regions", "Join regions"}, {"add_send", "Add send"}, {"remove_send", "Remove send"}, {"set_send", "Send"}, {"set_inserts", "Inserts"},
        {"add_insert", "Add insert"}, {"remove_insert", "Remove insert"}, {"set_insert_param", "Insert parameter"}, {"set_insert_state", "Plug-in state"},
        {"move_insert", "Move insert"}, {"set_insert_bypass", "Bypass insert"}, {"set_patch_id", "Patch"}, {"set_instrument", "Instrument"}, {"set_instrument_state", "Instrument state"}, {"set_region_loop", "Region loop"}, {"set_track_freeze", "Freeze"},
        {"set_output", "Output"}, {"set_region_gain", "Region gain"}, {"set_region_fades", "Region fades"}, {"set_markers", "Markers"},
        {"set_groups", "Groups"}, {"set_automation", "Automation"}, {"set_project_name", "Project name"}, {"set_track_order", "Track order"}};
    for (const auto& [id, label] : names)
        if (type == id) return label;
    std::string out = type;
    for (char& c : out)
        if (c == '_') c = ' ';
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

std::string labelOf(const Command& c) {
    if (c.type() != "transaction") return humanize(c.type());
    const auto j = c.toJson();
    const auto n = j.contains("commands") ? j["commands"].size() : 0;
    if (n > 0 && j["commands"][0].contains("type")) {
        const std::string firstType = j["commands"][0]["type"].get<std::string>();
        const std::string first = humanize(firstType);
        bool same = true;
        for (const auto& inner : j["commands"]) same = same && inner.value("type", std::string()) == firstType;
        if (same) return n == 1 ? first : first + " (" + std::to_string(n) + ")";
    }
    return std::to_string(n) + " changes";
}

}  // namespace

std::vector<std::string> UndoStack::undoLabels() const {
    std::vector<std::string> out;
    for (const Entry& e : undo_) out.push_back(labelOf(*e.forward));
    return out;
}

std::vector<std::string> UndoStack::redoLabels() const {
    std::vector<std::string> out;
    for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) out.push_back(labelOf(*it->forward));
    return out;
}

}  // namespace lpc
