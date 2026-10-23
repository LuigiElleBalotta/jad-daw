// Live Loops: a grid of cells (scenes down, tracks across). A cell is a loop; launching it starts it at the next multiple of the quantization (a bar by default)
// and it plays until another cell of the track is launched or the cell is stopped. The cells that play are laid over the track by the audio graph; the track's
// own regions are silent while one does. Stopping the transport stops every cell.
#include <QUuid>

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "bridge/project_controller.h"
#include "lpc/commands.h"
#include "lpc/live_loops.h"
#include "lpc/project_host.h"

namespace jad {
namespace {

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

bool holdsCells(const QString& kind) { return kind == QLatin1String("audio") || kind == QLatin1String("instrument") || kind == QLatin1String("midi"); }

}  // namespace

lpc::Ticks ProjectController::liveQuantizeTicks() const {
    bool ok = false;
    const double beats = projectSetting("liveQuantize", "").toDouble(&ok);
    return static_cast<lpc::Ticks>(std::llround((ok && beats > 0 ? beats : barBeats()) * lpc::kPPQ));
}

int ProjectController::liveSceneCount() const {
    int count = std::clamp(projectSetting("sceneCount", "8").toInt(), 1, 256);
    for (auto it = liveCells_.cbegin(); it != liveCells_.cend(); ++it)
        for (const LiveCellRow& c : it.value()) count = std::max(count, c.scene + 1);
    return count;
}

QString ProjectController::liveCellState(const QString& trackId, const QString& cellId) const {
    const auto runs = liveRuns_.constFind(trackId);
    if (runs == liveRuns_.cend()) return QStringLiteral("stopped");
    const lpc::Ticks pos = static_cast<lpc::Ticks>(std::llround(positionBeats_ * lpc::kPPQ));
    for (const LiveRunRow& r : runs.value()) {
        if (r.cellId != cellId) continue;
        if (pos < r.start) return QStringLiteral("queued");
        if (r.end >= 0 && pos >= r.end) continue;
        return r.end >= 0 ? QStringLiteral("stopping") : QStringLiteral("playing");
    }
    return QStringLiteral("stopped");
}

QVariantMap ProjectController::liveGrid() const {
    QVariantList columns, cells;
    for (const TrackRow& t : allRows_) {
        if (t.master || !holdsCells(t.kind)) continue;
        columns.append(QVariantMap{{"id", t.id}, {"name", t.name}, {"color", t.color}, {"kind", t.kind}});
        for (const LiveCellRow& c : liveCells_.value(t.id))
            cells.append(QVariantMap{{"trackId", t.id}, {"scene", c.scene}, {"cellId", c.id}, {"name", c.name}, {"state", liveCellState(t.id, c.id)}});
    }
    QVariantList names;
    const int scenes = liveSceneCount();
    for (int i = 0; i < scenes; ++i) names.append(projectSetting(QStringLiteral("sceneName.%1").arg(i), QString()));
    return {{"tracks", columns}, {"cells", cells}, {"scenes", scenes}, {"sceneNames", names}};
}

// The cells of the project as the last snapshot saw them (refreshed with the snapshot).
void ProjectController::refreshLiveCells(const std::vector<std::pair<QString, LiveCellRow>>& cells) {
    QHash<QString, QVector<LiveCellRow>> next;
    for (const auto& [track, cell] : cells) next[track].push_back(cell);
    if (next == liveCells_) return;
    liveCells_ = std::move(next);
    ++liveRevision_;
    emit liveLoopsChanged();
}

void ProjectController::pushLiveRuns(const QString& trackId) {
    if (!host_) return;
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    if (!id) return;
    std::vector<lpc::LiveRun> runs;
    for (const LiveRunRow& r : liveRuns_.value(trackId))
        if (const auto cell = lpc::Uuid::parse(r.cellId.toStdString())) runs.push_back({*cell, r.start, r.end});
    host_->setLiveRuns(*id, std::move(runs));
}

void ProjectController::launchCell(const QString& trackId, int scene) {
    if (!host_) return;
    QString cellId;
    for (const LiveCellRow& c : liveCells_.value(trackId))
        if (c.scene == scene) cellId = c.id;
    if (cellId.isEmpty()) return;
    const lpc::Ticks pos = static_cast<lpc::Ticks>(std::llround(positionBeats_ * lpc::kPPQ));
    const lpc::Ticks start = playing_ ? lpc::launchTick(pos, liveQuantizeTicks()) : pos;
    QVector<LiveRunRow> runs;
    for (LiveRunRow r : liveRuns_.value(trackId)) {
        if (r.end >= 0 && r.end <= pos) continue;      // over
        if (r.start >= start) continue;                // queued for later: this launch replaces it
        r.end = r.end >= 0 ? std::min(r.end, start) : start;  // the cell that plays gives way where the new one begins
        runs.push_back(r);
    }
    runs.push_back({cellId, start, -1});
    liveRuns_[trackId] = runs;
    pushLiveRuns(trackId);
    if (!playing_) play();
    lastLiveSignature_.clear();
    updateLive();
}

void ProjectController::launchScene(int scene) {
    for (const TrackRow& t : allRows_) {
        if (t.master || !holdsCells(t.kind)) continue;
        bool has = false;
        for (const LiveCellRow& c : liveCells_.value(t.id)) has = has || c.scene == scene;
        if (has) launchCell(t.id, scene);
    }
}

void ProjectController::stopCell(const QString& trackId) {
    if (!host_ || !liveRuns_.contains(trackId)) return;
    const lpc::Ticks pos = static_cast<lpc::Ticks>(std::llround(positionBeats_ * lpc::kPPQ));
    const lpc::Ticks at = playing_ ? lpc::launchTick(pos, liveQuantizeTicks()) : pos;
    QVector<LiveRunRow> runs;
    for (LiveRunRow r : liveRuns_.value(trackId)) {
        if (r.end >= 0 && r.end <= pos) continue;
        if (r.start >= at) continue;  // never began
        r.end = r.end >= 0 ? std::min(r.end, at) : at;
        runs.push_back(r);
    }
    liveRuns_[trackId] = runs;
    pushLiveRuns(trackId);
    lastLiveSignature_.clear();
    updateLive();
}

void ProjectController::stopAllCells() {
    const QStringList tracks = liveRuns_.keys();
    for (const QString& id : tracks) stopCell(id);
}

// From tick(): cells whose stop has been reached are let go, and the grid is told when what it shows changes.
void ProjectController::updateLive() {
    if (liveRuns_.isEmpty()) return;
    const lpc::Ticks pos = static_cast<lpc::Ticks>(std::llround(positionBeats_ * lpc::kPPQ));
    QString signature;
    QStringList emptied;
    for (auto it = liveRuns_.begin(); it != liveRuns_.end(); ++it) {
        const int before = it.value().size();
        it.value().erase(std::remove_if(it.value().begin(), it.value().end(), [pos](const LiveRunRow& r) { return r.end >= 0 && r.end <= pos; }), it.value().end());
        if (it.value().size() != before) {
            pushLiveRuns(it.key());
            if (it.value().isEmpty()) emptied << it.key();
        }
        for (const LiveRunRow& r : it.value()) signature += liveCellState(it.key(), r.cellId) + r.cellId;
    }
    for (const QString& id : emptied) liveRuns_.remove(id);
    if (signature == lastLiveSignature_) return;
    lastLiveSignature_ = signature;
    ++liveRevision_;
    emit liveLoopsChanged();
}

void ProjectController::stopLiveOnStop() {
    if (liveRuns_.isEmpty()) return;
    const QStringList tracks = liveRuns_.keys();
    liveRuns_.clear();
    for (const QString& id : tracks) pushLiveRuns(id);
    lastLiveSignature_.clear();
    ++liveRevision_;
    emit liveLoopsChanged();
}

void ProjectController::clearCell(const QString& trackId, int scene) {
    if (!host_) return;
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    if (!id) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::Track* t = project.findTrack(*id);
    if (!t) return;
    std::vector<lpc::LiveCell> cells;
    for (const lpc::LiveCell& c : t->cells)
        if (c.scene != scene) cells.push_back(c);
    if (cells.size() == t->cells.size()) return;
    sendCommand(lpc::makeSetLiveCells(*id, cells)->toJson());
}

// The selected region of the track becomes the cell (in the scene), a copy: the region itself stays where it is.
void ProjectController::setCellFromSelection(const QString& trackId, int scene) {
    if (!host_) return;
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    if (!id) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::Track* t = project.findTrack(*id);
    if (!t) return;
    std::vector<const lpc::Region*> picked;
    for (const QString& rid : selectedRegions_)
        for (const lpc::Region& r : t->regions)
            if (QString::fromStdString(r.id.toString()) == rid) picked.push_back(&r);
    if (picked.empty()) {
        emit notice("Select a region of this track first");
        return;
    }
    lpc::Ticks from = picked.front()->start, to = picked.front()->start + picked.front()->length;
    for (const lpc::Region* r : picked) {
        from = std::min(from, r->start);
        to = std::max(to, r->start + r->length);
    }
    const auto region = lpc::cellFromRegions(project, *t, picked, from, to, *lpc::Uuid::parse(newId().toStdString()));
    if (!region) {
        emit notice("Nothing of the selection can go in a cell");
        return;
    }
    std::vector<lpc::LiveCell> cells;
    for (const lpc::LiveCell& c : t->cells)
        if (c.scene != scene) cells.push_back(c);
    cells.push_back({*lpc::Uuid::parse(newId().toStdString()), scene, region->name, *region});
    sendCommand(lpc::makeSetLiveCells(*id, cells)->toJson());
}

// Edit > Copy to Live Loops Scene: the selected regions go to one cell each in the first empty scene; with nothing selected the cycle's span does when the cycle
// is on, and without it every section of the Arrangement track gets a scene of its own.
void ProjectController::copyToLiveLoopsScene() {
    if (!host_) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    std::map<lpc::Uuid, std::vector<lpc::LiveCell>> added;
    int scene = lpc::firstEmptyScene(project);
    const auto cellOf = [&](const lpc::Track& t, const std::vector<const lpc::Region*>& regions, lpc::Ticks from, lpc::Ticks to, int at) {
        if (const auto region = lpc::cellFromRegions(project, t, regions, from, to, *lpc::Uuid::parse(newId().toStdString())))
            added[t.id].push_back({*lpc::Uuid::parse(newId().toStdString()), at, region->name, *region});
    };
    const auto allOf = [](const lpc::Track& t) {
        std::vector<const lpc::Region*> all;
        for (const lpc::Region& r : t.regions) all.push_back(&r);
        return all;
    };
    bool any = false;
    if (!selectedRegions_.isEmpty()) {
        for (const lpc::Track& t : project.tracks) {
            std::vector<const lpc::Region*> picked;
            for (const lpc::Region& r : t.regions)
                if (selectedRegions_.contains(QString::fromStdString(r.id.toString()))) picked.push_back(&r);
            if (picked.empty()) continue;
            lpc::Ticks from = picked.front()->start, to = picked.front()->start + picked.front()->length;
            for (const lpc::Region* r : picked) {
                from = std::min(from, r->start);
                to = std::max(to, r->start + r->length);
            }
            cellOf(t, picked, from, to, scene);
            any = true;
        }
    } else if (loop_ && loopEndBeats_ > loopStartBeats_) {
        const lpc::Ticks from = static_cast<lpc::Ticks>(std::llround(loopStartBeats_ * lpc::kPPQ)), to = static_cast<lpc::Ticks>(std::llround(loopEndBeats_ * lpc::kPPQ));
        for (const lpc::Track& t : project.tracks) {
            if (t.kind != lpc::TrackKind::Audio && t.kind != lpc::TrackKind::Instrument && t.kind != lpc::TrackKind::Midi) continue;
            cellOf(t, allOf(t), from, to, scene);
        }
        any = true;
    } else if (!project.arrangement.empty()) {
        lpc::Ticks end = 0;
        for (const lpc::Track& t : project.tracks)
            for (const lpc::Region& r : t.regions) end = std::max(end, r.start + r.length);
        for (std::size_t i = 0; i < project.arrangement.size(); ++i) {
            const lpc::Ticks from = project.arrangement[i].tick, to = i + 1 < project.arrangement.size() ? project.arrangement[i + 1].tick : end;
            if (to <= from) continue;
            for (const lpc::Track& t : project.tracks) {
                if (t.kind != lpc::TrackKind::Audio && t.kind != lpc::TrackKind::Instrument && t.kind != lpc::TrackKind::Midi) continue;
                cellOf(t, allOf(t), from, to, scene);
            }
            sendCommand({{"type", "set_project_setting"}, {"key", QStringLiteral("sceneName.%1").arg(scene).toStdString()}, {"value", project.arrangement[i].name}});
            ++scene;
            any = true;
        }
    }
    if (!any || added.empty()) {
        emit notice("Select regions, or turn the cycle on, to copy them to a scene");
        return;
    }
    nlohmann::json commands = nlohmann::json::array();
    for (auto& [trackId, cells] : added) {
        const lpc::Track* t = project.findTrack(trackId);
        std::vector<lpc::LiveCell> all = t->cells;
        for (lpc::LiveCell& c : cells) all.push_back(std::move(c));
        commands.push_back(lpc::makeSetLiveCells(trackId, all)->toJson());
    }
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::addLiveScene() { setProjectSetting("sceneCount", QString::number(std::min(256, liveSceneCount() + 1))); }

void ProjectController::renameLiveScene(int scene, const QString& name) { setProjectSetting(QStringLiteral("sceneName.%1").arg(scene), name.left(60)); }

}  // namespace jad
