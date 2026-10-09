// The Edit menu of the Tracks area on regions: copy, cut, paste, duplicate, mute, the Select, Trim, Length and Move commands.
// Each change is one command (or one transaction) and so one undo step, like the Core's own commands.
#include <QTimer>
#include <QtConcurrent>
#include <QPointer>
#include <QUuid>

#include <algorithm>
#include <filesystem>
#include <cmath>
#include <nlohmann/json.hpp>

#include "bridge/project_controller.h"
#include "lpc/model_json.h"
#include "lpc/media_store.h"
#include "lpc/wav.h"
#include "lpc/offline_render.h"
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

QVariantList ProjectController::regionNotes(const QString& regionId) const {
    QVariantList out;
    const RegionRow* row = regions_.find(regionId);
    if (!row || row->audio) return out;
    const nlohmann::json j = nlohmann::json::parse(row->json, nullptr, false);
    if (j.is_discarded() || !j.contains("notes") || !j["notes"].is_array()) return out;
    for (const auto& n : j["notes"]) {
        out.append(QVariantMap{{"start", n.value("start", 0) / static_cast<double>(lpc::kPPQ)},
                               {"length", n.value("length", 0) / static_cast<double>(lpc::kPPQ)},
                               {"note", n.value("note", 60)},
                               {"velocity", n.value("velocity", 100)},
                               {"muted", n.value("muted", false)}});
    }
    return out;
}

void ProjectController::setRegionNotes(const QString& regionId, const QVariantList& notes) {
    if (!host_) return;
    const RegionRow* row = regions_.find(regionId);
    if (!row || row->audio) return;
    nlohmann::json region = nlohmann::json::parse(row->json, nullptr, false);
    if (region.is_discarded()) return;
    nlohmann::json list = nlohmann::json::array();
    for (const QVariant& v : notes) {
        const QVariantMap m = v.toMap();
        const double start = m.value("start").toDouble(), length = m.value("length").toDouble();
        if (!std::isfinite(start) || !std::isfinite(length)) continue;
        list.push_back({{"start", static_cast<std::int64_t>(std::llround(std::clamp(start, 0.0, kMaxBeatsEdit) * lpc::kPPQ))},
                        {"length", std::max<std::int64_t>(1, static_cast<std::int64_t>(std::llround(std::clamp(length, 0.0, kMaxBeatsEdit) * lpc::kPPQ)))},
                        {"note", std::clamp(m.value("note").toInt(), 0, 127)},
                        {"velocity", std::clamp(m.value("velocity", 100).toInt(), 1, 127)}});
        if (m.value("muted").toBool()) list.back()["muted"] = true;
    }
    region["notes"] = list;
    sendCommand({{"type", "replace_region"}, {"region", region}});
}

QVariantMap ProjectController::regionInfo(const QString& regionId) const {
    const RegionRow* row = regions_.find(regionId);
    if (!row) return {{"found", false}};
    QString trackName;
    for (const TrackRow& t : allRows_)
        if (t.id == row->trackId) trackName = t.name;
    return {{"found", true}, {"trackId", row->trackId}, {"trackName", trackName}, {"startBeats", row->startBeats},
            {"lengthBeats", row->lengthBeats}, {"audio", row->audio}, {"color", row->color}};
}

void ProjectController::beginGesture() {
    if (!host_) return;
    if (!liveWired_) {
        liveWired_ = true;
        liveTimer_.setInterval(33);
        connect(&liveTimer_, &QTimer::timeout, this, [this] {
            if (liveGain_.isEmpty() && livePan_.isEmpty()) liveTimer_.stop();
            else flushLive();
        });
    }
    liveGain_.clear();
    livePan_.clear();
    host_->beginGesture();
}

void ProjectController::flushLive() {
    const auto gains = liveGain_;
    const auto pans = livePan_;
    liveGain_.clear();
    livePan_.clear();
    for (auto it = gains.begin(); it != gains.end(); ++it) setStripField(it.key(), "gainDb", std::clamp(it.value(), -96.0, 24.0));
    for (auto it = pans.begin(); it != pans.end(); ++it) setStripField(it.key(), "pan", std::clamp(it.value(), -1.0, 1.0));
}

void ProjectController::endGesture() {
    liveTimer_.stop();
    liveGain_.clear();  // the final value follows as an ordinary command
    livePan_.clear();
    if (host_) host_->endGesture();
}

void ProjectController::setGainLive(const QString& trackId, double db) {
    if (!host_ || !std::isfinite(db)) return;
    liveGain_[trackId] = db;
    if (!liveTimer_.isActive()) {
        flushLive();
        liveTimer_.start();
    }
}

