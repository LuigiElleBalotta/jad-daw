// Live Loops, as Logic's grid works: tracks are the rows and scenes the columns; a cell is a loop (or a one-shot) with its own start, loop, quantize start, trigger mode and
// recording settings. A launch starts the cell at the Quantize Start of the cell, of its scene or of the grid; the cell that plays on a track gives way to it. While a track
// plays its cells, its regions are silent (the Divider column's Track Activation button gives the track back to them). The cells that play are laid over the track by the
// audio graph (see live_loops.h). Stopping the transport stops the cells and queues the ones that played; recording into a cell takes the pass out of the Tracks area into
// the cell; a performance recording turns what was played into regions.
#include <QFileInfo>
#include <QPointer>
#include <QtConcurrent>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <map>

#include <nlohmann/json.hpp>

#include "bridge/project_controller.h"
#include "lpc/commands.h"
#include "lpc/live_loops.h"
#include "lpc/model_json.h"
#include "lpc/project_io.h"
#include "lpc/wav.h"
#include "lpc/project_host.h"

namespace jad {
namespace {

lpc::Uuid newUuid() { return *lpc::Uuid::parse(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()); }

bool holdsCells(const QString& kind) { return kind == QLatin1String("audio") || kind == QLatin1String("instrument") || kind == QLatin1String("midi"); }

lpc::Ticks toTicks(double beats) { return static_cast<lpc::Ticks>(std::llround(beats * lpc::kPPQ)); }

QString typeOf(const lpc::LiveCell& c, const QString& kind) {
    if (c.pattern) return QStringLiteral("pattern");
    if (!c.region.generator.empty()) return QStringLiteral("session");
    return kind == QLatin1String("audio") ? QStringLiteral("audio") : QStringLiteral("midi");
}

bool sameFamily(const QString& a, const QString& b) {
    const auto fam = [](const QString& k) { return k == QLatin1String("audio") ? 0 : 1; };
    return fam(a) == fam(b);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------------- the grid

lpc::Ticks ProjectController::liveQuantizeTicks(const QString& cellQuantize, int scene) const {
    QString q = cellQuantize;
    if (q.isEmpty() || q == QLatin1String("global")) {
        q = scene >= 0 ? projectSetting(QStringLiteral("sceneQuantize.%1").arg(scene), QString()) : QString();
        if (q.isEmpty() || q == QLatin1String("global")) q = projectSetting("liveQuantize", "");
    }
    if (q == QLatin1String("off")) return -1;
    if (q == QLatin1String("cellEnd")) return -2;
    bool ok = false;
    const double beats = q.toDouble(&ok);
    return toTicks(ok && beats > 0 ? beats : barBeats());
}

int ProjectController::liveSceneCount() const {
    int count = std::clamp(projectSetting("sceneCount", "8").toInt(), 1, 256);
    for (auto it = liveCells_.cbegin(); it != liveCells_.cend(); ++it)
        for (const LiveCellRow& c : it.value()) count = std::max(count, c.cell.scene + 1);
    return count;
}

const lpc::LiveCell* ProjectController::findLiveCell(const lpc::Project& project, const QString& trackId, int scene) const {
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    const lpc::Track* t = id ? project.findTrack(*id) : nullptr;
    if (!t) return nullptr;
    for (const lpc::LiveCell& c : t->cells)
        if (c.scene == scene) return &c;
    return nullptr;
}

QString ProjectController::liveCellState(const QString& trackId, const QString& cellId) const {
    const LiveCellRow* row = nullptr;
    for (const LiveCellRow& c : liveCells_.value(trackId))
        if (QString::fromStdString(c.cell.id.toString()) == cellId) row = &c;
    if (!row) return QStringLiteral("stopped");
    for (const CellRecording& r : cellRec_)
        if (r.trackId == trackId && r.scene == row->cell.scene) return r.state == CellRecording::Waiting ? QStringLiteral("recordQueued") : QStringLiteral("recording");
    const lpc::Ticks pos = toTicks(positionBeats_);
    for (const LiveRunRow& r : liveRuns_.value(trackId)) {
        if (r.cellId != cellId) continue;
        if (pos < r.start) return QStringLiteral("queued");
        if (r.end >= 0 && pos >= r.end) continue;
        return r.end >= 0 && row->cell.loop ? QStringLiteral("stopping") : QStringLiteral("playing");
    }
    const auto q = liveQueued_.constFind(trackId);
    if (q != liveQueued_.cend() && q.value() == row->cell.scene) return QStringLiteral("queued");
    return QStringLiteral("stopped");
}

QVariantMap ProjectController::liveGrid() const {
    QVariantList columns, cells;
    for (const TrackRow& t : allRows_) {
        if (t.master || !holdsCells(t.kind)) continue;
        bool running = false, queued = liveQueued_.contains(t.id);
        for (const LiveCellRow& c : liveCells_.value(t.id)) {
            const QString cellId = QString::fromStdString(c.cell.id.toString());
            const QString state = liveCellState(t.id, cellId);
            running = running || state == QLatin1String("playing") || state == QLatin1String("stopping");
            queued = queued || state == QLatin1String("queued");
            QStringList takeNames;
            for (const lpc::LiveTake& take : c.cell.takes) takeNames << QString::fromStdString(take.name);
            cells.append(QVariantMap{{"trackId", t.id}, {"scene", c.cell.scene}, {"cellId", cellId}, {"name", QString::fromStdString(c.cell.name)}, {"state", state},
                                     {"type", typeOf(c.cell, t.kind)}, {"loop", c.cell.loop}, {"takes", static_cast<int>(c.cell.takes.size())}, {"activeTake", c.cell.activeTake},
                                     {"takeNames", takeNames}});
        }
        columns.append(QVariantMap{{"id", t.id}, {"name", t.name}, {"color", t.color}, {"kind", t.kind}, {"cells", liveCellsMode_.contains(t.id)}, {"running", running}, {"queued", queued},
                                   {"paused", livePaused_.contains(t.id)}, {"armed", trackToggles_.value(QStringLiteral("track.recordArm")).contains(t.id)}});
    }
    QVariantList names;
    QStringList quantize;
    const int scenes = liveSceneCount();
    for (int i = 0; i < scenes; ++i) {
        names.append(projectSetting(QStringLiteral("sceneName.%1").arg(i), QString()));
        quantize.append(projectSetting(QStringLiteral("sceneQuantize.%1").arg(i), QStringLiteral("global")));
    }
    QVariantList recordings;
    for (const CellRecording& r : cellRec_)
        recordings.append(QVariantMap{{"trackId", r.trackId}, {"scene", r.scene}, {"state", r.state == CellRecording::Waiting ? QStringLiteral("recordQueued") : QStringLiteral("recording")}, {"automatic", r.end < 0}});
    return {{"tracks", columns}, {"cells", cells}, {"scenes", scenes}, {"sceneNames", names}, {"sceneQuantize", quantize}, {"performance", perfRecording_}, {"recordings", recordings}};
}

QVariantMap ProjectController::liveCellInfo(const QString& trackId, int scene) const {
    for (const LiveCellRow& row : liveCells_.value(trackId)) {
        if (row.cell.scene != scene) continue;
        const lpc::LiveCell& c = row.cell;
        const double q = 1.0 / lpc::kPPQ;
        return {{"exists", true},         {"name", QString::fromStdString(c.name)},     {"loop", c.loop},
                {"lengthBeats", c.region.length * q}, {"startBeats", c.startPos * q}, {"loopStartBeats", c.loopStart * q},
                {"loopLengthBeats", (c.loopLength > 0 ? c.loopLength : c.region.length - c.loopStart) * q},
                {"trigger", QString::fromStdString(c.trigger)}, {"playFrom", QString::fromStdString(c.playFrom)}, {"quantize", QString::fromStdString(c.quantize)},
                {"qLoopStart", QString::fromStdString(c.qLoopStart)}, {"recording", QString::fromStdString(c.recording)}, {"recLength", QString::fromStdString(c.recLength)},
                {"atRecEnd", QString::fromStdString(c.atRecEnd)}, {"pattern", c.pattern}, {"session", !c.region.generator.empty()}, {"takes", static_cast<int>(c.takes.size())}};
    }
    return {{"exists", false}};
}

double ProjectController::liveProgress(const QString& trackId, int scene) const {
    const lpc::Ticks pos = toTicks(positionBeats_);
    for (const LiveCellRow& row : liveCells_.value(trackId)) {
        if (row.cell.scene != scene) continue;
        const QString cellId = QString::fromStdString(row.cell.id.toString());
        for (const LiveRunRow& r : liveRuns_.value(trackId)) {
            if (r.cellId != cellId || pos < r.start || (r.end >= 0 && pos >= r.end)) continue;
            return static_cast<double>(lpc::cellPositionAt(row.cell, r.offset, pos - r.start)) / static_cast<double>(std::max<lpc::Ticks>(1, row.cell.region.length));
        }
    }
    return -1.0;
}

// The cells of the project as the last snapshot saw them (refreshed with the snapshot).
void ProjectController::refreshLiveCells(const std::vector<LiveCellRow>& cells) {
    QHash<QString, QVector<LiveCellRow>> next;
    for (const LiveCellRow& c : cells) next[c.trackId].push_back(c);
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
        if (const auto cell = lpc::Uuid::parse(r.cellId.toStdString())) runs.push_back({*cell, r.start, r.end, r.offset});
    host_->setLiveRuns(*id, std::move(runs), liveCellsMode_.contains(trackId));
    if (perfRecording_)
        for (const LiveRunRow& r : liveRuns_.value(trackId)) {
            bool found = false;
            for (auto& entry : perfLog_)
                if (entry.first == trackId && entry.second.cellId == r.cellId && entry.second.start == r.start) {
                    entry.second = r;
                    found = true;
                }
            if (!found) perfLog_.push_back({trackId, r});
        }
}

void ProjectController::saveCells(const QString& trackId, std::vector<lpc::LiveCell> cells) {
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    if (id) sendCommand(lpc::makeSetLiveCells(*id, std::move(cells))->toJson());
}

// ---------------------------------------------------------------------------------------------------------------------------------------- start and stop

void ProjectController::launchCell(const QString& trackId, int scene, bool queueOnly) { launchCellAt(trackId, scene, -1, queueOnly); }

// `sceneQuantizeFrom`: the scene whose Quantize Start a global cell follows (-1: the grid's); -3: right now (queued cells at the start of the transport).
void ProjectController::launchCellAt(const QString& trackId, int scene, int sceneQuantizeFrom, bool queueOnly) {
    if (!host_) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::LiveCell* found = findLiveCell(project, trackId, scene);
    if (!found) return;
    const lpc::LiveCell c = *found;
    const QString cellId = QString::fromStdString(c.id.toString());
    if (queueOnly) {
        if (liveQueued_.value(trackId, -1) == scene) liveQueued_.remove(trackId);  // queueing again dequeues
        else liveQueued_[trackId] = scene;
        lastLiveSignature_.clear();
        ++liveRevision_;
        emit liveLoopsChanged();
        return;
    }
    const auto trackUuid = lpc::Uuid::parse(trackId.toStdString());
    const lpc::Track* track = trackUuid ? project.findTrack(*trackUuid) : nullptr;
    if (!track) return;
    const lpc::Ticks pos = toTicks(positionBeats_);
    QVector<LiveRunRow>& runs = liveRuns_[trackId];
    int cur = -1;
    for (int i = 0; i < runs.size(); ++i)
        if (runs[i].start <= pos && (runs[i].end < 0 || runs[i].end > pos)) cur = i;
    const lpc::LiveCell* curCell = nullptr;
    if (cur >= 0)
        for (const lpc::LiveCell& x : track->cells)
            if (QString::fromStdString(x.id.toString()) == runs[cur].cellId) curCell = &x;
    if (cur >= 0 && runs[cur].cellId == cellId) {
        if (c.trigger == "startStop") {
            stopCell(trackId);
            return;
        }
        if (c.trigger == "momentary") return;
    }
    lpc::Ticks q = sceneQuantizeFrom == -3 || !playing_ ? -1 : liveQuantizeTicks(QString::fromStdString(c.quantize), sceneQuantizeFrom);
    if (q == -2 && !curCell) q = liveQuantizeTicks(QStringLiteral("global"), sceneQuantizeFrom);
    const bool smart = projectSetting("liveSmartPickup", "1") == QLatin1String("1");
    lpc::Ticks start = pos, offset = c.startPos;
    if (q == -2) {
        const lpc::Ticks wait = lpc::ticksToCellEnd(*curCell, runs[cur].offset, pos - runs[cur].start);
        start = wait < 0 ? pos : pos + wait;
    } else if (q > 0) {
        const lpc::Ticks boundary = lpc::launchTick(pos, q);
        start = boundary;
        const lpc::Ticks intro = c.loopStart > c.startPos ? c.loopStart - c.startPos : 0;
        if (c.qLoopStart != "off" && c.loop && intro > 0) {
            if (c.qLoopStart == "on") {
                if (boundary - intro >= pos) start = boundary - intro;
                else {
                    start = pos;
                    offset = std::max(c.startPos, c.loopStart - (boundary - pos));
                }
            } else {
                const lpc::Ticks b = lpc::launchTick(pos + intro, q);
                start = b - intro;
            }
        } else if (smart && boundary != pos && c.playFrom == "start") {
            start = pos;
            offset = lpc::cellPositionAt(c, c.startPos, pos - (boundary - q));
        }
    }
    if (c.playFrom == "stopPosition" || liveForceStopPos_) {
        const auto it = liveStopPos_.constFind(cellId);
        if (it != liveStopPos_.cend()) offset = it.value();
    } else if (c.playFrom == "playingCell") {
        if (curCell) offset = lpc::cellPositionAt(*curCell, runs[cur].offset, start - runs[cur].start);
    } else if (c.playFrom == "playhead") {
        offset = lpc::cellPositionAt(c, c.startPos, start);
    }
    offset = std::clamp<lpc::Ticks>(offset, 0, std::max<lpc::Ticks>(0, c.region.length - 1));
    lpc::Ticks end = -1;
    if (!c.loop) end = start + (c.region.length - offset);  // a one-shot ends by itself
    QVector<LiveRunRow> next;
    for (LiveRunRow r : runs) {
        if (r.end >= 0 && r.end <= pos) continue;  // over
        if (r.start >= start) continue;            // queued for later: this launch replaces it
        r.end = r.end >= 0 ? std::min(r.end, start) : start;  // the cell that plays gives way where the new one begins
        next.push_back(r);
    }
    next.push_back({cellId, start, end, offset});
    runs = next;
    liveCellsMode_.insert(trackId);
    livePaused_.remove(trackId);
    liveQueued_.remove(trackId);
    pushLiveRuns(trackId);
    if (!playing_) play();
    lastLiveSignature_.clear();
    updateLive();
}

void ProjectController::releaseCell(const QString& trackId, int scene) {
    if (!host_) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::LiveCell* c = findLiveCell(project, trackId, scene);
    if (c && c->trigger == "momentary") stopCell(trackId);
}

void ProjectController::launchScene(int scene, bool queueOnly) {
    bool any = false;
    for (const TrackRow& t : allRows_) {
        if (t.master || !holdsCells(t.kind)) continue;
        for (const LiveCellRow& c : liveCells_.value(t.id))
            if (c.cell.scene == scene) {
                any = true;
                launchCellAt(t.id, scene, scene, queueOnly);
            }
    }
    if (!any && !queueOnly) stopAllCells();  // the trigger of an empty scene stops everything at the next quantize start
}

void ProjectController::triggerQueued() {
    const QStringList tracks = liveQueued_.keys();
    for (const QString& id : tracks) {
        const int scene = liveQueued_.value(id);
        liveQueued_.remove(id);
        launchCellAt(id, scene, -1, false);
    }
}

void ProjectController::stopCell(const QString& trackId) {
    if (!host_) return;
    liveQueued_.remove(trackId);
    if (!liveRuns_.contains(trackId)) {
        lastLiveSignature_.clear();
        updateLive();
        return;
    }
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    const lpc::Track* track = id ? project.findTrack(*id) : nullptr;
    const lpc::Ticks pos = toTicks(positionBeats_);
    QVector<LiveRunRow> runs;
    for (LiveRunRow r : liveRuns_.value(trackId)) {
        if (r.end >= 0 && r.end <= pos) continue;
        const lpc::LiveCell* cell = nullptr;
        if (track)
            for (const lpc::LiveCell& x : track->cells)
                if (QString::fromStdString(x.id.toString()) == r.cellId) cell = &x;
        lpc::Ticks q = cell && playing_ ? liveQuantizeTicks(QString::fromStdString(cell->quantize), -1) : -1;
        lpc::Ticks at = pos;
        if (q > 0) at = lpc::launchTick(pos, q);
        else if (q == -2 && cell) {
            const lpc::Ticks wait = lpc::ticksToCellEnd(*cell, r.offset, pos - r.start);
            at = wait < 0 ? pos : pos + wait;
        }
        if (r.start >= at) continue;  // never began
        r.end = r.end >= 0 ? std::min(r.end, at) : at;
        runs.push_back(r);
    }
    liveRuns_[trackId] = runs;
    pushLiveRuns(trackId);
    lastLiveSignature_.clear();
    updateLive();
}

void ProjectController::pauseCell(const QString& trackId) {
    const QVector<LiveRunRow> runs = liveRuns_.value(trackId);
    if (runs.isEmpty()) return;
    livePaused_[trackId] = runs.last().cellId;
    stopCell(trackId);
}

void ProjectController::resumeCell(const QString& trackId) {
    const QString cellId = livePaused_.value(trackId);
    for (const LiveCellRow& c : liveCells_.value(trackId))
        if (QString::fromStdString(c.cell.id.toString()) == cellId) {
            liveForceStopPos_ = true;  // it goes on from where it stopped
            launchCellAt(trackId, c.cell.scene, -1, false);
            liveForceStopPos_ = false;
            livePaused_.remove(trackId);
            return;
        }
}

void ProjectController::activateTrackRegions(const QString& trackId) {
    liveCellsMode_.remove(trackId);
    livePaused_.remove(trackId);
    pushLiveRuns(trackId);
    lastLiveSignature_.clear();
    ++liveRevision_;
    emit liveLoopsChanged();
}

void ProjectController::activateTrackCells(const QString& trackId) {
    liveCellsMode_.insert(trackId);
    pushLiveRuns(trackId);
    lastLiveSignature_.clear();
    ++liveRevision_;
    emit liveLoopsChanged();
}

void ProjectController::stopAllCells() {
    QStringList tracks = liveRuns_.keys();
    for (const QString& id : tracks) stopCell(id);
    liveQueued_.clear();
    lastLiveSignature_.clear();
    updateLive();
}

// From tick(): cells whose stop has been reached are let go (their position kept for Stop Position and Resume), and the grid is told when what it shows changes.
void ProjectController::updateLive() {
    updateCellRecording();
    updatePendingCellFiles();
    if (liveRuns_.isEmpty() && liveQueued_.isEmpty() && cellRec_.empty()) return;
    const lpc::Ticks pos = toTicks(positionBeats_);
    QString signature;
    QStringList emptied;
    for (auto it = liveRuns_.begin(); it != liveRuns_.end(); ++it) {
        const int before = it.value().size();
        QVector<LiveRunRow> kept;
        for (const LiveRunRow& r : it.value()) {
            if (r.end >= 0 && r.end <= pos) {
                for (const LiveCellRow& c : liveCells_.value(it.key()))
                    if (QString::fromStdString(c.cell.id.toString()) == r.cellId) {
                        bool finished = false;
                        const lpc::Ticks at = lpc::cellPositionAt(c.cell, r.offset, r.end - r.start, &finished);
                        liveStopPos_[r.cellId] = finished ? c.cell.startPos : at;
                    }
                continue;
            }
            kept.push_back(r);
        }
        it.value() = kept;
        if (kept.size() != before) {
            if (kept.isEmpty() && livePaused_.contains(it.key())) liveCellsMode_.remove(it.key());  // paused: the regions play now
            pushLiveRuns(it.key());
            if (kept.isEmpty()) emptied << it.key();
        }
        for (const LiveRunRow& r : kept) signature += liveCellState(it.key(), r.cellId) + r.cellId;
    }
    for (const QString& id : emptied) liveRuns_.remove(id);
    for (const CellRecording& r : cellRec_) signature += QStringLiteral("rec%1%2").arg(r.scene).arg(static_cast<int>(r.state));
    signature += QString::number(liveQueued_.size());
    if (signature == lastLiveSignature_) return;
    lastLiveSignature_ = signature;
    ++liveRevision_;
    emit liveLoopsChanged();
}

// The transport stops: the cells stop and the ones that played are queued (they start again with the transport, wherever it is placed).
void ProjectController::stopLiveOnStop() {
    finishPerformance();
    cellRec_.clear();
    const lpc::Ticks pos = toTicks(positionBeats_);
    if (liveRuns_.isEmpty()) return;
    const QStringList tracks = liveRuns_.keys();
    for (const QString& id : tracks) {
        for (const LiveRunRow& r : liveRuns_.value(id)) {
            if (r.start > pos || (r.end >= 0 && r.end <= pos)) continue;
            for (const LiveCellRow& c : liveCells_.value(id))
                if (QString::fromStdString(c.cell.id.toString()) == r.cellId) {
                    liveQueued_[id] = c.cell.scene;
                    liveStopPos_[r.cellId] = lpc::cellPositionAt(c.cell, r.offset, pos - r.start);
                }
        }
    }
    liveRuns_.clear();
    for (const QString& id : tracks) pushLiveRuns(id);
    lastLiveSignature_.clear();
    ++liveRevision_;
    emit liveLoopsChanged();
}

void ProjectController::liveTransportStarted() {
    if (liveQueued_.isEmpty()) return;
    const QStringList tracks = liveQueued_.keys();
    for (const QString& id : tracks) {
        const int scene = liveQueued_.value(id);
        liveQueued_.remove(id);
        launchCellAt(id, scene, -3, false);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------- making cells

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
    saveCells(trackId, cells);
}

// The selected regions of the track become the cell (in the scene), a copy: the regions themselves stay where they are.
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
    const auto region = lpc::cellFromRegions(project, *t, picked, from, to, newUuid());
    if (!region) {
        emit notice("Nothing of the selection can go in a cell");
        return;
    }
    std::vector<lpc::LiveCell> cells;
    for (const lpc::LiveCell& c : t->cells)
        if (c.scene != scene) cells.push_back(c);
    lpc::LiveCell cell;
    cell.id = newUuid();
    cell.scene = scene;
    cell.name = region->name;
    cell.region = *region;
    cell.region.loopLength = 0;
    cells.push_back(std::move(cell));
    saveCells(trackId, cells);
}

void ProjectController::newSessionCell(const QString& trackId, int scene, const QVariantMap& options) {
    if (!host_) return;
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    if (!id) return;
    std::string error;
    const nlohmann::json regionJson = buildSessionRegion(options, error);
    if (!error.empty() || regionJson.is_null()) {
        emit notice(QString::fromStdString(error.empty() ? std::string("The Session Player has nothing to play for these chords") : error));
        return;
    }
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::Track* t = project.findTrack(*id);
    if (!t || (t->kind != lpc::TrackKind::Instrument)) {
        emit notice("A Session Player cell goes in a row of a software instrument track");
        return;
    }
    lpc::LiveCell cell;
    cell.id = newUuid();
    cell.scene = scene;
    regionJson.get_to(cell.region);
    cell.region.id = newUuid();
    cell.region.start = 0;
    cell.name = cell.region.name;
    std::vector<lpc::LiveCell> cells;
    for (const lpc::LiveCell& c : t->cells)
        if (c.scene != scene) cells.push_back(c);
    cells.push_back(std::move(cell));
    saveCells(trackId, cells);
}

void ProjectController::newPatternCell(const QString& trackId, int scene) {
    if (!host_) return;
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    if (!id) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::Track* t = project.findTrack(*id);
    if (!t || t->kind != lpc::TrackKind::Instrument) {
        emit notice("A pattern cell goes in a row of a software instrument track");
        return;
    }
    lpc::LiveCell cell;
    cell.id = newUuid();
    cell.scene = scene;
    cell.pattern = true;
    cell.name = "Pattern";
    cell.region.id = newUuid();
    cell.region.length = static_cast<lpc::Ticks>(std::llround(barBeats() * lpc::kPPQ));
    const lpc::Ticks beat = lpc::kPPQ;
    const auto add = [&](int note, double at, int velocity) { cell.region.notes.push_back(lpc::MidiNote{static_cast<lpc::Ticks>(std::llround(at * beat)), beat / 4, static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity), false}); };
    for (double b = 0; b < barBeats(); b += 1.0) {
        add(36, b, 100);                                  // kick on every beat
        if (static_cast<int>(b) % 2 == 1) add(38, b, 96);  // snare on 2 and 4
        add(42, b, 70);
        add(42, b + 0.5, 60);                              // closed hat on the eighths
    }
    std::vector<lpc::LiveCell> cells;
    for (const lpc::LiveCell& c : t->cells)
        if (c.scene != scene) cells.push_back(c);
    cells.push_back(std::move(cell));
    saveCells(trackId, cells);
}

void ProjectController::convertToMidiCell(const QString& trackId, int scene) {
    if (!host_) return;
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    if (!id) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::Track* t = project.findTrack(*id);
    if (!t) return;
    std::vector<lpc::LiveCell> cells = t->cells;
    bool changed = false;
    for (lpc::LiveCell& c : cells)
        if (c.scene == scene && (c.pattern || !c.region.generator.empty())) {
            c.pattern = false;
            c.region.generator.clear();
            for (lpc::LiveTake& take : c.takes) take.region.generator.clear();
            changed = true;
        }
    if (changed) saveCells(trackId, cells);
    else emit notice("Only a Session Player or pattern cell can be converted");
}

void ProjectController::setCellSetting(const QString& trackId, int scene, const QString& key, const QVariant& value) {
    if (!host_) return;
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    if (!id) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::Track* t = project.findTrack(*id);
    if (!t) return;
    std::vector<lpc::LiveCell> cells = t->cells;
    for (lpc::LiveCell& c : cells) {
        if (c.scene != scene) continue;
        const std::string text = value.toString().toStdString();
        const lpc::Ticks ticks = toTicks(std::max(0.0, value.toDouble()));
        if (key == "loop") c.loop = value.toBool();
        else if (key == "name") c.name = text.substr(0, 60);
        else if (key == "startBeats") c.startPos = std::min(ticks, c.region.length - 1);
        else if (key == "loopStartBeats") {
            c.loopStart = std::min(ticks, c.region.length - 1);
            if (c.loopLength > 0) c.loopLength = std::min(c.loopLength, c.region.length - c.loopStart);
        } else if (key == "loopLengthBeats") c.loopLength = std::min(ticks, c.region.length - c.loopStart);
        else if (key == "lengthBeats") {
            const lpc::Ticks length = std::max<lpc::Ticks>(lpc::kPPQ / 4, ticks);
            c.region.length = length;
            for (lpc::LiveTake& take : c.takes) take.region.length = length;
            c.startPos = std::min(c.startPos, length - 1);
            c.loopStart = std::min(c.loopStart, length - 1);
            if (c.loopLength > 0) c.loopLength = std::min(c.loopLength, length - c.loopStart);
        } else if (key == "trigger") c.trigger = text;
        else if (key == "playFrom") c.playFrom = text;
        else if (key == "quantize") c.quantize = text;
        else if (key == "qLoopStart") c.qLoopStart = text;
        else if (key == "recording") c.recording = text;
        else if (key == "recLength") c.recLength = text;
        else if (key == "atRecEnd") c.atRecEnd = text;
        else return;
        saveCells(trackId, cells);
        return;
    }
}

// A copy of the cell's region in the Tracks area, at the playhead (the cell's start and loop settings do not go with it).
void ProjectController::copyCellToTracks(const QString& trackId, int scene) {
    if (!host_) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::LiveCell* c = findLiveCell(project, trackId, scene);
    if (!c) return;
    lpc::Region r = c->region;
    r.id = newUuid();
    r.start = toTicks(positionBeats_);
    r.loopLength = 0;
    sendCommand({{"type", "add_region"}, {"trackId", trackId.toStdString()}, {"index", -1}, {"region", nlohmann::json(r)}});
}

void ProjectController::copySceneToTracks(int scene) {
    if (!host_) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    nlohmann::json commands = nlohmann::json::array();
    for (const lpc::Track& t : project.tracks)
        for (const lpc::LiveCell& c : t.cells)
            if (c.scene == scene) {
                lpc::Region r = c.region;
                r.id = newUuid();
                r.start = toTicks(positionBeats_);
                r.loopLength = 0;
                commands.push_back({{"type", "add_region"}, {"trackId", t.id.toString()}, {"index", -1}, {"region", nlohmann::json(r)}});
            }
    if (commands.empty()) return;
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

// Edit > Copy to Live Loops Scene: the selected regions go to one cell each in the first empty scene; with nothing selected the cycle's span does when the cycle
// is on, and without it every section of the Arrangement track gets a scene of its own.
void ProjectController::copyToLiveLoopsScene() {
    if (!host_) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    std::map<lpc::Uuid, std::vector<lpc::LiveCell>> added;
    int scene = lpc::firstEmptyScene(project);
    const auto cellOf = [&](const lpc::Track& t, const std::vector<const lpc::Region*>& regions, lpc::Ticks from, lpc::Ticks to, int at) {
        if (auto region = lpc::cellFromRegions(project, t, regions, from, to, newUuid())) {
            lpc::LiveCell cell;
            cell.id = newUuid();
            cell.scene = at;
            cell.name = region->name;
            cell.region = std::move(*region);
            cell.region.loopLength = 0;
            if (!cell.region.generator.empty() || true) added[t.id].push_back(std::move(cell));
        }
    };
    const auto allOf = [](const lpc::Track& t) {
        std::vector<const lpc::Region*> all;
        for (const lpc::Region& r : t.regions) all.push_back(&r);
        return all;
    };
    const auto holds = [](const lpc::Track& t) { return t.kind == lpc::TrackKind::Audio || t.kind == lpc::TrackKind::Instrument || t.kind == lpc::TrackKind::Midi; };
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
        const lpc::Ticks from = toTicks(loopStartBeats_), to = toTicks(loopEndBeats_);
        for (const lpc::Track& t : project.tracks)
            if (holds(t)) cellOf(t, allOf(t), from, to, scene);
        any = true;
    } else if (!project.arrangement.empty()) {
        lpc::Ticks end = 0;
        for (const lpc::Track& t : project.tracks)
            for (const lpc::Region& r : t.regions) end = std::max(end, r.start + r.length);
        for (std::size_t i = 0; i < project.arrangement.size(); ++i) {
            const lpc::Ticks from = project.arrangement[i].tick, to = i + 1 < project.arrangement.size() ? project.arrangement[i + 1].tick : end;
            if (to <= from) continue;
            for (const lpc::Track& t : project.tracks)
                if (holds(t)) cellOf(t, allOf(t), from, to, scene);
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

// ---------------------------------------------------------------------------------------------------------------------------------------- takes

// The take folder of a cell: select, rename, delete and flatten take the one in `take`; copyTo, moveTo and unpack send takes to `argument` (newScene, nextRight, newTrack, nextBelow).
void ProjectController::takeAction(const QString& trackId, int scene, const QString& action, int take, const QString& argument) {
    if (!host_) return;
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    if (!id) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::Track* t = project.findTrack(*id);
    if (!t) return;
    std::vector<lpc::LiveCell> cells = t->cells;
    lpc::LiveCell* cell = nullptr;
    for (lpc::LiveCell& c : cells)
        if (c.scene == scene) cell = &c;
    if (!cell || cell->takes.empty()) return;
    const int n = static_cast<int>(cell->takes.size());
    const int which = take >= 0 && take < n ? take : cell->activeTake;
    const auto activate = [&](lpc::LiveCell& c) {
        c.activeTake = std::clamp(c.activeTake, 0, static_cast<int>(c.takes.size()) - 1);
        c.region = c.takes[static_cast<std::size_t>(c.activeTake)].region;
    };
    if (action == "select") {
        cell->activeTake = which;
        activate(*cell);
        saveCells(trackId, cells);
        return;
    }
    if (action == "rename") {
        cell->takes[static_cast<std::size_t>(which)].name = argument.left(60).toStdString();
        saveCells(trackId, cells);
        return;
    }
    if (action == "flatten") {
        cell->activeTake = which;
        activate(*cell);
        cell->takes.clear();
        cell->activeTake = 0;
        saveCells(trackId, cells);
        return;
    }
    if (action == "delete") {
        cell->takes.erase(cell->takes.begin() + which);
        if (cell->takes.empty()) {
            std::erase_if(cells, [&](const lpc::LiveCell& c) { return c.scene == scene; });  // the last take: the cell is empty
        } else {
            cell->activeTake = std::min(cell->activeTake, static_cast<int>(cell->takes.size()) - 1);
            activate(*cell);
        }
        saveCells(trackId, cells);
        return;
    }
    if (action != "copyTo" && action != "moveTo" && action != "unpack") return;
    // the places the takes go to
    struct Place {
        QString trackId;
        int scene;
        bool newTrack;
    };
    std::vector<lpc::Track> addedTracks;
    std::map<lpc::Uuid, std::vector<lpc::LiveCell>> byTrack;
    byTrack[*id] = cells;
    const auto hasCell = [&](const lpc::Uuid& tid, int sc) {
        const auto& list = byTrack.count(tid) ? byTrack[tid] : project.findTrack(tid)->cells;
        for (const lpc::LiveCell& c : list)
            if (c.scene == sc) return true;
        return false;
    };
    const auto rowsInOrder = [&] {
        std::vector<const lpc::Track*> rows;
        for (const lpc::Track& x : project.tracks)
            if (x.kind == lpc::TrackKind::Audio || x.kind == lpc::TrackKind::Instrument || x.kind == lpc::TrackKind::Midi) rows.push_back(&x);
        return rows;
    }();
    const QString kind = QString::fromLatin1(t->kind == lpc::TrackKind::Audio ? "audio" : "instrument");
    int nextScene = lpc::firstEmptyScene(project);
    int rightScene = scene + 1;
    std::size_t belowIndex = 0;
    for (std::size_t i = 0; i < rowsInOrder.size(); ++i)
        if (rowsInOrder[i]->id == *id) belowIndex = i + 1;
    const auto place = [&](lpc::Uuid& trackOut, int& sceneOut) {
        if (argument == "newScene") {
            while (hasCell(*id, nextScene)) ++nextScene;
            trackOut = *id;
            sceneOut = nextScene++;
            return true;
        }
        if (argument == "nextRight") {
            while (hasCell(*id, rightScene)) ++rightScene;
            trackOut = *id;
            sceneOut = rightScene++;
            return true;
        }
        if (argument == "nextBelow") {
            for (; belowIndex < rowsInOrder.size(); ++belowIndex) {
                const lpc::Track* below = rowsInOrder[belowIndex];
                const QString bk = QString::fromLatin1(below->kind == lpc::TrackKind::Audio ? "audio" : "instrument");
                if (sameFamily(bk, kind) && !hasCell(below->id, scene)) {
                    trackOut = below->id;
                    sceneOut = scene;
                    ++belowIndex;
                    return true;
                }
            }
            return false;
        }
        if (argument == "newTrack") {
            lpc::Track nt = *t;
            nt.id = newUuid();
            nt.name = t->name + " 2";
            nt.regions.clear();
            nt.cells.clear();
            nt.automation.clear();
            nt.alternatives.clear();
            nt.alternative.clear();
            nt.liveRuns.clear();
            nt.strip.inserts = t->strip.inserts;
            addedTracks.push_back(nt);
            byTrack[nt.id] = {};
            trackOut = nt.id;
            sceneOut = scene;
            return true;
        }
        return false;
    };
    const std::vector<int> sources = action == "unpack" ? [&] { std::vector<int> v; for (int i = 0; i < n; ++i) v.push_back(i); return v; }() : std::vector<int>{which};
    int moved = 0;
    for (const int idx : sources) {
        lpc::Uuid destTrack;
        int destScene = 0;
        if (!place(destTrack, destScene)) {
            emit notice("There is no empty cell for it there");
            break;
        }
        lpc::LiveCell copy = *cell;  // the settings go along
        copy.id = newUuid();
        copy.scene = destScene;
        copy.takes.clear();
        copy.activeTake = 0;
        copy.region = cell->takes[static_cast<std::size_t>(idx)].region;
        copy.region.id = newUuid();
        copy.name = cell->takes[static_cast<std::size_t>(idx)].name;
        byTrack[destTrack].push_back(std::move(copy));
        ++moved;
    }
    if (moved == 0) return;
    if (action != "copyTo") {  // moved or unpacked: they leave the folder
        lpc::LiveCell& source = [&]() -> lpc::LiveCell& {
            for (lpc::LiveCell& c : byTrack[*id])
                if (c.scene == scene) return c;
            return byTrack[*id].front();
        }();
        if (action == "unpack") {
            std::erase_if(byTrack[*id], [&](const lpc::LiveCell& c) { return c.scene == scene; });
        } else {
            source.takes.erase(source.takes.begin() + which);
            if (source.takes.empty()) std::erase_if(byTrack[*id], [&](const lpc::LiveCell& c) { return c.scene == scene; });
            else {
                source.activeTake = std::min(source.activeTake, static_cast<int>(source.takes.size()) - 1);
                source.region = source.takes[static_cast<std::size_t>(source.activeTake)].region;
            }
        }
    }
    nlohmann::json commands = nlohmann::json::array();
    for (const lpc::Track& nt : addedTracks) commands.push_back(lpc::makeAddTrack(nt)->toJson());
    for (auto& [tid, list] : byTrack) {
        const lpc::Track* existing = project.findTrack(tid);
        if (existing && existing->cells == list) continue;
        commands.push_back(lpc::makeSetLiveCells(tid, list)->toJson());
    }
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

// ---------------------------------------------------------------------------------------------------------------------------------------- recording

// Record Into Cell: the track records from the next quantize start (a bar at most) for the Rec-Length of the cell; what was recorded leaves the Tracks area and becomes the cell
// (Replace), a take of its folder (Takes) or is merged into it (Merge, instruments). Pressed again while the length is automatic, it sets the end to the nearest bar or beat.
void ProjectController::recordIntoCell(const QString& trackId, int scene) {
    if (!host_) return;
    for (CellRecording& r : cellRec_)
        if (r.trackId == trackId && r.state == CellRecording::Recording && r.end < 0) {  // the length was automatic: this press ends it
            const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
            const lpc::LiveCell* c = findLiveCell(project, trackId, r.scene);
            const lpc::Ticks unit = c && c->recLength == "beats" ? lpc::kPPQ * 4 / std::max(1, beatUnit_) : toTicks(barBeats());
            const lpc::Ticks pos = toTicks(positionBeats_);
            const lpc::Ticks at = r.start + std::max<lpc::Ticks>(unit, ((pos - r.start + unit / 2) / unit) * unit);
            r.end = at;
            return;
        }
    if (!trackToggles_.value(QStringLiteral("track.recordArm")).contains(trackId)) {
        emit notice("Arm the track (R) to record into a cell");
        return;
    }
    if (recording_) {
        emit notice("A recording is already running");
        return;
    }
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::LiveCell* c = findLiveCell(project, trackId, scene);
    CellRecording rec;
    rec.trackId = trackId;
    rec.scene = scene;
    const lpc::Ticks bar = toTicks(barBeats());
    const lpc::Ticks pos = toTicks(positionBeats_);
    lpc::Ticks q = c ? liveQuantizeTicks(QString::fromStdString(c->quantize), -1) : liveQuantizeTicks(QStringLiteral("global"), -1);
    if (q < 0 || q > bar) q = q == -1 ? -1 : bar;  // Off records at once; a longer quantize start still starts at the next bar
    rec.start = !playing_ || q == -1 ? pos : lpc::launchTick(pos, q);
    const QString mode = c ? QString::fromStdString(c->recLength) : QStringLiteral("bars");
    rec.length = c && mode == "cell" ? c->region.length : toTicks(std::max(1, std::clamp(projectSetting("liveDefaultBars", "4").toInt(), 1, 64)) * barBeats());
    rec.end = mode == "cell" || !c ? rec.start + rec.length : -1;
    for (const RegionRow& r : regionRows_)
        if (r.trackId == trackId) rec.regionsBefore << r.id;
    cellRec_.push_back(rec);
    if (!playing_) play();
    lastLiveSignature_.clear();
    updateLive();
}

void ProjectController::updateCellRecording() {
    if (cellRec_.empty() || !host_) return;
    const lpc::Ticks pos = toTicks(positionBeats_);
    CellRecording& rec = cellRec_.front();
    if (rec.state == CellRecording::Waiting) {
        if (!playing_ || pos < rec.start) return;
        const bool punch = allowQuickPunch_;
        allowQuickPunch_ = true;  // a cell records from the middle of a playing project whatever Quick Punch-In says
        startRecording();
        allowQuickPunch_ = punch;
        if (!recording_) {
            cellRec_.clear();
            return;
        }
        rec.state = CellRecording::Recording;
        lastLiveSignature_.clear();
    } else if (rec.state == CellRecording::Recording) {
        if (!recording_) {  // the recording was stopped by the user
            if (rec.end < 0 || rec.end > pos) rec.end = std::max(pos, rec.start + 1);
            rec.state = CellRecording::Finishing;
        } else if (rec.end >= 0 && pos >= rec.end) {
            sendRecordingStop();  // the transport keeps playing
            rec.state = CellRecording::Finishing;
        }
    }
    if (rec.state == CellRecording::Finishing && !recording_) {
        const CellRecording done = rec;
        cellRec_.clear();
        QPointer<ProjectController> self(this);
        QTimer::singleShot(450, this, [self, done] {  // the take's regions are made by commands: they have reached the project a moment later
            if (self) self->finishCellPass(done);
        });
    }
}

void ProjectController::finishCellPass(const CellRecording& rec) {
    if (!host_) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const auto id = lpc::Uuid::parse(rec.trackId.toStdString());
    const lpc::Track* t = id ? project.findTrack(*id) : nullptr;
    if (!t) return;
    std::vector<const lpc::Region*> fresh;
    for (const lpc::Region& r : t->regions)
        if (!rec.regionsBefore.contains(QString::fromStdString(r.id.toString()))) fresh.push_back(&r);
    lpc::Ticks end = rec.end;
    if (end <= rec.start) end = rec.start + std::max<lpc::Ticks>(rec.length, 1);
    auto region = fresh.empty() ? std::nullopt : lpc::cellFromRegions(project, *t, fresh, rec.start, end, newUuid());
    if (!region) {
        emit notice("Nothing was recorded");
        return;
    }
    region->loopLength = 0;
    std::vector<lpc::LiveCell> cells = t->cells;
    lpc::LiveCell* cell = nullptr;
    for (lpc::LiveCell& c : cells)
        if (c.scene == rec.scene) cell = &c;
    if (!cell) {
        lpc::LiveCell nc;
        nc.id = newUuid();
        nc.scene = rec.scene;
        nc.region = *region;
        cells.push_back(std::move(nc));
        cell = &cells.back();
    } else {
        const bool midi = t->kind != lpc::TrackKind::Audio;
        if (cell->recording == "merge" && midi) {
            for (const lpc::MidiNote& n : region->notes) cell->region.notes.push_back(n);
            for (const lpc::MidiControl& c : region->controls) cell->region.controls.push_back(c);
            std::stable_sort(cell->region.notes.begin(), cell->region.notes.end(), [](const lpc::MidiNote& a, const lpc::MidiNote& b) { return a.start < b.start; });
            std::stable_sort(cell->region.controls.begin(), cell->region.controls.end(), [](const lpc::MidiControl& a, const lpc::MidiControl& b) { return a.tick < b.tick; });
            cell->region.length = std::max(cell->region.length, region->length);
        } else if (cell->recording == "takes") {
            if (cell->takes.empty()) cell->takes.push_back({"Take 1", cell->region});
            region->id = newUuid();
            cell->takes.push_back({"Take " + std::to_string(cell->takes.size() + 1), *region});
            cell->activeTake = static_cast<int>(cell->takes.size()) - 1;
            cell->region = *region;
        } else {
            const lpc::Uuid keep = cell->region.id;
            cell->region = *region;
            if (!cell->takes.empty()) cell->takes[static_cast<std::size_t>(cell->activeTake)].region = *region;
            (void)keep;
        }
    }
    const QString trackId = rec.trackId;
    const int scene = rec.scene;
    const bool play = cell->atRecEnd == "play";
    const bool again = cell->atRecEnd == "continue";
    nlohmann::json commands = nlohmann::json::array();
    for (const lpc::Region* r : fresh) commands.push_back({{"type", "remove_region"}, {"regionId", r->id.toString()}});  // the pass leaves the Tracks area
    commands.push_back(lpc::makeSetLiveCells(*id, cells)->toJson());
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
    QPointer<ProjectController> self(this);
    if (play) {
        QTimer::singleShot(250, this, [self, trackId, scene] {
            if (self) self->launchCellAt(trackId, scene, -3, false);
        });
    } else if (again) {
        QTimer::singleShot(250, this, [self, trackId, scene] {
            if (self) self->recordIntoCell(trackId, scene);
        });
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------- audio files, loops, other grids

// Add Audio File: the file is imported on the track (as the project's settings say) and the region it makes becomes the cell.
void ProjectController::addAudioFileToCell(const QUrl& file, const QString& trackId, int scene) {
    const TrackRow* track = tracks_.find(trackId);
    if (!host_ || !track || track->kind != QLatin1String("audio")) {
        emit notice("An audio file goes in a cell of an audio track");
        return;
    }
    PendingCellFile job;
    job.trackId = trackId;
    job.scene = scene;
    for (const RegionRow& r : regionRows_)
        if (r.trackId == trackId) job.regionsBefore << r.id;
    pendingCellFiles_.push_back(job);
    importAudioFiles({file}, trackId, std::max(0.0, positionBeats_), projectSetting("copyAudioFiles", "1") == QLatin1String("0"));
}

void ProjectController::updatePendingCellFiles() {
    if (pendingCellFiles_.empty() || !host_) return;
    PendingCellFile& job = pendingCellFiles_.front();
    QStringList fresh;
    for (const RegionRow& r : regionRows_)
        if (r.trackId == job.trackId && !job.regionsBefore.contains(r.id)) fresh << r.id;
    if (fresh.isEmpty()) {
        if (++job.waited > 1200) pendingCellFiles_.erase(pendingCellFiles_.begin());  // the import did not come to anything
        return;
    }
    const PendingCellFile done = job;
    pendingCellFiles_.erase(pendingCellFiles_.begin());
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const auto id = lpc::Uuid::parse(done.trackId.toStdString());
    const lpc::Track* t = id ? project.findTrack(*id) : nullptr;
    if (!t) return;
    std::vector<const lpc::Region*> imported;
    for (const lpc::Region& r : t->regions)
        if (fresh.contains(QString::fromStdString(r.id.toString()))) imported.push_back(&r);
    if (imported.empty()) return;
    const lpc::Region& src = *imported.front();
    auto region = lpc::cellFromRegions(project, *t, {&src}, src.start, src.start + src.length, newUuid());
    if (!region) return;
    region->loopLength = 0;
    std::vector<lpc::LiveCell> cells;
    for (const lpc::LiveCell& c : t->cells)
        if (c.scene != done.scene) cells.push_back(c);
    lpc::LiveCell cell;
    cell.id = newUuid();
    cell.scene = done.scene;
    cell.region = *region;
    cell.name = QFileInfo(QString::fromStdString(src.name)).completeBaseName().toStdString();
    cells.push_back(std::move(cell));
    nlohmann::json commands = nlohmann::json::array();
    commands.push_back({{"type", "remove_region"}, {"regionId", src.id.toString()}});  // the file lives in the cell, not in the Tracks area
    commands.push_back(lpc::makeSetLiveCells(*id, cells)->toJson());
    sendCommand({{"type", "transaction"}, {"commands", commands}});
}

// Extract Loops: the tempo and the beats of the cell's sound are analysed and the cell gets the start, the loop start and the loop length that suit it.
void ProjectController::extractLoops(const QString& trackId, int scene) {
    if (!host_) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::LiveCell* c = findLiveCell(project, trackId, scene);
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    const lpc::Track* t = id ? project.findTrack(*id) : nullptr;
    const lpc::MediaItem* media = c ? project.findMedia(c->region.mediaId) : nullptr;
    if (!c || !t || t->kind != lpc::TrackKind::Audio || !media) {
        emit notice("Extract Loops works on an audio cell");
        return;
    }
    const std::filesystem::path path = dir_ / std::filesystem::path(std::u8string(media->path.begin(), media->path.end()));
    const std::int64_t skip = c->region.sourceOffsetFrames;
    const lpc::Uuid cellId = c->id;
    emit notice("Looking for the loop…");
    QPointer<ProjectController> self(this);
    (void)QtConcurrent::run([self, project, path, skip, cellId, trackId, scene] {
        std::optional<lpc::LoopSuggestion> loop;
        try {
            lpc::WavData audio = lpc::readWav(path);
            if (skip > 0 && skip < audio.frames()) audio.samples.erase(audio.samples.begin(), audio.samples.begin() + static_cast<std::ptrdiff_t>(skip) * audio.channels);
            loop = lpc::suggestLoop(audio, project);
        } catch (const std::exception&) {
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, loop, trackId, scene] {
            if (!self) return;
            if (!loop) {
                emit self->notice("No steady pulse was found in this sound");
                return;
            }
            const double q = 1.0 / lpc::kPPQ;
            self->setCellSetting(trackId, scene, "startBeats", loop->start * q);
            self->setCellSetting(trackId, scene, "loopStartBeats", loop->start * q);
            self->setCellSetting(trackId, scene, "loopLengthBeats", loop->length * q);
            emit self->notice(QString("Loop of %1 bar%2 found").arg(loop->bars).arg(loop->bars == 1 ? "" : "s"));
        }, Qt::QueuedConnection);
    });
}

// File > Import > Live Loops Grid: the tracks of another project that have cells come in with their cells, and the audio is copied into this project.
void ProjectController::importLiveLoopsGrid(const QUrl& projectFolder) {
    if (!host_) return;
    const std::filesystem::path other = toPath(projectFolder);
    try {
        const lpc::Project source = lpc::loadProject(other);
        const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
        const lpc::ImportedGrid grid = lpc::importLiveLoopsGrid(project, dir_, source, other);
        if (grid.tracks.empty()) {
            emit notice("That project has no Live Loops cells");
            return;
        }
        nlohmann::json commands = nlohmann::json::array();
        for (const lpc::MediaItem& m : grid.media) commands.push_back(lpc::makeAddMedia(m)->toJson());
        for (const lpc::Track& t : grid.tracks) commands.push_back(lpc::makeAddTrack(t)->toJson());
        sendCommand({{"type", "transaction"}, {"commands", commands}});
        emit notice(QString("%1 track%2 with cells imported").arg(grid.tracks.size()).arg(grid.tracks.size() == 1 ? "" : "s"));
    } catch (const std::exception& e) {
        setError(QString("Cannot import the grid: ") + QString::fromUtf8(e.what()));
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------- performance

void ProjectController::setLivePerformance(bool on) {
    if (livePerformance_ == on) return;
    livePerformance_ = on;
    ++liveRevision_;
    emit liveLoopsChanged();
}

// What was played in the grid while the project was recording becomes regions in the Tracks area.
void ProjectController::finishPerformance() {
    if (!perfRecording_ || !host_) return;
    perfRecording_ = false;
    const lpc::Ticks endTick = toTicks(positionBeats_);
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    nlohmann::json commands = nlohmann::json::array();
    for (const auto& [trackId, row] : perfLog_) {
        const auto tid = lpc::Uuid::parse(trackId.toStdString());
        const lpc::Track* t = tid ? project.findTrack(*tid) : nullptr;
        if (!t) continue;
        lpc::LiveRun run;
        run.start = std::max(row.start, perfStart_);
        run.offset = row.offset;
        if (row.start < perfStart_)
            for (const lpc::LiveCell& c : t->cells)
                if (QString::fromStdString(c.id.toString()) == row.cellId) run.offset = lpc::cellPositionAt(c, row.offset, perfStart_ - row.start);
        run.end = row.end >= 0 ? std::min(row.end, endTick) : endTick;
        if (run.end <= run.start) continue;
        for (const lpc::LiveCell& c : t->cells) {
            if (QString::fromStdString(c.id.toString()) != row.cellId) continue;
            run.cellId = c.id;
            for (lpc::Region& r : lpc::playedRegions(project, *t, run, c)) {
                r.id = newUuid();
                r.loopLength = r.loopLength;  // a looped piece keeps its loop
                commands.push_back({{"type", "add_region"}, {"trackId", trackId.toStdString()}, {"index", -1}, {"region", nlohmann::json(r)}});
            }
        }
    }
    perfLog_.clear();
    if (commands.empty()) {
        emit notice("No cell was played");
    } else {
        sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
        emit notice("The performance was recorded as regions");
    }
    ++liveRevision_;
    emit liveLoopsChanged();
}

}  // namespace jad
