// The Edit menu of the Tracks area on regions: copy, cut, paste, duplicate, mute, the Select, Trim, Length and Move commands.
// Each change is one command (or one transaction) and so one undo step, like the Core's own commands.
#include <QSettings>
#include <QDateTime>
#include <QFile>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QTimer>
#include <QtConcurrent>
#include <QPointer>
#include <QUuid>

#include <algorithm>
#include <filesystem>
#include <cmath>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>

#include "bridge/project_controller.h"
#ifdef JAD_HAVE_JUCE
#include "juce_device.h"
#include "juce_plugin_host.h"
#endif
#include "lpc/model_json.h"
#include "lpc/media_store.h"
#include "lpc/wav.h"
#include "lpc/offline_render.h"
#include "lpc/audio/effects.h"
#include "lpc/aiff.h"
#include "lpc/flac.h"
#include "lpc/midi_file.h"
#include "lpc/audio_ops.h"
#include "lpc/effect_specs.h"
#include "lpc/processor_ids.h"
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

// Mute Regions: the selected regions go silent when any of them sounds, else they all sound again (one undo step).
void ProjectController::toggleMuteSelectedRegions() {
    const auto rows = selectedRegionRows();
    if (rows.empty() || !host_) return;
    bool anyUnmuted = false;
    for (const RegionRow* r : rows) anyUnmuted = anyUnmuted || !r->muted;
    nlohmann::json commands = nlohmann::json::array();
    for (const RegionRow* r : rows) {
        nlohmann::json region = nlohmann::json::parse(r->json, nullptr, false);
        if (region.is_discarded() || r->muted == anyUnmuted) continue;
        if (anyUnmuted) region["muted"] = true;
        else region.erase("muted");
        commands.push_back({{"type", "replace_region"}, {"region", region}});
    }
    if (commands.empty()) return;
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

// Loop/Unloop Regions (L): the selected regions repeat their content until their end, or stop repeating; one undo step.
void ProjectController::toggleLoopSelectedRegions() {
    const auto rows = selectedRegionRows();
    if (rows.empty() || !host_) return;
    bool anyUnlooped = false;
    for (const RegionRow* r : rows) anyUnlooped = anyUnlooped || r->loopBeats <= 0;
    nlohmann::json commands = nlohmann::json::array();
    for (const RegionRow* r : rows) {
        nlohmann::json region = nlohmann::json::parse(r->json, nullptr, false);
        if (region.is_discarded()) continue;
        const std::int64_t length = region.value("length", std::int64_t{0});
        if ((r->loopBeats > 0) == anyUnlooped) continue;  // already as it should be
        commands.push_back({{"type", "set_region_loop"}, {"regionId", r->id.toStdString()}, {"loopLength", anyUnlooped ? length : std::int64_t{0}}});
    }
    if (commands.empty()) return;
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
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
            {"lengthBeats", row->lengthBeats}, {"audio", row->audio}, {"color", row->color}, {"mediaId", row->mediaId},
            {"sourceOffsetFrames", static_cast<double>(row->sourceOffsetFrames)}, {"lengthFrames", static_cast<double>(row->lengthFrames)},
            {"mediaFrames", static_cast<double>(row->mediaFrames)}};
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
    gestureActive_ = true;
    groupBase_.clear();
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
    gestureActive_ = false;
    groupBase_.clear();
    if (host_) host_->endGesture();
    finishAutomationCaptures(false);  // a touch take ends when the fader is let go
}

void ProjectController::setGainLive(const QString& trackId, double db) {
    if (!host_ || !std::isfinite(db)) return;
    liveGain_[trackId] = db;
    captureAutomation(trackId, false, std::clamp(db, -96.0, 24.0));
    if (!liveTimer_.isActive()) {
        flushLive();
        liveTimer_.start();
    }
}

