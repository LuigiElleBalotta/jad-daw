// The Edit menu of the Tracks area on regions: copy, cut, paste, duplicate, mute, the Select, Trim, Length and Move commands.
// Each change is one command (or one transaction) and so one undo step, like the Core's own commands.
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

#include "bridge/project_controller.h"
#include "lpc/model_json.h"
#include "lpc/project_host.h"
#include "lpc/validation.h"

namespace jad {

namespace {
constexpr double kMaxBeatsEdit = static_cast<double>(lpc::kMaxPosition) / lpc::kPPQ;
constexpr double kMinLength = 1.0 / 16.0;
constexpr double kEps = 1e-6;

bool fitsTrack(const RegionRow& region, const QString& trackKind) {
    return region.audio ? trackKind == "audio" : (trackKind == "instrument" || trackKind == "midi");
}
}  // namespace

std::vector<const RegionRow*> ProjectController::selectedRegionRows() const {
    std::vector<const RegionRow*> out;
    for (const QString& id : selectedRegions_)
        if (const RegionRow* r = regions_.find(id)) out.push_back(r);
    std::sort(out.begin(), out.end(), [](const RegionRow* a, const RegionRow* b) { return a->startBeats < b->startBeats; });
    return out;
}

void ProjectController::copySelectedRegions() {
    const auto rows = selectedRegionRows();
    if (rows.empty()) return;
    clipboard_.clear();
    for (const RegionRow* r : rows) clipboard_.push_back({*r, r->json});
}

void ProjectController::cutSelectedRegions() {
    copySelectedRegions();
    if (!clipboard_.empty()) deleteRegions(selectedRegions_);
}

// Puts the clipboard `offsetBeats` later than where it came from. A region goes on the selected track when all of the copied
// regions come from one track and the selected track can hold them; otherwise every region goes back on its own track.
void ProjectController::pasteClipboard(double offsetBeats, bool keepTrack) {
    if (!host_ || clipboard_.empty()) return;
    bool oneTrack = true;
    for (const ClipRegion& c : clipboard_) oneTrack = oneTrack && c.row.trackId == clipboard_.front().row.trackId;
    QString target;
    if (!keepTrack && oneTrack && !selectedTracks_.isEmpty()) {
        for (const TrackRow& t : allRows_)
            if (t.id == selectedTracks_.first() && !t.master && fitsTrack(clipboard_.front().row, t.kind)) target = t.id;
    }
    nlohmann::json commands = nlohmann::json::array();
    QStringList created;
    for (const ClipRegion& c : clipboard_) {
        const QString trackId = target.isEmpty() ? c.row.trackId : target;
        bool exists = false;
        for (const TrackRow& t : allRows_) exists = exists || (t.id == trackId && !t.master);
        if (!exists) continue;
        nlohmann::json region = nlohmann::json::parse(c.json, nullptr, false);
        if (region.is_discarded()) continue;
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        region["id"] = id.toStdString();
        region["start"] = regionPosition(c.row, std::clamp(c.row.startBeats + offsetBeats, 0.0, kMaxBeatsEdit));
        commands.push_back({{"type", "add_region"}, {"trackId", trackId.toStdString()}, {"index", -1}, {"region", region}});
        created << id;
    }
    if (commands.empty()) return;
    // the pasted regions become the selection once the project has them
    pendingRegionSelection_ = created;
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::pasteRegions(bool atOriginalPosition) {
    if (clipboard_.empty()) return;
    double first = clipboard_.front().row.startBeats;
    for (const ClipRegion& c : clipboard_) first = std::min(first, c.row.startBeats);
    pasteClipboard(atOriginalPosition ? 0.0 : positionBeats_ - first, atOriginalPosition);
}

// Duplicate: a copy of the selection right after it (the clipboard is left alone).
void ProjectController::duplicateSelectedRegions() {
    const auto rows = selectedRegionRows();
    if (rows.empty()) return;
    double first = rows.front()->startBeats, last = 0.0;
    for (const RegionRow* r : rows) last = std::max(last, r->startBeats + r->lengthBeats);
    const auto saved = clipboard_;
    clipboard_.clear();
    for (const RegionRow* r : rows) clipboard_.push_back({*r, r->json});
    pasteClipboard(last - first, true);
    clipboard_ = saved;
}

void ProjectController::toggleMuteSelectedRegions() {
    const auto rows = selectedRegionRows();
    if (rows.empty()) return;
    bool anyUnmuted = false;
    for (const RegionRow* r : rows) anyUnmuted = anyUnmuted || !mutedRegions_.contains(r->id);
    for (const RegionRow* r : rows) {
        if (anyUnmuted) mutedRegions_.insert(r->id);
        else mutedRegions_.remove(r->id);
    }
    for (RegionRow& r : regionRows_) r.muted = mutedRegions_.contains(r.id);
    regions_.reset(regionRows_);
}

void ProjectController::selectFollowingRegions(bool sameTrackOnly) {
    const auto rows = selectedRegionRows();
    if (rows.empty()) return;
    const RegionRow* from = rows.front();
    QStringList ids;
    for (const RegionRow& r : regionRows_)
        if (r.startBeats >= from->startBeats - kEps && (!sameTrackOnly || r.trackId == from->trackId)) ids << r.id;
    selectRegions(ids, "replace");
}

void ProjectController::selectOverlappedRegions() {
    QStringList ids;
    for (const RegionRow& a : regionRows_)
        for (const RegionRow& b : regionRows_)
            if (&a != &b && a.trackId == b.trackId && a.startBeats < b.startBeats + b.lengthBeats - kEps &&
                b.startBeats < a.startBeats + a.lengthBeats - kEps) {
                ids << a.id;
                break;
            }
    selectRegions(ids, "replace");
}

void ProjectController::selectSameColoredRegions() {
    const auto rows = selectedRegionRows();
    if (rows.empty()) return;
    QStringList colors;
    for (const RegionRow* r : rows) colors << r->color;
    QStringList ids;
    for (const RegionRow& r : regionRows_)
        if (colors.contains(r.color)) ids << r.id;
    selectRegions(ids, "replace");
}

void ProjectController::selectMutedRegions() {
    QStringList ids;
    for (const RegionRow& r : regionRows_)
        if (r.muted) ids << r.id;
    selectRegions(ids, "replace");
}

void ProjectController::selectEmptyRegions() {  // a MIDI region without notes
    QStringList ids;
    for (const RegionRow& r : regionRows_) {
        if (r.audio) continue;
        const nlohmann::json j = nlohmann::json::parse(r.json, nullptr, false);
        if (!j.is_discarded() && j.contains("notes") && j["notes"].empty()) ids << r.id;
    }
    selectRegions(ids, "replace");
}

void ProjectController::invertRegionSelection() {
    QStringList ids;
    for (const RegionRow& r : regionRows_)
        if (!selectedRegions_.contains(r.id)) ids << r.id;
    selectRegions(ids, "replace");
}

void ProjectController::selectNeighbourRegion(int direction) {
    const auto rows = selectedRegionRows();
    if (rows.empty() || direction == 0) return;
    const RegionRow* from = direction > 0 ? rows.back() : rows.front();
    const RegionRow* best = nullptr;
    for (const RegionRow& r : regionRows_) {
        if (r.trackId != from->trackId || r.id == from->id) continue;
        const bool ahead = direction > 0 ? r.startBeats > from->startBeats + kEps : r.startBeats < from->startBeats - kEps;
        if (!ahead) continue;
        if (!best || (direction > 0 ? r.startBeats < best->startBeats : r.startBeats > best->startBeats)) best = &r;
    }
    if (best) selectRegion(best->id, "replace");
}

// Regions of one track, earliest first.
static std::vector<const RegionRow*> onTrack(const std::vector<RegionRow>& all, const QString& trackId) {
    std::vector<const RegionRow*> out;
    for (const RegionRow& r : all)
        if (r.trackId == trackId) out.push_back(&r);
    std::sort(out.begin(), out.end(), [](const RegionRow* a, const RegionRow* b) { return a->startBeats < b->startBeats; });
    return out;
}

// Trim > Remove Overlaps. Both regions of an overlapping pair selected: the earlier one ends where the later starts. Only the
// earlier one selected: the later one starts where the earlier ends.
void ProjectController::removeOverlaps() {
    if (!host_ || selectedRegions_.isEmpty()) return;
    nlohmann::json commands = nlohmann::json::array();
    QSet<QString> tracks;
    for (const RegionRow* r : selectedRegionRows()) tracks.insert(r->trackId);
    for (const QString& trackId : tracks) {
        const auto list = onTrack(regionRows_, trackId);
        for (std::size_t i = 0; i + 1 < list.size(); ++i) {
            const RegionRow* a = list[i];
            const RegionRow* b = list[i + 1];
            const bool aSel = selectedRegions_.contains(a->id), bSel = selectedRegions_.contains(b->id);
            if (!aSel || a->startBeats + a->lengthBeats <= b->startBeats + kEps) continue;
            if (bSel) {
                const double len = b->startBeats - a->startBeats;
                if (len >= kMinLength) commands.push_back(resizeCommand(*a, a->startBeats, len));
            } else {
                const double end = b->startBeats + b->lengthBeats, start = a->startBeats + a->lengthBeats;
                if (end - start >= kMinLength) commands.push_back(resizeCommand(*b, start, end - start));
            }
        }
    }
    if (!commands.empty()) sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

// Trim > Region End to Next Region: each selected region is lengthened to the start of the next region on its track.
void ProjectController::regionEndToNextRegion() {
    if (!host_) return;
    nlohmann::json commands = nlohmann::json::array();
    for (const RegionRow* r : selectedRegionRows()) {
        const RegionRow* next = nullptr;
        for (const RegionRow& o : regionRows_)
            if (o.trackId == r->trackId && o.startBeats > r->startBeats + kEps && (!next || o.startBeats < next->startBeats)) next = &o;
        if (next && next->startBeats - r->startBeats >= kMinLength && std::abs(next->startBeats - (r->startBeats + r->lengthBeats)) > kEps)
            commands.push_back(resizeCommand(*r, r->startBeats, next->startBeats - r->startBeats));
    }
    if (!commands.empty()) sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::halveSelectedRegions() {
    if (!host_) return;
    nlohmann::json commands = nlohmann::json::array();
    for (const RegionRow* r : selectedRegionRows())
        if (r->lengthBeats / 2 >= kMinLength) commands.push_back(resizeCommand(*r, r->startBeats, r->lengthBeats / 2));
    if (!commands.empty()) sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::doubleSelectedRegions() {
    if (!host_) return;
    nlohmann::json commands = nlohmann::json::array();
    for (const RegionRow* r : selectedRegionRows()) commands.push_back(resizeCommand(*r, r->startBeats, r->lengthBeats * 2));
    if (!commands.empty()) sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::nudgeSelectedRegions(int direction) {
    if (!host_ || direction == 0) return;
    nlohmann::json commands = nlohmann::json::array();
    const double delta = direction * nudgeBeats_;
    for (const RegionRow* r : selectedRegionRows()) commands.push_back(moveCommand(*r, std::max(0.0, r->startBeats + delta)));
    if (!commands.empty()) sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::setNudgeBeats(double beats) {
    if (!std::isfinite(beats) || beats <= 0 || std::abs(beats - nudgeBeats_) < kEps) return;
    nudgeBeats_ = beats;
    emit nudgeChanged();
}

}  // namespace jad