void ProjectController::setPanLive(const QString& trackId, double pan) {
    if (!host_ || !std::isfinite(pan)) return;
    livePan_[trackId] = pan;
    if (!liveTimer_.isActive()) {
        flushLive();
        liveTimer_.start();
    }
}

void ProjectController::setNudgeBeats(double beats) {
    if (!std::isfinite(beats) || beats <= 0 || std::abs(beats - nudgeBeats_) < kEps) return;
    nudgeBeats_ = beats;
    emit nudgeChanged();
}

// ---------------------------------------------------------------- global tracks: markers, tempo, signature

void ProjectController::setGlobalTracksVisible(bool on) {
    if (on == globalTracksVisible_) return;
    globalTracksVisible_ = on;
    emit globalTracksVisibleChanged();
}

QVariantList ProjectController::markers() const {
    QVariantList out;
    for (const MarkerRow& m : markerRows_) out.append(QVariantMap{{"id", m.id}, {"beats", m.beats}, {"name", m.name}});
    return out;
}

QVariantList ProjectController::tempoEvents() const {
    QVariantList out;
    for (const auto& e : tempoMap_.tempos()) out.append(QVariantMap{{"beats", static_cast<double>(e.tick) / lpc::kPPQ}, {"bpm", e.bpm}});
    return out;
}

QVariantList ProjectController::signatureEvents() const {
    QVariantList out;
    for (const auto& e : tempoMap_.signatures())
        out.append(QVariantMap{{"beats", static_cast<double>(e.tick) / lpc::kPPQ}, {"numerator", e.numerator}, {"denominator", e.denominator}});
    return out;
}

void ProjectController::sendMarkers(const std::vector<MarkerRow>& rows) {
    nlohmann::json list = nlohmann::json::array();
    for (const MarkerRow& m : rows)
        list.push_back({{"id", m.id.toStdString()},
                        {"tick", static_cast<std::int64_t>(std::llround(std::clamp(m.beats, 0.0, kMaxBeatsEdit) * lpc::kPPQ))},
                        {"name", m.name.left(200).toStdString()}});
    sendCommand({{"type", "set_markers"}, {"markers", list}});
}

void ProjectController::addMarker(double beats, const QString& name) {
    if (!host_ || !std::isfinite(beats)) return;
    auto rows = markerRows_;
    rows.push_back({QUuid::createUuid().toString(QUuid::WithoutBraces), name.isEmpty() ? QStringLiteral("Marker %1").arg(rows.size() + 1) : name, std::max(0.0, beats)});
    sendMarkers(rows);
}

void ProjectController::createMarkerAtPlayhead() { addMarker(positionBeats_); }

void ProjectController::moveMarker(const QString& id, double beats) {
    if (!host_ || !std::isfinite(beats)) return;
    auto rows = markerRows_;
    for (MarkerRow& m : rows)
        if (m.id == id) m.beats = std::max(0.0, beats);
    sendMarkers(rows);
}

void ProjectController::renameMarker(const QString& id, const QString& name) {
    if (!host_) return;
    auto rows = markerRows_;
    for (MarkerRow& m : rows)
        if (m.id == id) m.name = name;
    sendMarkers(rows);
}

void ProjectController::removeMarker(const QString& id) {
    if (!host_) return;
    auto rows = markerRows_;
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const MarkerRow& m) { return m.id == id; }), rows.end());
    sendMarkers(rows);
}

void ProjectController::setTempoAt(double beats, double bpm) {
    if (!host_ || !std::isfinite(beats) || !std::isfinite(bpm)) return;
    const auto tick = static_cast<std::int64_t>(std::llround(std::clamp(beats, 0.0, kMaxBeatsEdit) * lpc::kPPQ));
    sendCommand({{"type", "set_tempo"}, {"tick", tick}, {"bpm", std::clamp(bpm, 20.0, 999.0)}});
}

void ProjectController::removeTempoAt(double beats) {
    if (!host_ || !std::isfinite(beats)) return;
    sendCommand({{"type", "remove_tempo"}, {"tick", static_cast<std::int64_t>(std::llround(std::clamp(beats, 0.0, kMaxBeatsEdit) * lpc::kPPQ))}});
}

void ProjectController::setSignatureAt(double beats, int numerator, int denominator) {
    const bool denominatorOk = denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16 || denominator == 32;
    if (!host_ || !std::isfinite(beats) || numerator < 1 || numerator > 32 || !denominatorOk) return;
    const auto tick = static_cast<std::int64_t>(std::llround(std::clamp(beats, 0.0, kMaxBeatsEdit) * lpc::kPPQ));
    sendCommand({{"type", "set_signature"}, {"tick", tick}, {"numerator", numerator}, {"denominator", denominator}});
}