void ProjectController::setPanLive(const QString& trackId, double pan) {
    if (!host_ || !std::isfinite(pan)) return;
    livePan_[trackId] = pan;
    captureAutomation(trackId, true, std::clamp(pan, -1.0, 1.0));
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

QString ProjectController::addAudioTrackNamed(const QString& name) {
    QString clean;
    for (const QChar c : name)
        if (c.unicode() >= 32 && c.unicode() != 127) clean.append(c);
    clean = clean.trimmed().left(60);
    if (clean.isEmpty()) clean = QStringLiteral("Audio");
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const std::string nullId = "00000000-0000-0000-0000-000000000000";
    sendCommand({{"type", "add_track"},
                 {"index", -1},
                 {"track", {{"id", id.toStdString()},
                            {"kind", "audio"},
                            {"name", clean.toStdString()},
                            {"color", ""},
                            {"strip", {{"gainDb", 0}, {"pan", 0}, {"mute", false}, {"solo", false}, {"inserts", nlohmann::json::array()},
                                       {"sends", nlohmann::json::array()}, {"output", nullId}}},
                            {"regions", nlohmann::json::array()},
                            {"automation", nlohmann::json::array()},
                            {"instrument", nullptr}}}});
    pendingAudioTracks_.insert(id);
    return id;
}

void ProjectController::importAudioFilesAt(const QList<QUrl>& files, const QString& trackId, double startBeats) {
    if (!host_ || files.isEmpty() || !std::isfinite(startBeats)) return;
    const TrackRow* track = tracks_.find(trackId);
    if (track && track->kind == "audio") {
        importAudioFiles(files, trackId, startBeats);
        return;
    }
    for (const QUrl& file : files) {  // not on an audio track: one new track per file, named after it
        const QString id = addAudioTrackNamed(QFileInfo(file.toLocalFile()).completeBaseName());
        importAudioFiles({file}, id, startBeats);
    }
}

void ProjectController::importAudioFilesHere(const QList<QUrl>& files) {
    if (!host_ || files.isEmpty()) return;
    QString target;
    for (const QString& id : std::as_const(selectedTracks_))
        if (const TrackRow* t = tracks_.find(id); t && t->kind == "audio") { target = id; break; }
    importAudioFilesAt(files, target, positionBeats_);  // no audio track selected: new tracks named after the files
}

void ProjectController::bounceProject(const QUrl& file) { bounceProjectAs(file, {}); }

void ProjectController::bounceProjectAs(const QUrl& file, const QVariantMap& options) {
    if (!host_) return;
    const QString format = options.value("format", "wav24").toString();
    const QString range = options.value("range", "project").toString();
    const bool normalizeOn = options.value("normalize", false).toBool();
    const double tail = std::clamp(options.value("tail", 0.5).toDouble(), 0.0, 30.0);
    const bool aiff = format.startsWith("aiff");
    const bool flac = format.startsWith("flac");
    const int bits = format.endsWith("16") ? 16 : (format.endsWith("32") ? 32 : 24);
    const bool dither = options.value("dither", bits == 16).toBool() && bits == 16;
    std::filesystem::path out = file.toLocalFile().toStdWString();
    if (out.extension().empty()) out += aiff ? ".aif" : (flac ? ".flac" : ".wav");
    if (!saveProject()) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    if (lpc::projectEndFrame(project) == 0) {
        setError("Nothing to bounce: the project has no regions");
        return;
    }
    lpc::RenderOptions render;
    render.tailSeconds = tail;
    if (range == "cycle") {
        if (loopEndBeats_ <= loopStartBeats_) {
            setError("Set the cycle area first (drag in the top strip of the ruler)");
            return;
        }
        const auto toFrames = [this](double beats) {
            return static_cast<std::int64_t>(std::llround(tempoMap_.ticksToSamples(static_cast<lpc::Ticks>(std::llround(beats * lpc::kPPQ)), sampleRate_)));
        };
        render.startFrame = toFrames(loopStartBeats_);
        render.frames = toFrames(loopEndBeats_) - render.startFrame;
    }
    if (bouncing_->exchange(true)) {
        setError("A bounce is already running");
        return;
    }
    if (playing_) stop();  // the plug-ins are rendered by the bounce: the live engine must not touch them meanwhile
#ifdef JAD_HAVE_JUCE
    render.plugins = pluginHost_.get();  // VST3 effects and instruments sound in the bounce, aligned for their latency
#endif
    emit notice("Bouncing…");
    const std::filesystem::path root = dir_;
    QPointer<ProjectController> self(this);
    const std::shared_ptr<std::atomic<bool>> flag = bouncing_;
    (void)QtConcurrent::run([self, flag, project, root, out, render, aiff, flac, bits, normalizeOn, dither] {
        QString message;
        try {
            lpc::MediaStore media(root, /*streaming=*/false);
            const lpc::RenderResult r = lpc::renderOffline(project, media, render);
            flag->store(false);  // the plug-ins are free again: only the files are left to write
            lpc::WavData data;
            data.sampleRate = r.sampleRate;
            data.channels = 2;
            data.samples = r.interleaved;
            if (normalizeOn) lpc::normalize(data, -0.3);
            if (dither) lpc::ditherTpdf(data, bits);
            if (aiff) lpc::writeAiff(out, r.sampleRate, 2, data.samples, bits);
            else if (flac) lpc::writeFlac(out, r.sampleRate, 2, data.samples, bits);
            else lpc::writeWav(out, r.sampleRate, 2, data.samples, bits == 16 ? lpc::WavFormat::Pcm16 : (bits == 32 ? lpc::WavFormat::Float32 : lpc::WavFormat::Pcm24));
            message = QString("Bounced %1 s to %2").arg(static_cast<double>(r.frames) / r.sampleRate, 0, 'f', 1).arg(QString::fromStdWString(out.filename().wstring()));
        } catch (const std::exception& e) {
            message = QString("Bounce failed: ") + QString::fromUtf8(e.what());
        }
        flag->store(false);  // the files are written: playback may use the plug-ins again
        if (self) QMetaObject::invokeMethod(self, [self, message] { if (self) emit self->notice(message); }, Qt::QueuedConnection);
    });
}

double ProjectController::projectEndBeats() const {
    double end = 0;
    for (const RegionRow& r : regionRows_) end = std::max(end, r.startBeats + r.lengthBeats);
    return end;
}

void ProjectController::setAutomationVisible(bool on) {
    if (on == automationVisible_) return;
    automationVisible_ = on;
    emit automationViewChanged();
}

void ProjectController::setAutomationParam(const QString& param) {
    if ((param != "volume" && param != "pan") || param == automationParam_) return;
    automationParam_ = param;
    emit automationViewChanged();
}

QVariantList ProjectController::automationPoints(const QString& trackId, const QString& target) const {
    QVariantList out;
    for (const TrackRow& t : allRows_) {
        if (t.id != trackId) continue;
        for (const AutoRow& p : target == "pan" ? t.panAuto : t.volumeAuto) out.append(QVariantMap{{"beats", p.beats}, {"value", p.value}});
    }
    return out;
}

void ProjectController::setAutomationPoints(const QString& trackId, const QString& target, const QVariantList& points) {
    if (!host_ || (target != "volume" && target != "pan")) return;
    const double lo = target == "volume" ? -96.0 : -1.0, hi = target == "volume" ? 24.0 : 1.0;
    nlohmann::json list = nlohmann::json::array();
    for (const QVariant& v : points) {
        const QVariantMap m = v.toMap();
        const double beats = m.value("beats").toDouble(), value = m.value("value").toDouble();
        if (!std::isfinite(beats) || !std::isfinite(value)) continue;
        list.push_back({{"tick", static_cast<std::int64_t>(std::llround(std::clamp(beats, 0.0, kMaxBeatsEdit) * lpc::kPPQ))}, {"value", std::clamp(value, lo, hi)}});
    }
    sendCommand({{"type", "set_automation"}, {"trackId", trackId.toStdString()}, {"target", target.toStdString()}, {"points", list}});
}

void ProjectController::setMetronome(bool on) {
    if (on == metronome_) return;
    metronome_ = on;
    if (host_) host_->setMetronome(on);
    emit metronomeChanged();
}

void ProjectController::clickSettingsEdited() {
    ++clickRevision_;
    if (host_) host_->setClickSettings(clickSettings_);
    emit clickSettingsChanged();
}

void ProjectController::setClickMode(const QString& mode) {
    if ((mode != "beats" && mode != "eighths" && mode != "sixteenths" && mode != "grouped") || mode.toStdString() == clickSettings_.mode) return;
    clickSettings_.mode = mode.toStdString();
    clickSettingsEdited();
}

void ProjectController::setClickGrouping(const QString& grouping) {
    const std::string text = grouping.left(40).toStdString();
    if (text == clickSettings_.grouping) return;
    clickSettings_.grouping = text;
    clickSettingsEdited();
}

QString ProjectController::clickSlotFile(int slot) const {
    if (slot < 1 || slot >= lpc::audio::kClickSlots) return {};
    return QString::fromStdString(clickSettings_.files[static_cast<std::size_t>(slot)]);
}

void ProjectController::setClickSlotFile(int slot, const QUrl& file) {
    if (slot < 1 || slot >= lpc::audio::kClickSlots || !file.isLocalFile()) return;
    clickSettings_.files[static_cast<std::size_t>(slot)] = file.toLocalFile().toStdString();
    clickSettingsEdited();
}

void ProjectController::clearClickSlot(int slot) {
    if (slot < 1 || slot >= lpc::audio::kClickSlots || clickSettings_.files[static_cast<std::size_t>(slot)].empty()) return;
    clickSettings_.files[static_cast<std::size_t>(slot)].clear();
    clickSettingsEdited();
}

void ProjectController::setClickSlotGain(int slot, double gain) {
    if (slot < 1 || slot >= lpc::audio::kClickSlots || !std::isfinite(gain)) return;
    clickSettings_.gain[static_cast<std::size_t>(slot)] = static_cast<float>(std::clamp(gain, 0.0, 2.0));
    clickSettingsEdited();
}

double ProjectController::clickSlotGain(int slot) const {
    return slot < 1 || slot >= lpc::audio::kClickSlots ? 1.0 : static_cast<double>(clickSettings_.gain[static_cast<std::size_t>(slot)]);
}

QString ProjectController::clickSlotName(int slot) {
    switch (slot) {
    case lpc::audio::kSlotE: return QStringLiteral("e");
    case lpc::audio::kSlotAnd: return QStringLiteral("&");
    case lpc::audio::kSlotA: return QStringLiteral("a");
    case lpc::audio::kSlotLa: return QStringLiteral("la");
    case lpc::audio::kSlotLi: return QStringLiteral("li");
    default: return slot >= 1 && slot <= 32 ? QString::number(slot) : QString();
    }
}

void ProjectController::loadClickSettings(QSettings& s) {
    clickSettings_.mode = s.value("click/mode", "beats").toString().toStdString();
    clickSettings_.grouping = s.value("click/grouping").toString().toStdString();
    for (int slot = 1; slot < lpc::audio::kClickSlots; ++slot) {
        clickSettings_.files[static_cast<std::size_t>(slot)] = s.value(QStringLiteral("click/file%1").arg(slot)).toString().toStdString();
        clickSettings_.gain[static_cast<std::size_t>(slot)] = s.value(QStringLiteral("click/gain%1").arg(slot), 1.0).toFloat();
    }
    ++clickRevision_;
    if (host_) host_->setClickSettings(clickSettings_);
    emit clickSettingsChanged();
}

void ProjectController::saveClickSettings(QSettings& s) const {
    s.setValue("click/mode", QString::fromStdString(clickSettings_.mode));
    s.setValue("click/grouping", QString::fromStdString(clickSettings_.grouping));
    for (int slot = 1; slot < lpc::audio::kClickSlots; ++slot) {
        s.setValue(QStringLiteral("click/file%1").arg(slot), QString::fromStdString(clickSettings_.files[static_cast<std::size_t>(slot)]));
        s.setValue(QStringLiteral("click/gain%1").arg(slot), clickSettings_.gain[static_cast<std::size_t>(slot)]);
    }
}

void ProjectController::setCountInEnabled(bool on) {
    if (on == countIn_) return;
    countIn_ = on;
    emit recordingChanged();
}

void ProjectController::setAutoInputMonitoring(bool on) {
    if (on == autoInput_) return;
    autoInput_ = on;
    QSettings().setValue("record/autoInputMonitoring", on);
    applyMonitoring();
    emit recordingChanged();
}

void ProjectController::setCountInChoice(int choice) {
    if (choice == 0 || choice < -3 || choice > 6 || choice == countInChoice_) return;
    countInChoice_ = choice;
    emit recordingChanged();
}

void ProjectController::toggleRecording() {
    if (recording_) sendRecordingStop();  // the transport keeps playing: Record/Record Toggle
    else startRecording();
}

void ProjectController::sendRecordingStop() {
    recFinishing_ = true;
    if (host_) host_->stopRecording();
}

void ProjectController::setPunchEnabled(bool on) {
    if (on == punchEnabled_) return;
    punchEnabled_ = on;
    emit punchChanged();
}

void ProjectController::setPunchRange(double startBeats, double endBeats) {
    if (!std::isfinite(startBeats) || !std::isfinite(endBeats) || endBeats <= startBeats) return;
    punchStartBeats_ = std::clamp(startBeats, 0.0, kMaxBeatsEdit);
    punchEndBeats_ = std::clamp(endBeats, 0.0, kMaxBeatsEdit);
    emit punchChanged();
}

void ProjectController::setRecordingDelay(int samples) {
    samples = std::clamp(samples, -4800, 48000);
    if (samples == recordingDelay_) return;
    recordingDelay_ = samples;
    emit audioSettingsChanged();
}

int ProjectController::trackInput(const QString& trackId) const {
    for (const TrackRow& t : allRows_)
        if (t.id == trackId) return t.input;
    return 0;
}

void ProjectController::setTrackInput(const QString& trackId, int input) {
    if (!host_ || input < 0 || input > 64) return;
    sendCommand({{"type", "set_strip"}, {"trackId", trackId.toStdString()}, {"input", input}});
}

QStringList ProjectController::inputChoices() const {
    const bool defaults = inputLabel(1) == QStringLiteral("Input 1") && inputLabel(2) == QStringLiteral("Input 2");
    QStringList out{defaults ? QStringLiteral("Input 1 + 2 (stereo)") : QStringLiteral("%1 + %2 (stereo)").arg(inputLabel(1), inputLabel(2))};
    const int n = std::max(inputChannels(), 2);
    for (int i = 1; i <= n; ++i) out << inputLabel(i);
    return out;
}

// The tracks that play their input through the strip: the ones with the I button on.
void ProjectController::applyMonitoring() {
    if (!host_) return;
    const QSet<QString> on = trackToggles_.value(QStringLiteral("track.inputMonitor"));
    for (const TrackRow& t : allRows_) {
        if (t.master || t.kind != "audio") continue;
        const bool armed = trackToggles_.value(QStringLiteral("track.recordArm")).contains(t.id);
        const bool monitored = on.contains(t.id) || (autoInput_ && armed && (!playing_ || recording_));
        host_->setMonitor(lpc::Uuid::parse(t.id.toStdString()).value_or(lpc::Uuid{}), monitored ? (t.input == 0 ? 1 : t.input) : 0, monitored && t.input == 0 ? 2 : 0);
    }
}

void ProjectController::startRecording() {
    if (!host_ || !engine_ || recording_) return;
    if (degraded()) {
        setError("Audio engine not running: recording is unavailable until it recovers");
        return;
    }
    if (openAudioDevice_ && !device_) {
        setError(QString("No audio device: ") + (deviceError_.isEmpty() ? QString("no device") : deviceError_));
        return;
    }
    recTracks_.clear();
    recInputs_.clear();
    const QSet<QString> armed = trackToggles_.value(QStringLiteral("track.recordArm"));
    for (const TrackRow& t : allRows_)
        if (t.kind == "audio" && !t.master && armed.contains(t.id)) {
            recTracks_ << t.id;
            recInputs_.insert(t.id, t.input);
        }
    recMidiTracks_.clear();
    recMidi_.clear();
    for (const TrackRow& t : allRows_)
        if (t.kind == "instrument" && !t.master && armed.contains(t.id)) recMidiTracks_ << t.id;
    if (recTracks_.isEmpty() && recMidiTracks_.isEmpty()) {
        emit notice("Arm a track (R) to record");
        return;
    }
    const bool quickPunch = playing_;  // pressing Record while the project plays: record from here, no count-in
    if (quickPunch && !allowQuickPunch_) {
        emit notice("Quick punch-in is off (Record > Allow Quick Punch-In)");
        return;
    }
    const lpc::Ticks tick = static_cast<lpc::Ticks>(std::llround(std::clamp(positionBeats_, 0.0, kMaxBeatsEdit) * lpc::kPPQ));
    const auto startFrame = static_cast<std::int64_t>(std::llround(tempoMap_.ticksToSamples(tick, sampleRate_)));
    lpc::audio::ClickTrack count;
    std::int64_t countFrames = 0;
    if (countIn_ && !quickPunch) {  // the bars before the take, at the tempo of the start, counted 1 2 3 4 with an accent on the first
        const double framesPerBeat = sampleRate_ * 60.0 / tempoMap_.bpmAt(tick) * 4.0 / beatUnit_;
        const int beats = countInChoice_ > 0 ? countInChoice_ * beatsPerBar_ : -countInChoice_;  // bars, or x/4: x beats
        for (int k = 0; k < beats; ++k) {
            count.frames.push_back(static_cast<std::int64_t>(std::llround(k * framesPerBeat)));
            count.accent.push_back(k % beatsPerBar_ == 0 ? 1 : 0);
            count.slot.push_back(static_cast<std::uint8_t>(std::min(k % beatsPerBar_ + 1, 32)));
        }
        countFrames = static_cast<std::int64_t>(std::llround(beats * framesPerBeat));
    }
    recChannels_.clear();
    recChunks_.clear();
    punchStopSent_ = false;
    recording_ = true;
    recFinishing_ = false;
    host_->startRecording(startFrame, countFrames, std::move(count), !quickPunch);
    applyMonitoring();
    emit recordingChanged();
}

void ProjectController::drainRecording() {
    drainMidiRecording();
    constexpr std::size_t kMaxFrames = 48000u * 60u * 30u;  // half an hour at 48 kHz
    lpc::audio::AudioEngine::RecChunk c;
    while (engine_ && engine_->takeRecorded(c)) {
        if (recChannels_.empty()) recChannels_.assign(static_cast<std::size_t>(std::max(c.channels, 1)), {});
        if (!recChannels_.empty() && recChannels_[0].size() + static_cast<std::size_t>(c.frames) > kMaxFrames) {
            if (!recFinishing_) {
                emit notice("The take reached 30 minutes: recording stopped");
                sendRecordingStop();
            }
            continue;
        }
        recChunks_.push_back({c.position, c.frames, recChannels_[0].size()});
        for (std::size_t ch = 0; ch < recChannels_.size(); ++ch) {
            const int src = static_cast<int>(std::min<std::size_t>(ch, static_cast<std::size_t>(std::max(c.channels, 1)) - 1));
            recChannels_[ch].insert(recChannels_[ch].end(), c.ch[src], c.ch[src] + c.frames);
        }
    }
}

void ProjectController::finishRecording() {
    drainRecording();
    recording_ = false;
    recFinishing_ = false;
    applyMonitoring();
    emit recordingChanged();
    if (!recChunks_.empty()) finishMidiRecording(recChunks_.front().position, recChunks_.back().position + recChunks_.back().frames);
    else { recMidiTracks_.clear(); recMidi_.clear(); }
    if (recTracks_.isEmpty()) {  // only instrument tracks were armed: the notes are the take
        recChannels_.clear();
        recChunks_.clear();
        return;
    }
    if (recChunks_.empty() || recChannels_.empty()) {
        emit notice("Nothing was recorded");
        recChannels_.clear();
        recChunks_.clear();
        return;
    }
    // the takes: runs of blocks that follow each other on the timeline (a cycle wrap starts a new one)
    struct Run { std::int64_t position; std::size_t from, to; };
    std::vector<Run> runs;
    for (const RecChunkInfo& ch : recChunks_) {
        if (!runs.empty() && runs.back().position + static_cast<std::int64_t>(runs.back().to - runs.back().from) == ch.position) runs.back().to = ch.offset + static_cast<std::size_t>(ch.frames);
        else runs.push_back({ch.position, ch.offset, ch.offset + static_cast<std::size_t>(ch.frames)});
    }
    const int latency = (device_ ? device_->roundTripLatency() : 0) + recordingDelay_;
    const std::int64_t punchIn = punchEnabled_ ? static_cast<std::int64_t>(std::llround(tempoMap_.ticksToSamples(static_cast<lpc::Ticks>(std::llround(punchStartBeats_ * lpc::kPPQ)), sampleRate_))) : 0;
    const std::int64_t punchOut = punchEnabled_ ? static_cast<std::int64_t>(std::llround(tempoMap_.ticksToSamples(static_cast<lpc::Ticks>(std::llround(punchEndBeats_ * lpc::kPPQ)), sampleRate_))) : std::numeric_limits<std::int64_t>::max();
    int made = 0;
    double seconds = 0;
    const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh.mm.ss");
    for (const QString& trackId : std::as_const(recTracks_)) {
        const int input = recInputs_.value(trackId);
        QString trackName;
        for (const TrackRow& t : allRows_) if (t.id == trackId) trackName = t.name;
        int take = 0;
        for (const Run& run : runs) {
            std::int64_t from = static_cast<std::int64_t>(run.from) + latency;  // the take moves earlier by the latency
            std::int64_t start = run.position + latency - latency;              // where the first kept frame lies on the timeline
            std::int64_t to = static_cast<std::int64_t>(run.to);
            if (punchEnabled_) {  // only what lies inside the punch range
                const std::int64_t first = std::max(start, punchIn), last = std::min(run.position + (to - static_cast<std::int64_t>(run.from)), punchOut);
                from = static_cast<std::int64_t>(run.from) + (first - run.position) + latency;
                to = static_cast<std::int64_t>(run.from) + (last - run.position);
                start = first;
            }
            from = std::max<std::int64_t>(from, 0);
            if (to <= from) continue;
            const bool stereo = input == 0 && recChannels_.size() >= 2;
            const std::size_t left = input == 0 ? 0 : std::min<std::size_t>(static_cast<std::size_t>(input - 1), recChannels_.size() - 1);
            const std::size_t right = stereo ? 1 : left;
            std::vector<float> data;
            data.reserve(static_cast<std::size_t>(to - from) * (stereo ? 2 : 1));
            for (std::int64_t i = from; i < to; ++i) {
                data.push_back(recChannels_[left][static_cast<std::size_t>(i)]);
                if (stereo) data.push_back(recChannels_[right][static_cast<std::size_t>(i)]);
            }
            const QString name = QStringLiteral("Recording %1 %2%3.wav").arg(stamp, trackName.left(30), take == 0 && runs.size() == 1 ? QString() : QStringLiteral(" take %1").arg(take + 1));
            QString safe;
            for (const QChar ch : name) safe.append(QStringLiteral("\\/:*?\"<>|").contains(ch) ? QLatin1Char('_') : ch);
            const std::filesystem::path tmp = std::filesystem::temp_directory_path() / safe.toStdU16String();
            try {
                lpc::writeWav(tmp, sampleRate_, stereo ? 2 : 1, data, lpc::WavFormat::Pcm24);
            } catch (const std::exception& e) {
                setError(QString("Cannot save the recording: ") + QString::fromUtf8(e.what()));
                continue;
            }
            const double startBeats = static_cast<double>(tempoMap_.samplesToTicks(static_cast<double>(start), sampleRate_)) / lpc::kPPQ;
            importQueue_.push_back({QUrl::fromLocalFile(QString::fromStdU16String(tmp.u16string())), trackId, startBeats, true});
            seconds = std::max(seconds, static_cast<double>(data.size() / (stereo ? 2 : 1)) / sampleRate_);
            ++take;
            ++made;
        }
    }
    recChannels_.clear();
    recChunks_.clear();
    if (made == 0) {
        emit notice("Nothing was recorded");
        return;
    }
    emit notice(QString("Recorded %1 take%2, %3 s").arg(made).arg(made == 1 ? "" : "s").arg(seconds, 0, 'f', 1));
    if (!importRunning_) startNextImport();
}


void ProjectController::addTracks(const QString& kind, int count, const QString& name) {
    if (!host_ || (kind != "audio" && kind != "instrument" && kind != "bus")) return;
    count = std::clamp(count, 1, 64);
    const QString label = name.isEmpty() ? (kind == "audio" ? "Audio" : (kind == "instrument" ? "Instrument" : "Bus")) : name.left(60);
    const std::string nullId = "00000000-0000-0000-0000-000000000000";
    nlohmann::json commands = nlohmann::json::array();
    for (int i = 0; i < count; ++i) {
        // a single track keeps the name it was given; several get a number
        const QString full = name.isEmpty() ? label + " " + QString::number(tracks_.totalCount() + 1 + i) : (count == 1 ? label : label + " " + QString::number(i + 1));
        nlohmann::json instrument = nullptr;
        if (kind == "instrument") instrument = {{"processorId", "builtin.sine"}, {"params", nlohmann::json::object()}, {"state", ""}};
        commands.push_back({{"type", "add_track"},
                            {"index", -1},
                            {"track", {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()},
                                       {"kind", kind.toStdString()},
                                       {"name", full.toStdString()},
                                       {"color", ""},
                                       {"strip", {{"gainDb", 0}, {"pan", 0}, {"mute", false}, {"solo", false}, {"inserts", nlohmann::json::array()},
                                                  {"sends", nlohmann::json::array()}, {"output", nullId}}},
                                       {"regions", nlohmann::json::array()},
                                       {"automation", nlohmann::json::array()},
                                       {"instrument", instrument}}}});
    }
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

int ProjectController::inputChannels() const { return device_ ? device_->inputChannels() : 0; }
double ProjectController::deviceRate() const { return device_ ? device_->sampleRate() : 0.0; }

QVariantMap ProjectController::audioDevices(const QString& output, const QString& input) const {
    lpc::AudioDeviceChoices c;
    if (deviceLister_) {
        c = deviceLister_(output.toStdString(), input.toStdString());
    } else {
#ifdef JAD_HAVE_JUCE
        c = lpc::listJuceAudioDevices(output.toStdString(), input.toStdString());
#endif
    }
    QStringList outputs, inputs;
    for (const std::string& n : c.outputs) outputs << QString::fromStdString(n);
    for (const std::string& n : c.inputs) inputs << QString::fromStdString(n);
    QVariantList rates, buffers;
    for (const double r : c.rates) rates << r;
    for (const int b : c.buffers) buffers << b;
    return {{"outputs", outputs}, {"inputs", inputs}, {"currentOutput", QString::fromStdString(c.currentOutput)},
            {"currentInput", QString::fromStdString(c.currentInput)}, {"rates", rates}, {"buffers", buffers},
            {"inputChannels", c.inputChannels}, {"outputChannels", c.outputChannels}};
}

void ProjectController::applyAudioSettings(const QString& output, const QString& input, int bufferSize) {
    audioOutput_ = output;
    audioInput_ = input;
    audioBuffer_ = std::clamp(bufferSize, 32, 4096);
    if (engine_) openDevice();  // the project keeps playing state: it is stopped by the device change
    emit audioSettingsChanged();
}

void ProjectController::loadAudioSettings(QSettings& s) {
    audioOutput_ = s.value("audio/output").toString();
    audioInput_ = s.value("audio/input").toString();
    audioBuffer_ = std::clamp(s.value("audio/buffer", 256).toInt(), 32, 4096);
    recordingDelay_ = std::clamp(s.value("audio/recordingDelay", 0).toInt(), -4800, 48000);
    emit audioSettingsChanged();
}

void ProjectController::saveAudioSettings(QSettings& s) const {
    s.setValue("audio/output", audioOutput_);
    s.setValue("audio/input", audioInput_);
    s.setValue("audio/buffer", audioBuffer_);
    s.setValue("audio/recordingDelay", recordingDelay_);
}

void ProjectController::openEffectEditor(const QString& trackId, int index) {
    effectEditorTrack_ = trackId;
    effectEditorIndex_ = index;
    emit effectEditorChanged();
}

void ProjectController::closeEffectEditor() {
    if (effectEditorTrack_.isEmpty()) return;
    effectEditorTrack_.clear();
    effectEditorIndex_ = -1;
    emit effectEditorChanged();
}

QVariantList ProjectController::effectSpecs() const {
    static const QVariantList specs = [] {
        QVariantList out;
        for (const lpc::EffectSpec& s : lpc::effectSpecs()) {
            QVariantList params;
            for (const lpc::EffectParam& p : s.params)
                params.append(QVariantMap{{"name", QString::fromStdString(p.name)}, {"label", QString::fromStdString(p.label)},
                                          {"unit", QString::fromStdString(p.unit)}, {"min", p.min}, {"max", p.max}, {"def", p.def},
                                          {"logarithmic", p.logarithmic}});
            out.append(QVariantMap{{"id", QString::fromStdString(s.id)}, {"name", QString::fromStdString(s.name)},
                                   {"group", QString::fromStdString(s.group)}, {"params", params}});
        }
        return out;
    }();
    return specs;
}

QVariantList ProjectController::trackInserts(const QString& trackId) const {
    QVariantList out;
    for (const TrackRow& t : allRows_) {
        if (t.id != trackId) continue;
        for (const InsertRow& i : t.inserts)
            out.append(QVariantMap{{"processorId", i.processorId}, {"label", i.label}, {"plugin", i.plugin}, {"bypass", i.bypass}, {"params", i.params}});
    }
    return out;
}

QString ProjectController::trackName(const QString& trackId) const {
    for (const TrackRow& t : allRows_)
        if (t.id == trackId) return t.name;
    return {};
}

QVariantList ProjectController::eqCurve(const QVariantMap& values, int points, double minHz, double maxHz) const {
    lpc::ProcessorRef ref;
    ref.processorId = lpc::kProcEq;
    for (auto it = values.begin(); it != values.end(); ++it) ref.params[it.key().toStdString()] = it.value().toDouble();
    QVariantList out;
    points = std::clamp(points, 2, 2000);
    for (int i = 0; i < points; ++i) {
        const double f = minHz * std::pow(maxHz / minHz, static_cast<double>(i) / (points - 1));
        out.append(lpc::audio::eqResponseDb(ref, sampleRate_, f));
    }
    return out;
}

QVariantList ProjectController::instrumentSpecs() const {
    static const QVariantList specs = [] {
        QVariantList out;
        for (const lpc::EffectSpec& s : lpc::instrumentSpecs()) {
            QVariantList params;
            for (const lpc::EffectParam& p : s.params)
                params.append(QVariantMap{{"name", QString::fromStdString(p.name)}, {"label", QString::fromStdString(p.label)},
                                          {"unit", QString::fromStdString(p.unit)}, {"min", p.min}, {"max", p.max}, {"def", p.def},
                                          {"logarithmic", p.logarithmic}});
            out.append(QVariantMap{{"id", QString::fromStdString(s.id)}, {"name", QString::fromStdString(s.name)}, {"group", QString::fromStdString(s.group)}, {"params", params}});
        }
        return out;
    }();
    return specs;
}

QVariantMap ProjectController::trackInstrument(const QString& trackId) const {
    for (const TrackRow& t : allRows_)
        if (t.id == trackId) return {{"processorId", t.instrument}, {"params", t.instrumentParams}};
    return {};
}

void ProjectController::setInstrument(const QString& trackId, const QString& processorId) {
    if (!host_ || !lpc::isKnownInstrument(processorId.toStdString())) return;
    sendCommand({{"type", "set_instrument"}, {"trackId", trackId.toStdString()},
                 {"instrument", {{"processorId", processorId.toStdString()}, {"params", nlohmann::json::object()}, {"state", ""}}}});
}

void ProjectController::setInstrumentParam(const QString& trackId, const QString& param, double value) {
    if (!host_ || !std::isfinite(value)) return;
    for (const TrackRow& t : allRows_) {
        if (t.id != trackId || t.instrument.isEmpty()) continue;
        const lpc::EffectSpec* spec = lpc::findInstrumentSpec(t.instrument.toStdString());
        const lpc::EffectParam* p = spec ? spec->find(param.toStdString()) : nullptr;
        if (!p) return;
        nlohmann::json params = nlohmann::json::object();
        for (auto it = t.instrumentParams.begin(); it != t.instrumentParams.end(); ++it) params[it.key().toStdString()] = it.value().toDouble();
        params[param.toStdString()] = std::clamp(value, p->min, p->max);
        sendCommand({{"type", "set_instrument"}, {"trackId", trackId.toStdString()},
                     {"instrument", {{"processorId", t.instrument.toStdString()}, {"params", params}, {"state", ""}}}});
        return;
    }
}

namespace {
struct EngineMidi final : lpc::IMidiSink {
    explicit EngineMidi(lpc::audio::AudioEngine& e) : engine(e) {}
    void midi(unsigned char status, unsigned char d1, unsigned char d2) noexcept override { engine.pushDeviceMidi({status, d1, d2}); }
    lpc::audio::AudioEngine& engine;
};
}  // namespace

int ProjectController::midiOpenCount() const {
#ifdef JAD_HAVE_JUCE
    return midiInputs_ ? static_cast<lpc::IMidiInputs*>(midiInputs_.get())->count() : 0;
#else
    return 0;
#endif
}

QStringList ProjectController::midiInputNames() const {
    QStringList out;
#ifdef JAD_HAVE_JUCE
    for (const std::string& n : lpc::listJuceMidiInputs()) out << QString::fromStdString(n);
#endif
    return out;
}

void ProjectController::openMidi() {
    midiInputs_.reset();
    midiSink_.reset();
#ifdef JAD_HAVE_JUCE
    if (!openAudioDevice_ || !engine_) {
        emit midiChanged();
        return;
    }
    midiSink_ = std::make_unique<EngineMidi>(*engine_);
    std::vector<std::string> names;
    for (const QString& n : std::as_const(midiChosen_)) names.push_back(n.toStdString());
    std::shared_ptr<lpc::IMidiInputs> open = lpc::openJuceMidiInputs(names, *midiSink_);
    midiInputs_ = std::static_pointer_cast<void>(open);
#endif
    emit midiChanged();
}

void ProjectController::setMidiInputs(const QStringList& names) {
    midiChosen_ = names;
    if (engine_) openMidi();
    else emit midiChanged();
}

void ProjectController::loadMidiSettings(QSettings& s) {
    midiChosen_ = s.value("midi/inputs").toStringList();
    emit midiChanged();
}

void ProjectController::saveMidiSettings(QSettings& s) const { s.setValue("midi/inputs", midiChosen_); }

void ProjectController::playNote(int note, int velocity, bool on) {
    if (!engine_) return;
    note = std::clamp(note, 0, 127);
    engine_->pushUiMidi({static_cast<std::uint8_t>(on ? 0x90 : 0x80), static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(on ? std::clamp(velocity, 1, 127) : 0)});
}

// The live MIDI goes to the first armed instrument track, else to the selected instrument track.
void ProjectController::applyLiveTarget() {
    QString target;
    const QSet<QString> armed = trackToggles_.value(QStringLiteral("track.recordArm"));
    for (const TrackRow& t : allRows_)
        if (t.kind == "instrument" && armed.contains(t.id)) { target = t.id; break; }
    if (target.isEmpty())
        for (const QString& id : std::as_const(selectedTracks_))
            for (const TrackRow& t : allRows_)
                if (t.id == id && t.kind == "instrument") { target = id; break; }
    if (target == liveTarget_) return;
    liveTarget_ = target;
    if (host_) host_->setLiveTarget(lpc::Uuid::parse(target.toStdString()).value_or(lpc::Uuid{}));
}

void ProjectController::drainMidiRecording() {
    lpc::audio::AudioEngine::MidiRecEvent e;
    while (engine_ && engine_->takeMidiRecorded(e)) recMidi_.push_back(e);
}

// The notes played during the take become a MIDI region on each armed instrument track (the passes of a cycle merge).
void ProjectController::finishMidiRecording(std::int64_t startFrame, std::int64_t endFrame) {
    drainMidiRecording();
    const QStringList tracks = recMidiTracks_;
    recMidiTracks_.clear();
    std::vector<lpc::audio::AudioEngine::MidiRecEvent> events;
    events.swap(recMidi_);
    if (tracks.isEmpty() || events.empty() || !host_) return;
    auto toTicks = [this](std::int64_t frames) { return static_cast<std::int64_t>(tempoMap_.samplesToTicks(static_cast<double>(frames), sampleRate_)); };
    const std::int64_t startTick = toTicks(startFrame);
    const std::int64_t barTicks = static_cast<std::int64_t>(std::llround(barBeats() * lpc::kPPQ));
    struct Open { std::int64_t tick; int velocity; };
    std::map<int, Open> held;
    nlohmann::json notes = nlohmann::json::array();
    nlohmann::json controls = nlohmann::json::array();
    std::int64_t lastTick = startTick;
    auto close = [&](int note, std::int64_t offTick) {
        const auto it = held.find(note);
        if (it == held.end()) return;
        const std::int64_t length = std::max<std::int64_t>(offTick - it->second.tick, lpc::kPPQ / 32);
        notes.push_back({{"start", it->second.tick - startTick}, {"length", length}, {"note", note}, {"velocity", it->second.velocity}});
        lastTick = std::max(lastTick, it->second.tick + length);
        held.erase(it);
    };
    for (const auto& e : events) {
        const std::int64_t tick = std::max(toTicks(e.position), startTick);
        const int type = e.event.status & 0xF0;
        const bool on = type == 0x90 && e.event.data2 > 0;
        const bool off = type == 0x80 || (type == 0x90 && e.event.data2 == 0);
        if (on) {
            close(e.event.data1, tick);  // a repeated note ends the one before
            held[e.event.data1] = {tick, e.event.data2};
        } else if (off) {
            close(e.event.data1, tick);
        } else if ((type == 0xB0 && e.event.data1 < 120) || type == 0xD0 || type == 0xE0) {  // the pedal, the wheels and aftertouch of the take
            controls.push_back({{"tick", tick - startTick}, {"status", type}, {"data1", e.event.data1 & 0x7f}, {"data2", type == 0xD0 ? 0 : (e.event.data2 & 0x7f)}});
            lastTick = std::max(lastTick, tick);
        }
    }
    const std::int64_t endTick = std::max(toTicks(endFrame), lastTick);
    for (const auto& [note, o] : std::map<int, Open>(held)) close(note, endTick);  // notes still held when the take stops
    if (notes.empty() && controls.empty()) return;
    const std::int64_t length = std::max<std::int64_t>(barTicks, ((endTick - startTick + barTicks - 1) / barTicks) * barTicks);
    nlohmann::json commands = nlohmann::json::array();
    const std::string nullId = "00000000-0000-0000-0000-000000000000";
    for (const QString& id : tracks)
        commands.push_back({{"type", "add_region"}, {"trackId", id.toStdString()}, {"index", -1},
                            {"region", {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()}, {"timeBase", "musical"},
                                        {"start", startTick}, {"length", length}, {"mediaId", nullId}, {"sourceOffsetFrames", 0},
                                        {"gainDb", 0}, {"notes", notes}, {"controls", controls}}}});
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
    emit notice(QString("Recorded %1 note%2").arg(static_cast<int>(notes.size())).arg(notes.size() == 1 ? "" : "s"));
}

