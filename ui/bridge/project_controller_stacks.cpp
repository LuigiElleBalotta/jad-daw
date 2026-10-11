// Track Stacks: a folder stack gathers tracks under a header that opens and closes; a summing stack does the same and the tracks also play
// through it (their output is the stack's bus, so its fader and inserts act on all of them).
#include <QUuid>

#include <algorithm>
#include <nlohmann/json.hpp>

#include "bridge/project_controller.h"
#include "lpc/model_json.h"

namespace jad {

void ProjectController::createStack(const QString& kind) {
    if (!host_ || (kind != "folder" && kind != "summing")) return;
    std::vector<QString> order;  // every track but the master, in project order
    for (const TrackRow& t : allRows_)
        if (!t.master) order.push_back(t.id);
    std::vector<QString> chosen;
    for (const QString& id : order)
        if (selectedTracks_.contains(id)) chosen.push_back(id);
    if (chosen.empty()) {
        emit notice("Select the tracks to put in the stack first");
        return;
    }
    const std::string stackId = lpc::Uuid::random().toString();
    const std::string nullId = lpc::Uuid{}.toString();
    const QString name = kind == "folder" ? QStringLiteral("Folder Stack") : QStringLiteral("Summing Stack");
    nlohmann::json stack = {{"id", stackId}, {"kind", "bus"}, {"name", name.toStdString()}, {"color", ""},
                            {"strip", {{"gainDb", 0}, {"pan", 0}, {"mute", false}, {"solo", false}, {"inserts", nlohmann::json::array()}, {"sends", nlohmann::json::array()}, {"output", nullId}}},
                            {"regions", nlohmann::json::array()}, {"automation", nlohmann::json::array()}, {"instrument", nullptr}, {"stack", kind.toStdString()}};
    nlohmann::json commands = nlohmann::json::array();
    commands.push_back({{"type", "add_track"}, {"index", -1}, {"track", stack}});
    nlohmann::json newOrder = nlohmann::json::array();
    for (const QString& id : order) {
        if (std::find(chosen.begin(), chosen.end(), id) != chosen.end()) {
            if (id == chosen.front()) {
                newOrder.push_back(stackId);
                for (const QString& c : chosen) newOrder.push_back(c.toStdString());
            }
            continue;
        }
        newOrder.push_back(id.toStdString());
    }
    for (const QString& c : chosen) {
        commands.push_back({{"type", "set_track_props"}, {"trackId", c.toStdString()}, {"parent", stackId}});
        if (kind == "summing") commands.push_back({{"type", "set_output"}, {"trackId", c.toStdString()}, {"output", stackId}});
    }
    commands.push_back({{"type", "set_track_order"}, {"order", newOrder}});
    sendCommand({{"type", "transaction"}, {"commands", commands}});
}

// The tracks directly in the stack.
static std::vector<const TrackRow*> membersOf(const std::vector<TrackRow>& rows, const QString& stackId) {
    std::vector<const TrackRow*> out;
    for (const TrackRow& t : rows)
        if (t.parentId == stackId) out.push_back(&t);
    return out;
}

void ProjectController::flattenStack(const QString& trackId) {
    const TrackRow* stack = nullptr;
    for (const TrackRow& t : allRows_)
        if (t.id == trackId) stack = &t;
    if (!host_ || !stack || stack->stack.isEmpty()) {
        emit notice("Select a Track Stack first");
        return;
    }
    nlohmann::json commands = nlohmann::json::array();
    const std::string nullId = lpc::Uuid{}.toString();
    for (const TrackRow* m : membersOf(allRows_, trackId)) {
        commands.push_back({{"type", "set_track_props"}, {"trackId", m->id.toStdString()}, {"parent", stack->parentId.isEmpty() ? nullId : stack->parentId.toStdString()}});
        if (m->outputId == trackId) commands.push_back({{"type", "set_output"}, {"trackId", m->id.toStdString()}, {"output", nullId}});
    }
    for (const TrackRow& t : allRows_)  // anything still routed to the stack (its sends) is left to the user: the stack goes only when nothing uses it
        if (t.id != trackId && t.parentId != trackId && t.outputId == trackId) commands.push_back({{"type", "set_output"}, {"trackId", t.id.toStdString()}, {"output", nullId}});
    commands.push_back({{"type", "remove_track"}, {"trackId", trackId.toStdString()}});
    sendCommand({{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::convertFolderStackToSummingStack(const QString& trackId) {
    const TrackRow* stack = nullptr;
    for (const TrackRow& t : allRows_)
        if (t.id == trackId) stack = &t;
    if (!host_ || !stack || stack->stack != "folder") {
        emit notice("Select a Folder Stack first");
        return;
    }
    nlohmann::json commands = nlohmann::json::array();
    commands.push_back({{"type", "set_track_props"}, {"trackId", trackId.toStdString()}, {"stack", "summing"}, {"name", stack->name == "Folder Stack" ? "Summing Stack" : stack->name.toStdString()}});
    for (const TrackRow* m : membersOf(allRows_, trackId)) commands.push_back({{"type", "set_output"}, {"trackId", m->id.toStdString()}, {"output", trackId.toStdString()}});
    sendCommand({{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::toggleStackOpen(const QString& trackId) {
    for (const TrackRow& t : allRows_)
        if (t.id == trackId && !t.stack.isEmpty()) {
            sendCommand({{"type", "set_track_props"}, {"trackId", trackId.toStdString()}, {"stackOpen", !t.stackOpen}});
            return;
        }
}

}  // namespace jad
