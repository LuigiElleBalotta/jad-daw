// Track Stacks: a folder stack gathers tracks under a header that opens and closes; a summing stack does the same and the tracks also play
// through it (their output is the stack's bus, so its fader and inserts act on all of them).
#include <QUuid>

#include <algorithm>
#include <nlohmann/json.hpp>

#include "bridge/project_controller.h"
#include <optional>

#include "lpc/model_json.h"
#include "lpc/project_host.h"

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


// ---------------------------------------------------------------- Track Alternatives

namespace {

// The track as the project has it now.
std::optional<lpc::Track> trackOf(lpc::ProjectHost& host, const QString& id) {
    return host.read([&](const lpc::Project& p) -> std::optional<lpc::Track> {
        if (const lpc::Track* t = p.findTrack(lpc::Uuid::parse(id.toStdString()).value_or(lpc::Uuid{}))) return *t;
        return std::nullopt;
    }).get();
}

nlohmann::json alternativesCommand(const lpc::Track& t, const std::string& active, const std::vector<lpc::TrackAlternative>& others, const std::vector<lpc::Region>& regions) {
    return {{"type", "set_track_alternatives"}, {"trackId", t.id.toString()}, {"active", active},
            {"alternatives", [&] { nlohmann::json a = nlohmann::json::array(); for (const lpc::TrackAlternative& x : others) a.push_back({{"name", x.name}, {"regions", x.regions}}); return a; }()},
            {"regions", regions}};
}

}  // namespace

void ProjectController::newTrackAlternative(const QString& trackId, bool duplicate) {
    if (!host_) return;
    const std::optional<lpc::Track> t = trackOf(*host_, trackId);
    if (!t) return;
    std::vector<lpc::TrackAlternative> others = t->alternatives;
    others.push_back({t->alternative.empty() ? std::string("Main") : t->alternative, t->regions});  // the one that played is kept aside
    std::string name;
    for (int n = static_cast<int>(others.size()); name.empty(); ++n) {
        const std::string candidate = (duplicate ? "Copy " : "Alt ") + std::to_string(n);
        bool taken = false;
        for (const lpc::TrackAlternative& a : others) taken = taken || a.name == candidate;
        if (!taken) name = candidate;
    }
    std::vector<lpc::Region> regions;
    if (duplicate)
        for (lpc::Region r : t->regions) {  // a copy plays now; the original stays in the alternative that was playing
            r.id = lpc::Uuid::random();
            regions.push_back(std::move(r));
        }
    sendCommand(alternativesCommand(*t, name, others, regions));
}

void ProjectController::selectTrackAlternative(const QString& trackId, const QString& name) {
    if (!host_) return;
    const std::optional<lpc::Track> t = trackOf(*host_, trackId);
    if (!t) return;
    std::vector<lpc::TrackAlternative> others = t->alternatives;
    const auto it = std::find_if(others.begin(), others.end(), [&](const lpc::TrackAlternative& a) { return QString::fromStdString(a.name) == name; });
    if (it == others.end()) return;
    std::vector<lpc::Region> regions = std::move(it->regions);
    const std::string active = it->name;
    it->name = t->alternative.empty() ? std::string("Main") : t->alternative;  // the two trade places
    it->regions = t->regions;
    sendCommand(alternativesCommand(*t, active, others, regions));
}

void ProjectController::deleteTrackAlternative(const QString& trackId) {
    if (!host_) return;
    const std::optional<lpc::Track> t = trackOf(*host_, trackId);
    if (!t) return;
    if (t->alternatives.empty()) {
        emit notice("A track needs at least one alternative");
        return;
    }
    std::vector<lpc::TrackAlternative> others = t->alternatives;
    lpc::TrackAlternative next = std::move(others.front());
    others.erase(others.begin());
    sendCommand(alternativesCommand(*t, next.name, others, next.regions));
}

}  // namespace jad