void ProjectController::setRegionFades(const QString& regionId, double fadeInBeats, double fadeOutBeats) {
    const RegionRow* r = regions_.find(regionId);
    if (!host_ || !r || !r->audio || !std::isfinite(fadeInBeats) || !std::isfinite(fadeOutBeats)) return;
    const double len = r->lengthBeats;
    const double in = std::clamp(fadeInBeats, 0.0, len), out = std::clamp(fadeOutBeats, 0.0, len);
    // the unit of the region: ticks, or microseconds for a region in real time
    auto unit = [&](double fromBeats, double lengthBeats) -> std::int64_t {
        if (lengthBeats <= 0) return 0;
        const auto a = static_cast<lpc::Ticks>(std::llround(fromBeats * lpc::kPPQ)), b = static_cast<lpc::Ticks>(std::llround((fromBeats + lengthBeats) * lpc::kPPQ));
        if (!r->absolute) return b - a;
        return static_cast<std::int64_t>(std::llround((tempoMap_.ticksToSamples(b, sampleRate_) - tempoMap_.ticksToSamples(a, sampleRate_)) * 1e6 / sampleRate_));
    };
    sendCommand({{"type", "set_region_fades"}, {"regionId", regionId.toStdString()}, {"fadeIn", unit(r->startBeats, in)},
                 {"fadeOut", unit(r->startBeats + len - out, out)}});
}