void ProjectController::removeSignatureAt(double beats) {
    if (!host_ || !std::isfinite(beats)) return;
    sendCommand({{"type", "remove_signature"}, {"tick", static_cast<std::int64_t>(std::llround(std::clamp(beats, 0.0, kMaxBeatsEdit) * lpc::kPPQ))}});
}

void ProjectController::setLocatorsBySelection(bool rounded) {
    double from = -1, to = 0;
    for (const QString& id : selectedRegions_) {
        const RegionRow* r = regions_.find(id);
        if (!r) continue;
        from = from < 0 ? r->startBeats : std::min(from, r->startBeats);
        to = std::max(to, r->startBeats + r->lengthBeats);
    }
    if (from < 0 || to <= from) return;
    if (rounded) {
        const double bar = barBeats();
        from = std::floor(from / bar + 1e-9) * bar;
        to = std::ceil(to / bar - 1e-9) * bar;
    }
    setLoopBeats(from, to);
}

void ProjectController::moveLocators(int direction) {
    const double length = loopEndBeats_ - loopStartBeats_;
    if (length <= 0) return;
    const double start = std::max(0.0, loopStartBeats_ + direction * length);
    setLoopRange(start, start + length);
}

void ProjectController::deleteMarkerAtPlayhead() {
    for (const MarkerRow& m : markerRows_)
        if (std::abs(m.beats - positionBeats_) < 1.0 / 32) {
            removeMarker(m.id);
            return;
        }
}

void ProjectController::selectInsideLocators() {
    if (loopEndBeats_ <= loopStartBeats_) return;
    QStringList ids;
    for (const RegionRow& r : regionRows_)
        if (r.startBeats >= loopStartBeats_ - 1e-6 && r.startBeats + r.lengthBeats <= loopEndBeats_ + 1e-6) ids.append(r.id);
    selectRegions(ids, "replace");
}

void ProjectController::deselectOutsideLocators() {
    if (loopEndBeats_ <= loopStartBeats_) return;
    QStringList ids;
    for (const RegionRow* r : selectedRegionRows())
        if (r->startBeats + r->lengthBeats > loopStartBeats_ && r->startBeats < loopEndBeats_) ids.append(r->id);
    selectRegions(ids, "replace");
}

namespace {
// the notes and length of a region as the Core stores them, without ids: two regions with the same text are equal
std::string contentKey(const RegionRow& r) {
    nlohmann::json j = nlohmann::json::parse(r.json, nullptr, false);
    if (j.is_discarded()) return {};
    j.erase("id");
    j.erase("start");
    return j.dump();
}
}  // namespace

void ProjectController::selectSimilarRegions() {
    const auto picked = selectedRegionRows();
    if (picked.empty()) return;
    QStringList ids;
    for (const RegionRow& r : regionRows_)
        for (const RegionRow* p : picked)
            if (r.audio == p->audio && (r.audio ? r.mediaId == p->mediaId : std::abs(r.lengthBeats - p->lengthBeats) < 1e-6)) {
                ids.append(r.id);
                break;
            }
    selectRegions(ids, "replace");
}

void ProjectController::selectEqualRegions() {
    const auto picked = selectedRegionRows();
    if (picked.empty()) return;
    QStringList keys;
    for (const RegionRow* p : picked) keys.append(QString::fromStdString(contentKey(*p)));
    QStringList ids;
    for (const RegionRow& r : regionRows_)
        if (keys.contains(QString::fromStdString(contentKey(r)))) ids.append(r.id);
    selectRegions(ids, "replace");
}