namespace {

std::filesystem::path mediaFile(const std::filesystem::path& dir, const QString& relative) {
    const QByteArray rel = relative.toUtf8();
    return dir / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(rel.constData()), static_cast<std::size_t>(rel.size())));
}

// The part of a media file a region plays, as audio the operations can work on.
lpc::WavData readRegionAudio(const std::filesystem::path& file, std::int64_t offset, std::int64_t frames) {
    lpc::WavFile wav(file);
    lpc::WavData d;
    d.sampleRate = wav.sampleRate();
    d.channels = wav.channels();
    d.samples.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(d.channels));
    wav.readFrames(offset, frames, d.samples.data());
    return d;
}

QString sha256Of(const std::filesystem::path& file) {
    QFile f(QString::fromStdU16String(file.u16string()));
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (f.open(QIODevice::ReadOnly) && h.addData(&f)) return QString::fromLatin1(h.result().toHex());
    return {};
}

}  // namespace

void ProjectController::processSelectedRegions(const QString& op, double value) {
    if (!host_ || !std::isfinite(value)) return;
    if (op != "normalize" && op != "reverse" && op != "gain" && op != "stretch" && op != "pitch") return;
    std::vector<RegionRow> rows;
    for (const RegionRow* r : selectedRegionRows())
        if (r->audio && !r->missing && r->lengthFrames > 0) rows.push_back(*r);
    if (rows.empty()) {
        emit notice("Select an audio region first");
        return;
    }
    emit notice(QString("Processing %1 region%2…").arg(rows.size()).arg(rows.size() == 1 ? "" : "s"));
    const std::filesystem::path projectDir = dir_;
    const int rate = sampleRate_;
    QPointer<ProjectController> self(this);
    const std::uint64_t generation = generation_;
    for (const RegionRow& row : rows) {
        const std::filesystem::path source = mediaFile(projectDir, mediaPaths_.value(row.mediaId));
        (void)QtConcurrent::run([self, row, source, projectDir, rate, op, value, generation] {
            auto fail = [&](const QString& message) {
                if (self) QMetaObject::invokeMethod(self.data(), [self, message] { if (self) self->setError(message); }, Qt::QueuedConnection);
            };
            std::filesystem::path target;
            std::int64_t frames = 0;
            int channels = 0;
            try {
                lpc::WavData d = readRegionAudio(source, row.sourceOffsetFrames, row.lengthFrames);
                if (op == "normalize") lpc::normalize(d, value);
                else if (op == "reverse") lpc::reverse(d);
                else if (op == "gain") lpc::applyGain(d, value);
                else if (op == "stretch") d = lpc::timeStretch(d, value / 100.0);
                else if (op == "pitch") d = lpc::pitchShift(d, value);
                frames = d.frames();
                channels = d.channels;
                const std::filesystem::path audioDir = projectDir / "audio";
                std::filesystem::create_directories(audioDir);
                const std::filesystem::path stem = source.stem();
                for (int n = 1;; ++n) {
                    target = audioDir / (stem.u16string() + u" (" + QString(op).toStdU16String() + (n > 1 ? u" " + QString::number(n).toStdU16String() : std::u16string()) + u").wav");
                    if (!std::filesystem::exists(target)) break;
                }
                lpc::writeWav(target, rate, channels, d.samples, lpc::WavFormat::Pcm24);
            } catch (const std::exception& e) {
                fail(QString("Cannot process the region: ") + QString::fromUtf8(e.what()));
                return;
            }
            const QString hash = sha256Of(target);
            if (!self) return;
            QMetaObject::invokeMethod(self.data(), [self, row, target, hash, frames, channels, rate, projectDir, generation] {
                std::error_code ignore;
                if (!self || generation != self->generation_ || !self->host_ || self->dir_ != projectDir) {
                    std::filesystem::remove(target, ignore);
                    return;
                }
                lpc::MediaItem item;
                item.id = lpc::Uuid::random();
                const std::u8string rel = (std::filesystem::path("audio") / target.filename()).generic_u8string();
                item.path.assign(rel.begin(), rel.end());
                item.hash = hash.toStdString();
                item.sampleRate = rate;
                item.channels = channels;
                item.frames = frames;
                nlohmann::json region = nlohmann::json::parse(row.json, nullptr, false);
                if (region.is_discarded()) return;
                region["mediaId"] = item.id.toString();
                region["sourceOffsetFrames"] = 0;
                // the length in the unit of the region: microseconds, or ticks of the tempo map
                const std::int64_t startUnit = region["start"].get<std::int64_t>();
                const std::int64_t newLength = row.absolute
                    ? static_cast<std::int64_t>(std::llround(static_cast<double>(frames) * 1e6 / rate))
                    : static_cast<std::int64_t>(self->tempoMap_.samplesToTicks(self->tempoMap_.ticksToSamples(startUnit, rate) + static_cast<double>(frames), rate)) - startUnit;
                region["length"] = std::max<std::int64_t>(1, newLength);
                const double fadeLimit = region["length"].get<double>();
                if (region.value("fadeIn", 0) > fadeLimit) region["fadeIn"] = 0;
                if (region.value("fadeOut", 0) > fadeLimit) region["fadeOut"] = 0;
                nlohmann::json commands = nlohmann::json::array();
                commands.push_back({{"type", "add_media"}, {"item", item}, {"index", -1}});
                commands.push_back({{"type", "replace_region"}, {"region", region}});
                self->sendCommand({{"type", "transaction"}, {"commands", commands}}, [target](bool accepted) {
                    if (!accepted) { std::error_code ec; std::filesystem::remove(target, ec); }
                });
            }, Qt::QueuedConnection);
        });
    }
}

void ProjectController::stripSilence(double thresholdDb, double minSilenceMs) {
    if (!host_ || !std::isfinite(thresholdDb) || !std::isfinite(minSilenceMs)) return;
    std::vector<RegionRow> rows;
    for (const RegionRow* r : selectedRegionRows())
        if (r->audio && !r->missing && r->lengthFrames > 0) rows.push_back(*r);
    if (rows.empty()) {
        emit notice("Select an audio region first");
        return;
    }
    const std::filesystem::path projectDir = dir_;
    const int rate = sampleRate_;
    QPointer<ProjectController> self(this);
    const std::uint64_t generation = generation_;
    for (const RegionRow& row : rows) {
        const std::filesystem::path source = mediaFile(projectDir, mediaPaths_.value(row.mediaId));
        (void)QtConcurrent::run([self, row, source, projectDir, rate, thresholdDb, minSilenceMs, generation] {
            std::vector<std::pair<std::int64_t, std::int64_t>> sounds;
            try {
                sounds = lpc::findSounds(readRegionAudio(source, row.sourceOffsetFrames, row.lengthFrames), thresholdDb, minSilenceMs);
            } catch (const std::exception& e) {
                const QString message = QString("Cannot read the region: ") + QString::fromUtf8(e.what());
                if (self) QMetaObject::invokeMethod(self.data(), [self, message] { if (self) self->setError(message); }, Qt::QueuedConnection);
                return;
            }
            if (!self) return;
            QMetaObject::invokeMethod(self.data(), [self, row, sounds, rate, projectDir, generation] {
                if (!self || generation != self->generation_ || !self->host_ || self->dir_ != projectDir) return;
                if (sounds.empty()) { emit self->notice("No sound above the threshold: nothing was changed"); return; }
                if (sounds.size() == 1 && sounds[0].first <= 0 && sounds[0].second >= row.lengthFrames) { emit self->notice("The region has no silence to strip"); return; }
                nlohmann::json original = nlohmann::json::parse(row.json, nullptr, false);
                if (original.is_discarded()) return;
                const std::int64_t startMicros = original["start"].get<std::int64_t>();
                // frames after the start of the region, in its own unit (microseconds, or ticks of the tempo map)
                auto micros = [self, rate, startMicros, absolute = row.absolute](std::int64_t frames) {
                    if (absolute) return static_cast<std::int64_t>(std::llround(static_cast<double>(frames) * 1e6 / rate));
                    return static_cast<std::int64_t>(self->tempoMap_.samplesToTicks(self->tempoMap_.ticksToSamples(startMicros, rate) + static_cast<double>(frames), rate)) - startMicros;
                };
                nlohmann::json commands = nlohmann::json::array();
                commands.push_back({{"type", "remove_region"}, {"regionId", row.id.toStdString()}});
                for (std::size_t k = 0; k < sounds.size(); ++k) {
                    nlohmann::json part = original;
                    part["id"] = lpc::Uuid::random().toString();
                    part["sourceOffsetFrames"] = row.sourceOffsetFrames + sounds[k].first;
                    part["start"] = startMicros + micros(sounds[k].first);
                    part["length"] = std::max<std::int64_t>(1, micros(sounds[k].second) - micros(sounds[k].first));
                    if (k > 0) part.erase("fadeIn");
                    if (k + 1 < sounds.size()) part.erase("fadeOut");
                    commands.push_back({{"type", "add_region"}, {"trackId", row.trackId.toStdString()}, {"region", part}, {"index", -1}});
                }
                self->sendCommand({{"type", "transaction"}, {"commands", commands}});
                emit self->notice(QString("Strip Silence: %1 part%2").arg(static_cast<int>(sounds.size())).arg(sounds.size() == 1 ? "" : "s"));
            }, Qt::QueuedConnection);
        });
    }
}