void ProjectController::moveSelectedToPlayhead() {
    if (!host_) return;
    const auto picked = selectedRegionRows();
    if (picked.empty()) return;
    double first = picked.front()->startBeats;
    for (const RegionRow* r : picked) first = std::min(first, r->startBeats);
    const double delta = positionBeats_ - first;
    nlohmann::json commands = nlohmann::json::array();
    for (const RegionRow* r : picked) commands.push_back(moveCommand(*r, std::max(0.0, r->startBeats + delta)));
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::splitAtLocators() {
    if (!host_ || loopEndBeats_ <= loopStartBeats_) return;
    std::vector<const RegionRow*> targets = selectedRegionRows();
    if (targets.empty())
        for (const RegionRow& r : regionRows_) targets.push_back(&r);
    nlohmann::json commands = nlohmann::json::array();
    for (const RegionRow* r : targets) {
        // the right-hand cut first: the left part keeps the region's id, so the second cut still finds the region
        for (double at : {loopEndBeats_, loopStartBeats_})
            if (r->startBeats < at && at < r->startBeats + r->lengthBeats)
                commands.push_back({{"type", "split_region"},
                                    {"regionId", r->id.toStdString()},
                                    {"at", regionPosition(*r, at)},
                                    {"newRegionId", QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()}});
    }
    if (commands.empty()) {
        emit notice("No region crosses a locator");
        return;
    }
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::deleteSelectedAndMove() {
    if (!host_) return;
    const auto picked = selectedRegionRows();
    if (picked.empty()) return;
    QSet<QString> gone;
    for (const RegionRow* r : picked) gone.insert(r->id);
    nlohmann::json commands = nlohmann::json::array();
    for (const RegionRow& r : regionRows_) {
        if (gone.contains(r.id)) continue;
        double shift = 0;  // the lengths of the deleted regions on this track that end before this one starts
        for (const RegionRow* p : picked)
            if (p->trackId == r.trackId && p->startBeats + p->lengthBeats <= r.startBeats + 1e-6) shift += p->lengthBeats;
        if (shift > 0) commands.push_back(moveCommand(r, std::max(0.0, r.startBeats - shift)));
    }
    for (const RegionRow* p : picked) commands.push_back({{"type", "remove_region"}, {"regionId", p->id.toStdString()}});
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::deleteUnusedTracks() {
    if (!host_) return;
    nlohmann::json commands = nlohmann::json::array();
    for (const TrackRow& t : allRows_)
        if (!t.master && t.regionCount == 0 && (t.kind == "audio" || t.kind == "instrument" || t.kind == "midi") && t.sends.empty())
            commands.push_back({{"type", "remove_track"}, {"trackId", t.id.toStdString()}});
    if (commands.empty()) {
        emit notice("Every track is in use");
        return;
    }
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

bool ProjectController::saveProjectAs(const QUrl& folder, bool openCopy) {
    if (!host_) return false;
    const std::filesystem::path target = folder.toLocalFile().toStdWString();
    std::error_code ec;
    if (std::filesystem::exists(target, ec) && !std::filesystem::is_empty(target, ec)) {
        setError("Choose an empty or new folder");
        return false;
    }
    if (std::filesystem::equivalent(target, dir_, ec)) {
        setError("That is the folder of this project");
        return false;
    }
    if (!saveProject()) return false;  // the folder on disk is now up to date
    std::filesystem::create_directories(target, ec);
    std::filesystem::copy(dir_, target, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        setError(QString("Cannot copy the project: ") + QString::fromStdString(ec.message()));
        return false;
    }
    if (openCopy) return openProject(QUrl::fromLocalFile(QString::fromStdWString(target.wstring())));
    emit notice("A copy of the project was saved");
    return true;
}

void ProjectController::importAudioFilesHere(const QList<QUrl>& files) {
    if (!host_ || files.isEmpty()) return;
    QString target;
    for (const QString& id : std::as_const(selectedTracks_))
        if (const TrackRow* t = tracks_.find(id); t && t->kind == "audio") { target = id; break; }
    if (target.isEmpty())
        for (const TrackRow& t : allRows_)
            if (t.kind == "audio" && !t.master) { target = t.id; break; }
    if (target.isEmpty()) {
        setError("Add an audio track first");
        return;
    }
    importAudioFiles(files, target, positionBeats_);
}

void ProjectController::bounceProject(const QUrl& file) {
    if (!host_) return;
    std::filesystem::path out = file.toLocalFile().toStdWString();
    if (out.extension().empty()) out += ".wav";
    if (!saveProject()) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    if (lpc::projectEndFrame(project) == 0) {
        setError("Nothing to bounce: the project has no regions");
        return;
    }
    emit notice("Bouncing…");
    const std::filesystem::path root = dir_;
    QPointer<ProjectController> self(this);
    (void)QtConcurrent::run([self, project, root, out] {
        QString message;
        try {
            lpc::MediaStore media(root, /*streaming=*/false);
            const lpc::RenderResult r = lpc::renderOffline(project, media);
            lpc::writeWav(out, r.sampleRate, 2, r.interleaved, lpc::WavFormat::Pcm24);
            message = QString("Bounced %1 s to %2").arg(static_cast<double>(r.frames) / r.sampleRate, 0, 'f', 1).arg(QString::fromStdWString(out.filename().wstring()));
        } catch (const std::exception& e) {
            message = QString("Bounce failed: ") + QString::fromUtf8(e.what());
        }
        if (self) QMetaObject::invokeMethod(self, [self, message] { if (self) emit self->notice(message); }, Qt::QueuedConnection);
    });
}

}  // namespace jad