void ProjectController::trimRegionToFrames(const QString& regionId, double fromFrame, double toFrame) {
    const RegionRow* r = regions_.find(regionId);
    if (!host_ || !r || !r->audio || !std::isfinite(fromFrame) || !std::isfinite(toFrame) || toFrame <= fromFrame) return;
    const double from = std::clamp(fromFrame, 0.0, static_cast<double>(r->lengthFrames)), to = std::clamp(toFrame, from, static_cast<double>(r->lengthFrames));
    if (to - from < 16) return;
    // frames after the start of the region -> beats (the tempo map: the region may cross tempo changes)
    const auto startTick = static_cast<lpc::Ticks>(std::llround(r->startBeats * lpc::kPPQ));
    const double startSamples = tempoMap_.ticksToSamples(startTick, sampleRate_);
    auto beatsAt = [&](double frames) { return static_cast<double>(tempoMap_.samplesToTicks(startSamples + frames, sampleRate_)) / lpc::kPPQ; };
    const double newStart = beatsAt(from), newEnd = beatsAt(to);
    sendCommand(resizeCommand(*r, newStart, newEnd - newStart));
}

namespace {
QVariantMap groupMap(const GroupRow& g) {
    return {{"id", g.id}, {"name", g.name}, {"members", g.members}, {"volume", g.volume}, {"pan", g.pan}, {"mute", g.mute}, {"solo", g.solo}, {"selection", g.selection}};
}
}  // namespace

QVariantList ProjectController::groups() const {
    QVariantList out;
    for (const GroupRow& g : groupRows_) out.append(groupMap(g));
    return out;
}

QVariantMap ProjectController::trackGroup(const QString& trackId) const {
    for (const GroupRow& g : groupRows_)
        if (g.members.contains(trackId)) return groupMap(g);
    return {};
}

void ProjectController::sendGroups(const std::vector<GroupRow>& rows) {
    nlohmann::json list = nlohmann::json::array();
    for (const GroupRow& g : rows) {
        nlohmann::json members = nlohmann::json::array();
        for (const QString& m : g.members) members.push_back(m.toStdString());
        list.push_back({{"id", g.id.toStdString()}, {"name", g.name.left(64).toStdString()}, {"members", members}, {"volume", g.volume}, {"pan", g.pan},
                        {"mute", g.mute}, {"solo", g.solo}, {"selection", g.selection}});
    }
    sendCommand({{"type", "set_groups"}, {"groups", list}});
}

void ProjectController::createGroup(const QStringList& trackIds, const QString& name) {
    if (!host_ || trackIds.isEmpty()) return;
    auto rows = groupRows_;
    for (GroupRow& g : rows)
        for (const QString& id : trackIds) g.members.removeAll(id);  // a track is in one group only
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const GroupRow& g) { return g.members.isEmpty(); }), rows.end());
    GroupRow g;
    g.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    g.name = name.trimmed().isEmpty() ? QStringLiteral("Group %1").arg(static_cast<int>(rows.size()) + 1) : name.trimmed();
    for (const QString& id : trackIds)
        if (tracks_.find(id) && !g.members.contains(id)) g.members << id;
    if (g.members.isEmpty()) return;
    rows.push_back(g);
    sendGroups(rows);
}

void ProjectController::createGroupFromSelection() {
    if (selectedTracks_.isEmpty()) {
        emit notice("Select the tracks of the group first");
        return;
    }
    createGroup(selectedTracks_);
}

void ProjectController::setTrackGroup(const QString& trackId, const QString& groupId) {
    if (!host_ || !tracks_.find(trackId)) return;
    if (groupId == "new") {
        createGroup(selectedTracks_.contains(trackId) ? selectedTracks_ : QStringList{trackId});
        return;
    }
    auto rows = groupRows_;
    for (GroupRow& g : rows) g.members.removeAll(trackId);
    if (!groupId.isEmpty())
        for (GroupRow& g : rows)
            if (g.id == groupId) g.members << trackId;
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const GroupRow& g) { return g.members.isEmpty(); }), rows.end());  // an empty group goes
    sendGroups(rows);
}

void ProjectController::setGroupField(const QString& groupId, const QString& field, const QVariant& value) {
    if (!host_) return;
    auto rows = groupRows_;
    for (GroupRow& g : rows) {
        if (g.id != groupId) continue;
        if (field == "name") { const QString n = value.toString().trimmed(); if (n.isEmpty()) return; g.name = n; }
        else if (field == "volume") g.volume = value.toBool();
        else if (field == "pan") g.pan = value.toBool();
        else if (field == "mute") g.mute = value.toBool();
        else if (field == "solo") g.solo = value.toBool();
        else if (field == "selection") g.selection = value.toBool();
        else return;
        sendGroups(rows);
        return;
    }
}

void ProjectController::deleteGroup(const QString& groupId) {
    if (!host_) return;
    auto rows = groupRows_;
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const GroupRow& g) { return g.id == groupId; }), rows.end());
    sendGroups(rows);
}

void ProjectController::setAutomationMode(const QString& trackId, const QString& mode) {
    if (!host_ || (mode != "off" && mode != "read" && mode != "touch" && mode != "latch" && mode != "write")) return;
    finishAutomationCapture(trackId);
    sendCommand({{"type", "set_track_props"}, {"trackId", trackId.toStdString()}, {"automationMode", mode.toStdString()}});
}

QString ProjectController::automationMode(const QString& trackId) const {
    for (const TrackRow& t : allRows_)
        if (t.id == trackId) return t.automationMode;
    return QStringLiteral("read");
}

namespace {
const TrackRow* findTrackRow(const std::vector<TrackRow>& rows, const QString& id) {
    for (const TrackRow& t : rows)
        if (t.id == id) return &t;
    return nullptr;
}
}  // namespace

// A fader or pan move while the project plays and the track is in a writing mode: keep it with its position.
void ProjectController::captureAutomation(const QString& trackId, bool isPan, double value) {
    if (!playing_) return;
    const TrackRow* t = findTrackRow(allRows_, trackId);
    if (!t || t->master) return;
    const bool touch = t->automationMode == "touch", latch = t->automationMode == "latch", write = t->automationMode == "write";
    if (!touch && !latch && !write) return;
    if (touch && !gestureActive_) return;  // touch writes only while the fader is held
    AutoCapture& c = autoCapture_[trackId];
    if (latch) c.latched = true;
    auto& list = isPan ? c.pan : c.gain;
    if (!list.empty() && std::abs(list.back().first - positionBeats_) < 1e-9) list.back().second = value;
    else list.emplace_back(positionBeats_, value);
}

// While the project plays, a track in write mode (or in a latch take) is written with the position and the value of its fader.
void ProjectController::tickAutomationWrite() {
    for (const TrackRow& t : allRows_) {
        if (t.master) continue;
        const bool write = t.automationMode == "write";
        const bool latched = t.automationMode == "latch" && autoCapture_.contains(t.id) && autoCapture_[t.id].latched;
        if (!write && !latched) continue;
        if (gestureActive_) continue;  // the drag itself is captured move by move
        AutoCapture& c = autoCapture_[t.id];
        if (c.gain.empty() || std::abs(c.gain.back().first - positionBeats_) > 1e-9) c.gain.emplace_back(positionBeats_, t.gainDb);
        if (c.pan.empty() || std::abs(c.pan.back().first - positionBeats_) > 1e-9) c.pan.emplace_back(positionBeats_, t.pan);
    }
}

void ProjectController::finishAutomationCaptures(bool latchedToo) {
    const QStringList ids = autoCapture_.keys();
    for (const QString& id : ids) {
        const TrackRow* t = findTrackRow(allRows_, id);
        const bool keepGoing = !latchedToo && t && (t->automationMode == "latch" || t->automationMode == "write");
        if (!keepGoing) finishAutomationCapture(id);
    }
}

// The written moves replace the lane between their first and last point (the points of the lane outside stay), thinned out.
void ProjectController::finishAutomationCapture(const QString& trackId) {
    const auto it = autoCapture_.find(trackId);
    if (it == autoCapture_.end()) return;
    const AutoCapture c = it.value();
    autoCapture_.erase(it);
    const TrackRow* t = findTrackRow(allRows_, trackId);
    if (!t || !host_) return;
    auto merge = [&](const std::vector<std::pair<double, double>>& moves, const std::vector<AutoRow>& old, const char* target, double eps) {
        if (moves.size() < 1) return;
        std::vector<std::pair<double, double>> kept;  // thinned: a point stays when it differs from the last kept one or ends the take
        for (std::size_t i = 0; i < moves.size(); ++i)
            if (kept.empty() || i + 1 == moves.size() || std::abs(moves[i].second - kept.back().second) > eps) kept.push_back(moves[i]);
        const double from = moves.front().first, to = moves.back().first;
        QVariantList points;
        for (const AutoRow& p : old)
            if (p.beats < from - 1e-9 || p.beats > to + 1e-9) points.append(QVariantMap{{"beats", p.beats}, {"value", p.value}});
        for (const auto& [b, v] : kept) points.append(QVariantMap{{"beats", b}, {"value", v}});
        setAutomationPoints(trackId, QString::fromLatin1(target), points);
    };
    merge(c.gain, t->volumeAuto, "volume", 0.05);
    merge(c.pan, t->panAuto, "pan", 0.01);
}

QString ProjectController::inputLabel(int input) const {
    const QString label = input >= 1 && input <= inputLabels_.size() ? inputLabels_.at(input - 1).trimmed() : QString();
    return label.isEmpty() ? QStringLiteral("Input %1").arg(input) : label;
}

void ProjectController::setInputLabel(int input, const QString& label) {
    if (input < 1 || input > 64) return;
    while (inputLabels_.size() < input) inputLabels_ << QString();
    const QString clean = label.trimmed().left(32);
    if (inputLabels_[input - 1] == clean) return;
    inputLabels_[input - 1] = clean;
    ++inputLabelsRevision_;
    emit inputLabelsChanged();
    emit audioSettingsChanged();  // the Input menus list the labels
}

void ProjectController::loadIoSettings(QSettings& s) {
    inputLabels_ = s.value("io/inputLabels").toStringList();
    ++inputLabelsRevision_;
    emit inputLabelsChanged();
}

void ProjectController::saveIoSettings(QSettings& s) const { s.setValue("io/inputLabels", inputLabels_); }

void ProjectController::createSummingStack(const QString& name) {
    if (!host_) return;
    QStringList members;
    for (const QString& id : std::as_const(selectedTracks_))
        if (const TrackRow* t = tracks_.find(id); t && (t->kind == "audio" || t->kind == "instrument")) members << id;
    if (members.isEmpty()) {
        emit notice("Select the tracks of the stack first");
        return;
    }
    int stacks = 0;
    for (const TrackRow& t : allRows_)
        if (t.kind == "bus") ++stacks;
    const QString label = name.trimmed().isEmpty() ? QStringLiteral("Stack %1").arg(stacks + 1) : name.trimmed().left(60);
    const QString busId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const std::string nullId = "00000000-0000-0000-0000-000000000000";
    nlohmann::json commands = nlohmann::json::array();
    commands.push_back({{"type", "add_track"}, {"index", -1},
                        {"track", {{"id", busId.toStdString()}, {"kind", "bus"}, {"name", label.toStdString()}, {"color", ""},
                                   {"strip", {{"gainDb", 0}, {"pan", 0}, {"mute", false}, {"solo", false}, {"inserts", nlohmann::json::array()},
                                              {"sends", nlohmann::json::array()}, {"output", nullId}}},
                                   {"regions", nlohmann::json::array()}, {"automation", nlohmann::json::array()}, {"instrument", nullptr}}}});
    for (const QString& id : members)
        commands.push_back({{"type", "set_output"}, {"trackId", id.toStdString()}, {"output", busId.toStdString()}});
    sendCommand({{"type", "transaction"}, {"commands", commands}});
    emit notice(QString("Created %1 for %2 track%3").arg(label).arg(members.size()).arg(members.size() == 1 ? "" : "s"));
}

void ProjectController::closeProject() {
    if (!host_ && !engine_) return;
    finishAutomationCaptures(true);
    recording_ = false;
    teardown();
    ++generation_;
    importQueue_.clear();
    dir_.clear();
    allRows_.clear();
    regionRows_.clear();
    markerRows_.clear();
    groupRows_.clear();
    tracks_.reset(std::vector<TrackRow>());
    mixer_.reset(std::vector<TrackRow>());
    regions_.reset(std::vector<RegionRow>());
    selectedTracks_.clear();
    selectedRegions_.clear();
    name_.clear();
    liveTarget_.clear();
    playing_ = false;
    positionBeats_ = 0;
    emit selectionChanged();
    emit playingChanged();
    emit positionChanged();
    emit projectChanged();
    emit groupsChanged();
}

bool ProjectController::revertToSaved() {
    if (!host_) return false;
    const QUrl folder = QUrl::fromLocalFile(projectFolder());
    return openProject(folder);
}

void ProjectController::addRecent() {
    const QString path = projectFolder();
    if (path.isEmpty()) return;
    recent_.removeAll(path);
    recent_.prepend(path);
    while (recent_.size() > 10) recent_.removeLast();
    emit recentChanged();
}

QStringList ProjectController::recentProjects() const {
    QStringList out;
    for (const QString& p : recent_)
        if (QFileInfo::exists(p + "/project.json")) out << p;
    return out;
}

bool ProjectController::openRecent(const QString& folder) {
    if (!QFileInfo::exists(folder + "/project.json")) {
        setError("This project is not there any more");
        recent_.removeAll(folder);
        emit recentChanged();
        return false;
    }
    return openProject(QUrl::fromLocalFile(folder));
}

void ProjectController::loadRecent(QSettings& s) { recent_ = s.value("recent/projects").toStringList(); emit recentChanged(); }
void ProjectController::saveRecent(QSettings& s) const { s.setValue("recent/projects", recent_); }

void ProjectController::setProjectName(const QString& name) {
    const QString clean = name.trimmed().left(128);
    if (!host_ || clean.isEmpty() || clean == name_) return;
    sendCommand({{"type", "set_project_name"}, {"name", clean.toStdString()}});
}

QVariantMap ProjectController::undoHistory() const {
    if (!host_) return {{"undo", QStringList()}, {"redo", QStringList()}};
    const auto h = host_->history().get();
    QStringList undo, redo;
    for (const std::string& l : h.first) undo << QString::fromStdString(l);
    for (const std::string& l : h.second) redo << QString::fromStdString(l);
    return {{"undo", undo}, {"redo", redo}};
}

void ProjectController::undoSteps(int count) { for (int i = 0; i < std::clamp(count, 0, 1000); ++i) undo(); }
void ProjectController::redoSteps(int count) { for (int i = 0; i < std::clamp(count, 0, 1000); ++i) redo(); }

void ProjectController::clearUndoHistory() {
    if (host_) host_->clearHistory().get();
    emit notice("The undo history was deleted");
}

void ProjectController::setShowHiddenTracks(bool on) {
    if (on == showHidden_) return;
    showHidden_ = on;
    emit showHiddenTracksChanged();
    if (host_) refresh(host_->revision());
}

void ProjectController::hideSelectedTracks() {
    nlohmann::json commands = nlohmann::json::array();
    for (const QString& id : std::as_const(selectedTracks_))
        if (const TrackRow* t = tracks_.find(id); t && !t->master && !t->hidden)
            commands.push_back({{"type", "set_track_props"}, {"trackId", id.toStdString()}, {"showInTracks", false}});
    if (commands.empty()) return;
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::hideUnselectedTracks() {
    if (selectedTracks_.isEmpty()) {
        emit notice("Select the tracks to keep first");
        return;
    }
    nlohmann::json commands = nlohmann::json::array();
    for (const TrackRow& t : allRows_)
        if (!t.master && !t.hidden && t.showInTracks && !selectedTracks_.contains(t.id))
            commands.push_back({{"type", "set_track_props"}, {"trackId", t.id.toStdString()}, {"showInTracks", false}});
    if (commands.empty()) return;
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::unhideAllTracks() {
    nlohmann::json commands = nlohmann::json::array();
    for (const TrackRow& t : allRows_)
        if (!t.master && t.hidden) commands.push_back({{"type", "set_track_props"}, {"trackId", t.id.toStdString()}, {"showInTracks", true}});
    if (commands.empty()) return;
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::sortTracks(const QString& by) {
    static const QStringList kinds{"audio", "instrument", "aux", "bus"};
    std::vector<const TrackRow*> rows;
    for (const TrackRow& t : allRows_)
        if (!t.master) rows.push_back(&t);
    auto key = [&](const TrackRow* t) {
        if (by == "type") return QString::number(kinds.indexOf(t->kind) + 1).rightJustified(2, '0') + t->name.toLower();
        if (by == "color") return t->color + QLatin1Char('') + t->name.toLower();
        return t->name.toLower();
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const TrackRow* a, const TrackRow* b) { return QString::localeAwareCompare(key(a), key(b)) < 0; });
    nlohmann::json order = nlohmann::json::array();
    bool changed = false;
    std::size_t i = 0;
    for (const TrackRow& t : allRows_) {
        if (t.master) continue;
        if (rows[i]->id != t.id) changed = true;
        ++i;
    }
    for (const TrackRow* t : rows) order.push_back(t->id.toStdString());
    if (!changed) return;
    sendCommand({{"type", "set_track_order"}, {"order", order}});
}

void ProjectController::setSelectedTracksColor(const QString& color) {
    nlohmann::json commands = nlohmann::json::array();
    for (const QString& id : std::as_const(selectedTracks_))
        if (const TrackRow* t = tracks_.find(id); t && !t->master && t->color != color)
            commands.push_back({{"type", "set_track_props"}, {"trackId", id.toStdString()}, {"color", color.toStdString()}});
    if (commands.empty()) return;
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::deleteAutomationOfSelected() {
    nlohmann::json commands = nlohmann::json::array();
    for (const QString& id : std::as_const(selectedTracks_)) {
        const TrackRow* t = tracks_.find(id);
        if (!t || t->master) continue;
        if (!t->volumeAuto.empty()) commands.push_back({{"type", "set_automation"}, {"trackId", id.toStdString()}, {"target", "volume"}, {"points", nlohmann::json::array()}});
        if (!t->panAuto.empty()) commands.push_back({{"type", "set_automation"}, {"trackId", id.toStdString()}, {"target", "pan"}, {"points", nlohmann::json::array()}});
    }
    if (commands.empty()) {
        emit notice("The selected tracks have no automation");
        return;
    }
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::createTrackAutomation() {
    setAutomationVisible(true);
    nlohmann::json commands = nlohmann::json::array();
    for (const QString& id : std::as_const(selectedTracks_))
        if (const TrackRow* t = tracks_.find(id); t && !t->master && t->automationMode == QLatin1String("off"))
            commands.push_back({{"type", "set_track_props"}, {"trackId", id.toStdString()}, {"automationMode", "read"}});
    if (commands.empty()) return;
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::importMidiFile(const QUrl& file) {
    if (!host_) return;
    QFile f(file.toLocalFile());
    if (!f.open(QIODevice::ReadOnly)) {
        setError("Cannot open " + file.toLocalFile());
        return;
    }
    const QByteArray raw = f.readAll();
    lpc::MidiFileData data;
    try {
        data = lpc::parseMidiFile(std::vector<std::uint8_t>(raw.begin(), raw.end()));
    } catch (const std::exception& e) {
        setError(QString("Cannot import the MIDI file: ") + e.what());
        return;
    }
    if (data.tracks.empty()) {
        setError("The MIDI file has no notes");
        return;
    }
    const std::string nullId = "00000000-0000-0000-0000-000000000000";
    const lpc::Ticks bar = std::max<lpc::Ticks>(lpc::kPPQ, static_cast<lpc::Ticks>(beatsPerBar_) * lpc::kPPQ * 4 / std::max(1, beatUnit_));
    const lpc::Ticks base = static_cast<lpc::Ticks>(std::llround(std::max(0.0, positionBeats_) * lpc::kPPQ));
    const QString baseName = QFileInfo(file.toLocalFile()).completeBaseName();
    nlohmann::json commands = nlohmann::json::array();
    for (std::size_t i = 0; i < data.tracks.size(); ++i) {
        const lpc::MidiFileTrack& t = data.tracks[i];
        if (t.notes.empty() && t.controls.empty()) continue;
        lpc::Ticks end = 0;
        for (const lpc::MidiNote& n : t.notes) end = std::max(end, n.start + n.length);
        for (const lpc::MidiControl& c : t.controls) end = std::max(end, c.tick);
        lpc::Region region;
        region.id = lpc::Uuid::random();
        region.timeBase = lpc::TimeBase::Musical;
        region.start = base;
        region.length = (end + bar - 1) / bar * bar;
        region.notes = t.notes;
        region.controls = t.controls;
        nlohmann::json regionJson = region;
        std::string name = t.name.empty() ? (data.tracks.size() == 1 ? baseName.toStdString() : baseName.toStdString() + " " + std::to_string(i + 1)) : t.name;
        nlohmann::json track = {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()},
                                {"kind", "instrument"},
                                {"name", name.substr(0, 60)},
                                {"color", ""},
                                {"strip", {{"gainDb", 0}, {"pan", 0}, {"mute", false}, {"solo", false}, {"inserts", nlohmann::json::array()},
                                           {"sends", nlohmann::json::array()}, {"output", nullId}}},
                                {"regions", nlohmann::json::array({regionJson})},
                                {"automation", nlohmann::json::array()},
                                {"instrument", {{"processorId", "builtin.sine"}, {"params", nlohmann::json::object()}, {"state", ""}}}};
        commands.push_back({{"type", "add_track"}, {"index", -1}, {"track", track}});
    }
    if (commands.empty()) {
        setError("The MIDI file has no notes");
        return;
    }
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
    emit notice(QString("Imported %1 MIDI track(s)").arg(commands.size()));
}

void ProjectController::exportMidiFile(const QUrl& file) {
    if (!host_) return;
    const QStringList regionIds = selectedRegions_;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    lpc::MidiFileData data;
    data.bpm = tempoMap_.bpmAt(0);
    data.numerator = beatsPerBar_;
    data.denominator = beatUnit_;
    for (const lpc::Track& t : project.tracks) {
        if (t.kind != lpc::TrackKind::Instrument) continue;
        lpc::MidiFileTrack out;
        out.name = t.name;
        out.channel = static_cast<int>(data.tracks.size() % 16);
        if (out.channel == 9) out.channel = 10;  // channel 10 is for drums in General MIDI
        for (const lpc::Region& r : t.regions) {
            if (r.timeBase != lpc::TimeBase::Musical) continue;
            if (!regionIds.isEmpty() && !regionIds.contains(QString::fromStdString(r.id.toString()))) continue;
            for (const lpc::MidiNote& n : r.notes) {
                if (n.muted) continue;
                lpc::MidiNote m = n;
                m.start += r.start;
                out.notes.push_back(m);
            }
            for (const lpc::MidiControl& c : r.controls) {
                lpc::MidiControl m = c;
                m.tick += r.start;
                out.controls.push_back(m);
            }
        }
        if (!out.notes.empty() || !out.controls.empty()) data.tracks.push_back(std::move(out));
    }
    if (data.tracks.empty()) {
        setError("There are no MIDI notes to export");
        return;
    }
    const std::vector<std::uint8_t> bytes = lpc::writeMidiFile(data);
    QString path = file.toLocalFile();
    if (QFileInfo(path).suffix().isEmpty()) path += ".mid";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size())) {
        setError("Cannot write " + path);
        return;
    }
    emit notice(QString("Exported %1 MIDI track(s)").arg(data.tracks.size()));
}

void ProjectController::setControlBarVisible(bool on) {
    if (on == controlBarVisible_) return;
    controlBarVisible_ = on;
    QSettings().setValue("panels/controlBar", on);
    emit barsChanged();
}

void ProjectController::setToolbarVisible(bool on) {
    if (on == toolbarVisible_) return;
    toolbarVisible_ = on;
    QSettings().setValue("panels/toolbar", on);
    emit barsChanged();
}

QVariantList ProjectController::projectAudio() const {
    QVariantList out;
    if (!host_) return out;
    struct Item {
        lpc::MediaItem media;
        int used = 0;
    };
    const std::vector<Item> items = host_->read([](const lpc::Project& p) {
        std::vector<Item> v;
        for (const lpc::MediaItem& m : p.mediaPool) v.push_back({m, 0});
        for (const lpc::Track& t : p.tracks)
            for (const lpc::Region& r : t.regions)
                for (Item& it : v)
                    if (it.media.id == r.mediaId) ++it.used;
        return v;
    }).get();
    for (const Item& it : items) {
        const QString path = QString::fromStdString(it.media.path);
        out.push_back(QVariantMap{{"name", QFileInfo(path).fileName()},
                                  {"path", path},
                                  {"seconds", it.media.sampleRate > 0 ? static_cast<double>(it.media.frames) / it.media.sampleRate : 0.0},
                                  {"sampleRate", it.media.sampleRate},
                                  {"channels", it.media.channels},
                                  {"used", it.used}});
    }
    return out;
}

QString ProjectController::projectNotes() const {
    if (dir_.empty()) return {};
    QFile f(QString::fromStdWString((dir_ / "notes.txt").wstring()));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

void ProjectController::setProjectNotes(const QString& text) {
    if (dir_.empty() || text == projectNotes()) return;
    QFile f(QString::fromStdWString((dir_ / "notes.txt").wstring()));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError("Cannot save the notes");
        return;
    }
    f.write(text.toUtf8());
}

namespace {
// The lanes of a controller editor: "cc<n>" (control change n), "bend" (pitch bend) and "touch" (channel aftertouch).
struct ControlLane {
    std::uint8_t status = 0xB0, controller = 0;
    bool valid = false;
};
ControlLane parseLane(const QString& lane) {
    ControlLane out;
    if (lane == QLatin1String("bend")) return {0xE0, 0, true};
    if (lane == QLatin1String("touch")) return {0xD0, 0, true};
    if (lane.startsWith(QLatin1String("cc"))) {
        bool ok = false;
        const int n = lane.mid(2).toInt(&ok);
        if (ok && n >= 0 && n < 120) return {0xB0, static_cast<std::uint8_t>(n), true};
    }
    return out;
}
}  // namespace

QVariantList ProjectController::regionControls(const QString& regionId, const QString& lane) const {
    QVariantList out;
    const RegionRow* row = regions_.find(regionId);
    const ControlLane l = parseLane(lane);
    if (!row || row->audio || !l.valid) return out;
    const nlohmann::json j = nlohmann::json::parse(row->json, nullptr, false);
    if (j.is_discarded() || !j.contains("controls") || !j["controls"].is_array()) return out;
    for (const auto& c : j["controls"]) {
        const int status = c.value("status", 0), d1 = c.value("data1", 0), d2 = c.value("data2", 0);
        if (status != l.status || (status == 0xB0 && d1 != l.controller)) continue;
        // control change: the value 0..127; aftertouch: 0..127; pitch bend: -8192..8191 around the centre
        const int value = status == 0xB0 ? d2 : (status == 0xD0 ? d1 : (d1 | (d2 << 7)) - 8192);
        out.append(QVariantMap{{"beats", c.value("tick", 0) / static_cast<double>(lpc::kPPQ)}, {"value", value}});
    }
    return out;
}

void ProjectController::setRegionControls(const QString& regionId, const QString& lane, const QVariantList& points) {
    if (!host_) return;
    const RegionRow* row = regions_.find(regionId);
    const ControlLane l = parseLane(lane);
    if (!row || row->audio || !l.valid) return;
    nlohmann::json region = nlohmann::json::parse(row->json, nullptr, false);
    if (region.is_discarded()) return;
    const std::int64_t length = region.value("length", std::int64_t{0});
    std::vector<nlohmann::json> list;
    if (region.contains("controls") && region["controls"].is_array())
        for (const auto& c : region["controls"]) {  // the other lanes stay as they are
            const int status = c.value("status", 0), d1 = c.value("data1", 0);
            if (status == l.status && (status != 0xB0 || d1 == l.controller)) continue;
            list.push_back(c);
        }
    for (const QVariant& v : points) {
        const QVariantMap m = v.toMap();
        const double beats = m.value("beats").toDouble();
        const double value = m.value("value").toDouble();
        if (!std::isfinite(beats) || !std::isfinite(value)) continue;
        const std::int64_t tick = std::clamp<std::int64_t>(static_cast<std::int64_t>(std::llround(beats * lpc::kPPQ)), 0, length);
        if (l.status == 0xE0) {
            const int bend = std::clamp(static_cast<int>(std::lround(value)), -8192, 8191) + 8192;
            list.push_back({{"tick", tick}, {"status", 0xE0}, {"data1", bend & 0x7f}, {"data2", bend >> 7}});
        } else if (l.status == 0xD0) {
            list.push_back({{"tick", tick}, {"status", 0xD0}, {"data1", std::clamp(static_cast<int>(std::lround(value)), 0, 127)}, {"data2", 0}});
        } else {
            list.push_back({{"tick", tick}, {"status", 0xB0}, {"data1", static_cast<int>(l.controller)}, {"data2", std::clamp(static_cast<int>(std::lround(value)), 0, 127)}});
        }
    }
    std::stable_sort(list.begin(), list.end(), [](const nlohmann::json& a, const nlohmann::json& b) { return a.value("tick", 0) < b.value("tick", 0); });
    if (list.empty()) region.erase("controls");
    else region["controls"] = list;
    sendCommand({{"type", "replace_region"}, {"region", region}});
}

}  // namespace jad
