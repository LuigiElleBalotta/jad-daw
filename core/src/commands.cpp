#include "lpc/commands.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

#include "lpc/model_json.h"
#include "lpc/processor_ids.h"
#include "lpc/validation.h"

namespace lpc {

namespace {

using nlohmann::json;

ApplyResult fail(CommandError e) {
    ApplyResult r;
    r.error = std::move(e);
    return r;
}
ApplyResult fail(std::string code, std::string message) { return fail(CommandError{std::move(code), std::move(message)}); }
ApplyResult success(CommandPtr inverse) {
    ApplyResult r;
    r.inverse = std::move(inverse);
    return r;
}


std::unordered_set<Uuid> allRegionIds(const Project& p) {
    std::unordered_set<Uuid> ids;
    for (const Track& t : p.tracks)
        for (const Region& r : t.regions) ids.insert(r.id);
    return ids;
}

std::unordered_set<Uuid> allSendIds(const Project& p) {
    std::unordered_set<Uuid> ids;
    for (const Track& t : p.tracks)
        for (const Send& s : t.strip.sends) ids.insert(s.id);
    return ids;
}


// ---------------------------------------------------------------- add_track / remove_track

class AddTrackCmd final : public Command {
public:
    AddTrackCmd(Track track, int index) : track_(std::move(track)), index_(index) {}
    std::string type() const override { return "add_track"; }
    json toJson() const override { return {{"type", type()}, {"track", track_}, {"index", index_}}; }

    ApplyResult apply(Project& p) const override {
        if (auto e = validate(p)) return fail(*e);
        const int idx = index_ < 0 ? static_cast<int>(p.tracks.size()) : index_;
        p.tracks.insert(p.tracks.begin() + idx, track_);
        return success(makeRemoveTrack(track_.id));
    }

private:
    MaybeError validate(const Project& p) const {
        if (track_.kind == TrackKind::Master) return CommandError{"invalid_kind", "a master track already exists"};
        if (p.tracks.size() >= kMaxProjectTracks) return CommandError{"limit", "a project holds at most 1024 tracks"};
        if (track_.id.isNull() || p.findTrack(track_.id)) return CommandError{"duplicate_id", "track id missing or already used"};
        if (auto e = checkTrackProps(track_.name, track_.color)) return e;
        if (auto e = checkShowInTracks(track_.kind, track_.showInTracks)) return e;
        if (index_ < -1 || index_ > static_cast<int>(p.tracks.size())) return CommandError{"bad_index", "track index out of range"};
        if (auto e = checkStripValues(track_.strip.gainDb, track_.strip.pan)) return e;
        if (!track_.strip.output.isNull()) {
            const Track* out = p.findTrack(track_.strip.output);
            if (!out || !isBusLike(out->kind))
                return CommandError{"bad_output", "output must be an existing bus or aux track (or null for master)"};
        }
        const bool wantsInstrument = track_.kind == TrackKind::Instrument;
        if (wantsInstrument != track_.instrument.has_value()) return CommandError{"invalid_kind", "exactly instrument tracks need an instrument"};
        if (wantsInstrument)
            if (auto e = checkInstrument(*track_.instrument)) return e;
        for (const ProcessorRef& ins : track_.strip.inserts)
            if (auto e = checkInsert(ins)) return e;
        auto sendIds = allSendIds(p);
        for (const Send& s : track_.strip.sends) {
            if (auto e = checkSendFields(p, track_.id, s)) return e;
            if (!sendIds.insert(s.id).second) return CommandError{"duplicate_id", "send id already used"};
        }
        auto regionIds = allRegionIds(p);
        for (const Region& r : track_.regions) {
            if (auto e = checkRegion(p, track_.kind, r)) return e;
            if (!regionIds.insert(r.id).second) return CommandError{"duplicate_id", "region id already used"};
        }
        return std::nullopt;
    }

    Track track_;
    int index_;
};

class RemoveTrackCmd final : public Command {
public:
    explicit RemoveTrackCmd(Uuid id) : id_(id) {}
    std::string type() const override { return "remove_track"; }
    json toJson() const override { return {{"type", type()}, {"trackId", id_}}; }

    ApplyResult apply(Project& p) const override {
        auto it = std::find_if(p.tracks.begin(), p.tracks.end(), [&](const Track& t) { return t.id == id_; });
        if (it == p.tracks.end()) return fail("not_found", "no such track");
        if (it->kind == TrackKind::Master) return fail("invalid_kind", "the master track cannot be removed");
        for (const Track& t : p.tracks) {
            if (t.id == id_) continue;
            const bool routed = t.strip.output == id_ ||
                                std::any_of(t.strip.sends.begin(), t.strip.sends.end(),
                                            [&](const Send& s) { return s.targetTrackId == id_; });
            if (routed) return fail("in_use", "another track routes to this track");
        }
        const int index = static_cast<int>(it - p.tracks.begin());
        Track removed = std::move(*it);
        p.tracks.erase(it);
        std::vector<Group> before = p.groups;  // the track leaves its group; undoing puts the groups back too
        bool inGroup = false;
        for (Group& g : p.groups) {
            const auto m = std::find(g.members.begin(), g.members.end(), id_);
            if (m != g.members.end()) { g.members.erase(m); inGroup = true; }
        }
        if (!inGroup) return success(makeAddTrack(std::move(removed), index));
        std::vector<CommandPtr> undo;
        undo.push_back(makeAddTrack(std::move(removed), index));
        undo.push_back(makeSetGroups(std::move(before)));
        return success(makeTransaction(std::move(undo)));
    }

private:
    Uuid id_;
};

// ---------------------------------------------------------------- set_strip

class SetStripCmd final : public Command {
public:
    SetStripCmd(Uuid id, StripPatch patch) : id_(id), patch_(patch) {}
    std::string type() const override { return "set_strip"; }
    json toJson() const override {
        json j = {{"type", type()}, {"trackId", id_}};
        if (patch_.gainDb) j["gainDb"] = *patch_.gainDb;
        if (patch_.pan) j["pan"] = *patch_.pan;
        if (patch_.mute) j["mute"] = *patch_.mute;
        if (patch_.solo) j["solo"] = *patch_.solo;
        if (patch_.input) j["input"] = *patch_.input;
        return j;
    }

    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(id_);
        if (!t) return fail("not_found", "no such track");
        if (auto e = checkStripValues(patch_.gainDb.value_or(t->strip.gainDb), patch_.pan.value_or(t->strip.pan)))
            return fail(*e);
        StripPatch prev;
        if (patch_.gainDb) { prev.gainDb = t->strip.gainDb; t->strip.gainDb = *patch_.gainDb; }
        if (patch_.pan) { prev.pan = t->strip.pan; t->strip.pan = *patch_.pan; }
        if (patch_.mute) { prev.mute = t->strip.mute; t->strip.mute = *patch_.mute; }
        if (patch_.solo) { prev.solo = t->strip.solo; t->strip.solo = *patch_.solo; }
        if (patch_.input) {
            if (*patch_.input < 0 || *patch_.input > 64) return fail("bad_value", "the input must be 0 (stereo 1+2) or a channel from 1 to 64");
            prev.input = t->strip.input;
            t->strip.input = *patch_.input;
        }
        return success(makeSetStrip(id_, prev));
    }

private:
    Uuid id_;
    StripPatch patch_;
};

// ---------------------------------------------------------------- tempo

class SetTempoCmd final : public Command {
public:
    SetTempoCmd(Ticks tick, double bpm) : tick_(tick), bpm_(bpm) {}
    std::string type() const override { return "set_tempo"; }
    json toJson() const override { return {{"type", type()}, {"tick", tick_}, {"bpm", bpm_}}; }

    ApplyResult apply(Project& p) const override {
        const auto previous = p.tempoMap.tempoEventAt(tick_);
        if (!p.tempoMap.setTempo(tick_, bpm_)) return fail("bad_tempo", "tempo must be 20..999 bpm at a tick >= 0");
        return success(previous ? makeSetTempo(tick_, *previous) : makeRemoveTempo(tick_));
    }

private:
    Ticks tick_;
    double bpm_;
};

class RemoveTempoCmd final : public Command {
public:
    explicit RemoveTempoCmd(Ticks tick) : tick_(tick) {}
    std::string type() const override { return "remove_tempo"; }
    json toJson() const override { return {{"type", type()}, {"tick", tick_}}; }

    ApplyResult apply(Project& p) const override {
        const auto previous = p.tempoMap.tempoEventAt(tick_);
        if (!previous || !p.tempoMap.removeTempo(tick_))
            return fail("bad_tempo", "no removable tempo event at this tick (tick 0 cannot be removed)");
        return success(makeSetTempo(tick_, *previous));
    }

private:
    Ticks tick_;
};

class SetGroupsCmd final : public Command {
public:
    explicit SetGroupsCmd(std::vector<Group> groups) : groups_(std::move(groups)) {}
    std::string type() const override { return "set_groups"; }
    json toJson() const override { return {{"type", type()}, {"groups", groups_}}; }

    ApplyResult apply(Project& p) const override {
        if (groups_.size() > 64) return fail("too_many", "at most 64 groups");
        std::unordered_set<Uuid> seenTracks;
        for (std::size_t i = 0; i < groups_.size(); ++i) {
            const Group& g = groups_[i];
            if (g.name.empty() || g.name.size() > 64) return fail("bad_group", "a group name has 1 to 64 bytes");
            for (std::size_t k = 0; k < i; ++k)
                if (groups_[k].id == g.id) return fail("bad_group", "group ids must be unique");
            for (const Uuid& m : g.members) {
                const Track* t = p.findTrack(m);
                if (!t) return fail("not_found", "a group member is not a track");
                if (t->kind == TrackKind::Master) return fail("invalid_kind", "the master track cannot be in a group");
                if (!seenTracks.insert(m).second) return fail("bad_group", "a track can be in one group only");
            }
        }
        std::vector<Group> previous = std::move(p.groups);
        p.groups = groups_;
        return success(makeSetGroups(std::move(previous)));
    }

private:
    std::vector<Group> groups_;
};

class SetTrackOrderCmd final : public Command {
public:
    explicit SetTrackOrderCmd(std::vector<Uuid> order) : order_(std::move(order)) {}
    std::string type() const override { return "set_track_order"; }
    json toJson() const override { return {{"type", type()}, {"order", order_}}; }
    ApplyResult apply(Project& p) const override {
        // order lists every track but the master exactly once; the master stays first
        std::vector<Track> sorted;
        std::vector<Uuid> previous;
        for (const Track& t : p.tracks)
            if (t.kind != TrackKind::Master) previous.push_back(t.id);
        if (order_.size() != previous.size()) return fail("bad_value", "the order must list every track but the master once");
        for (const Uuid& id : order_) {
            const Track* t = p.findTrack(id);
            if (!t || t->kind == TrackKind::Master) return fail("not_found", "unknown track in the order");
            for (const Track& done : sorted)
                if (done.id == id) return fail("bad_value", "a track is listed twice");
            sorted.push_back(*t);
        }
        std::vector<Track> next;
        for (const Track& t : p.tracks)
            if (t.kind == TrackKind::Master) next.push_back(t);
        for (Track& t : sorted) next.push_back(std::move(t));
        p.tracks = std::move(next);
        return success(makeSetTrackOrder(std::move(previous)));
    }

private:
    std::vector<Uuid> order_;
};

class SetProjectNameCmd final : public Command {
public:
    explicit SetProjectNameCmd(std::string name) : name_(std::move(name)) {}
    std::string type() const override { return "set_project_name"; }
    json toJson() const override { return {{"type", type()}, {"name", name_}}; }
    ApplyResult apply(Project& p) const override {
        if (name_.empty() || name_.size() > 512) return fail("bad_value", "a project name has 1 to 128 characters");
        for (const unsigned char c : name_)
            if (c < 0x20 || c == 0x7f) return fail("bad_value", "a project name has no control characters");
        std::string previous = p.name;
        p.name = name_;
        return success(makeSetProjectName(std::move(previous)));
    }

private:
    std::string name_;
};

class SetMarkersCmd final : public Command {
public:
    explicit SetMarkersCmd(std::vector<Marker> markers) : markers_(std::move(markers)) {}
    std::string type() const override { return "set_markers"; }
    json toJson() const override { return {{"type", type()}, {"markers", markers_}}; }

    ApplyResult apply(Project& p) const override {
        if (markers_.size() > 10000) return fail("too_many", "at most 10000 markers");
        for (std::size_t i = 0; i < markers_.size(); ++i) {
            if (markers_[i].tick < 0 || markers_[i].tick > kMaxPosition) return fail("bad_marker", "a marker position is out of range");
            if (markers_[i].name.size() > 200) return fail("bad_marker", "a marker name is longer than 200 bytes");
            for (std::size_t k = 0; k < i; ++k)
                if (markers_[k].id == markers_[i].id) return fail("bad_marker", "marker ids must be unique");
        }
        std::vector<Marker> previous = std::move(p.markers);
        p.markers = markers_;
        std::stable_sort(p.markers.begin(), p.markers.end(), [](const Marker& a, const Marker& b) { return a.tick < b.tick; });
        return success(makeSetMarkers(std::move(previous)));
    }

private:
    std::vector<Marker> markers_;
};

class SetAutomationCmd final : public Command {
public:
    SetAutomationCmd(Uuid id, std::string target, std::vector<AutomationPoint> points) : id_(id), target_(std::move(target)), points_(std::move(points)) {}
    std::string type() const override { return "set_automation"; }
    json toJson() const override { return {{"type", type()}, {"trackId", id_}, {"target", target_}, {"points", points_}}; }

    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(id_);
        if (!t) return fail("not_found", "no such track");
        if (target_ != "volume" && target_ != "pan") return fail("bad_target", "automation target must be volume or pan");
        if (points_.size() > 4096) return fail("too_many", "at most 4096 automation points per lane");
        const double lo = target_ == "volume" ? -96.0 : -1.0, hi = target_ == "volume" ? 24.0 : 1.0;
        for (const AutomationPoint& pt : points_)
            if (pt.tick < 0 || pt.tick > kMaxPosition || !(pt.value >= lo && pt.value <= hi)) return fail("bad_point", "an automation point is out of range");
        std::vector<AutomationPoint> sorted = points_;
        std::stable_sort(sorted.begin(), sorted.end(), [](const AutomationPoint& a, const AutomationPoint& b) { return a.tick < b.tick; });
        auto it = std::find_if(t->automation.begin(), t->automation.end(), [&](const AutomationLane& l) { return l.target == target_; });
        std::vector<AutomationPoint> previous;
        if (it != t->automation.end()) previous = it->points;
        if (sorted.empty()) {
            if (it != t->automation.end()) t->automation.erase(it);
        } else if (it != t->automation.end()) {
            it->points = std::move(sorted);
        } else {
            AutomationLane lane;
            lane.id = Uuid::random();
            lane.target = target_;
            lane.points = std::move(sorted);
            t->automation.push_back(std::move(lane));
        }
        return success(makeSetAutomation(id_, target_, std::move(previous)));
    }

private:
    Uuid id_;
    std::string target_;
    std::vector<AutomationPoint> points_;
};

class SetTrackPropsCmd final : public Command {
public:
    SetTrackPropsCmd(Uuid id, TrackPatch patch) : id_(id), patch_(std::move(patch)) {}
    std::string type() const override { return "set_track_props"; }
    json toJson() const override {
        json j = {{"type", type()}, {"trackId", id_}};
        if (patch_.name) j["name"] = *patch_.name;
        if (patch_.color) j["color"] = *patch_.color;
        if (patch_.showInTracks) j["showInTracks"] = *patch_.showInTracks;
        if (patch_.automationMode) j["automationMode"] = *patch_.automationMode;
        return j;
    }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(id_);
        if (!t) return fail("not_found", "no such track");
        if (t->kind == TrackKind::Master) return fail("invalid_kind", "the master track cannot be renamed or recoloured");
        if (auto e = checkTrackProps(patch_.name.value_or(t->name), patch_.color.value_or(t->color))) return fail(*e);
        if (patch_.showInTracks)
            if (auto e = checkShowInTracks(t->kind, *patch_.showInTracks)) return fail(*e);
        if (patch_.automationMode) {
            const std::string& m = *patch_.automationMode;
            if (m != "off" && m != "read" && m != "touch" && m != "latch" && m != "write") return fail("bad_value", "the automation mode is off, read, touch, latch or write");
        }
        TrackPatch previous;
        if (patch_.automationMode) {
            previous.automationMode = t->automationMode;
            t->automationMode = *patch_.automationMode;
        }
        if (patch_.name) {
            previous.name = t->name;
            t->name = *patch_.name;
        }
        if (patch_.color) {
            previous.color = t->color;
            t->color = *patch_.color;
        }
        if (patch_.showInTracks) {
            previous.showInTracks = t->showInTracks;
            t->showInTracks = *patch_.showInTracks;
        }
        return success(makeSetTrackProps(id_, std::move(previous)));
    }

private:
    Uuid id_;
    TrackPatch patch_;
};

class SetSignatureCmd final : public Command {
public:
    SetSignatureCmd(Ticks tick, int num, int den) : tick_(tick), num_(num), den_(den) {}
    std::string type() const override { return "set_signature"; }
    json toJson() const override { return {{"type", type()}, {"tick", tick_}, {"numerator", num_}, {"denominator", den_}}; }
    ApplyResult apply(Project& p) const override {
        const bool denominatorOk = den_ == 1 || den_ == 2 || den_ == 4 || den_ == 8 || den_ == 16 || den_ == 32;
        if (tick_ < 0 || tick_ > kMaxPosition || num_ < 1 || num_ > 32 || !denominatorOk)
            return fail("bad_value", "signature: numerator 1 to 32, denominator 1, 2, 4, 8, 16 or 32, tick >= 0");
        const auto previous = p.tempoMap.signatureEventAt(tick_);
        if (!p.tempoMap.setSignature(tick_, num_, den_)) return fail("bad_value", "invalid time signature");
        return success(previous ? makeSetSignature(tick_, previous->first, previous->second) : makeRemoveSignature(tick_));
    }

private:
    Ticks tick_;
    int num_, den_;
};

class RemoveSignatureCmd final : public Command {
public:
    explicit RemoveSignatureCmd(Ticks tick) : tick_(tick) {}
    std::string type() const override { return "remove_signature"; }
    json toJson() const override { return {{"type", type()}, {"tick", tick_}}; }
    ApplyResult apply(Project& p) const override {
        const auto previous = p.tempoMap.signatureEventAt(tick_);
        if (!previous || !p.tempoMap.removeSignature(tick_))
            return fail("bad_value", "no removable signature event at this tick (tick 0 cannot be removed)");
        return success(makeSetSignature(tick_, previous->first, previous->second));
    }

private:
    Ticks tick_;
};

// ---------------------------------------------------------------- media

class AddMediaCmd final : public Command {
public:
    AddMediaCmd(MediaItem item, int index) : item_(std::move(item)), index_(index) {}
    std::string type() const override { return "add_media"; }
    json toJson() const override { return {{"type", type()}, {"item", item_}, {"index", index_}}; }

    ApplyResult apply(Project& p) const override {
        if (item_.id.isNull() || p.findMedia(item_.id)) return fail("duplicate_id", "media id missing or already used");
        if (!validRelativeMediaPath(item_.path)) return fail("bad_media", "media path must be relative, inside the project, with '/' separators");
        if (item_.sampleRate != p.sampleRate) return fail("bad_media", "media sample rate must equal the project sample rate (no resampling)");
        if (item_.channels < 1 || item_.channels > 2) return fail("bad_media", "media must be mono or stereo");
        if (item_.frames < 0) return fail("bad_media", "media frame count must be >= 0");
        if (index_ < -1 || index_ > static_cast<int>(p.mediaPool.size())) return fail("bad_index", "media index out of range");
        const int idx = index_ < 0 ? static_cast<int>(p.mediaPool.size()) : index_;
        p.mediaPool.insert(p.mediaPool.begin() + idx, item_);
        return success(makeRemoveMedia(item_.id));
    }

private:
    MediaItem item_;
    int index_;
};

class RemoveMediaCmd final : public Command {
public:
    explicit RemoveMediaCmd(Uuid id) : id_(id) {}
    std::string type() const override { return "remove_media"; }
    json toJson() const override { return {{"type", type()}, {"mediaId", id_}}; }

    ApplyResult apply(Project& p) const override {
        auto it = std::find_if(p.mediaPool.begin(), p.mediaPool.end(), [&](const MediaItem& m) { return m.id == id_; });
        if (it == p.mediaPool.end()) return fail("not_found", "no such media");
        for (const Track& t : p.tracks)
            for (const Region& r : t.regions)
                if (r.mediaId == id_) return fail("in_use", "a region still uses this media");
        MediaItem removed = *it;
        const int index = static_cast<int>(it - p.mediaPool.begin());
        p.mediaPool.erase(it);
        return success(makeAddMedia(std::move(removed), index));
    }

private:
    Uuid id_;
};

// ---------------------------------------------------------------- regions

class AddRegionCmd final : public Command {
public:
    AddRegionCmd(Uuid trackId, Region region, int index) : trackId_(trackId), region_(std::move(region)), index_(index) {}
    std::string type() const override { return "add_region"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"region", region_}, {"index", index_}}; }

    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        if (allRegionIds(p).count(region_.id)) return fail("duplicate_id", "region id already used");
        if (auto e = checkRegion(p, t->kind, region_)) return fail(*e);
        if (index_ < -1 || index_ > static_cast<int>(t->regions.size())) return fail("bad_index", "region index out of range");
        const int idx = index_ < 0 ? static_cast<int>(t->regions.size()) : index_;
        t->regions.insert(t->regions.begin() + idx, region_);
        return success(makeRemoveRegion(region_.id));
    }

private:
    Uuid trackId_;
    Region region_;
    int index_;
};

class RemoveRegionCmd final : public Command {
public:
    explicit RemoveRegionCmd(Uuid id) : id_(id) {}
    std::string type() const override { return "remove_region"; }
    json toJson() const override { return {{"type", type()}, {"regionId", id_}}; }

    ApplyResult apply(Project& p) const override {
        std::size_t idx = 0;
        Track* t = p.findTrackOfRegion(id_, &idx);
        if (!t) return fail("not_found", "no such region");
        Region removed = std::move(t->regions[idx]);
        t->regions.erase(t->regions.begin() + static_cast<std::ptrdiff_t>(idx));
        return success(makeAddRegion(t->id, std::move(removed), static_cast<int>(idx)));
    }

private:
    Uuid id_;
};

class MoveRegionCmd final : public Command {
public:
    MoveRegionCmd(Uuid id, std::int64_t start) : id_(id), start_(start) {}
    std::string type() const override { return "move_region"; }
    json toJson() const override { return {{"type", type()}, {"regionId", id_}, {"start", start_}}; }

    ApplyResult apply(Project& p) const override {
        std::size_t idx = 0;
        Track* t = p.findTrackOfRegion(id_, &idx);
        if (!t) return fail("not_found", "no such region");
        if (start_ < 0 || start_ > kMaxPosition) return fail("bad_region", "region start must be in [0, 2^40]");
        const std::int64_t old = t->regions[idx].start;
        t->regions[idx].start = start_;
        return success(makeMoveRegion(id_, old));
    }

private:
    Uuid id_;
    std::int64_t start_;
};

// Frames between two positions of a region's own time base (ticks or microseconds).
std::int64_t framesBetween(const Project& p, TimeBase base, std::int64_t from, std::int64_t to) {
    if (base == TimeBase::Musical) {
        return static_cast<std::int64_t>(std::llround(p.tempoMap.ticksToSamples(to, p.sampleRate))) -
               static_cast<std::int64_t>(std::llround(p.tempoMap.ticksToSamples(from, p.sampleRate)));
    }
    return static_cast<std::int64_t>(std::llround(static_cast<double>(to - from) * p.sampleRate / 1e6));
}

class ReplaceRegionCmd final : public Command {
public:
    explicit ReplaceRegionCmd(Region region) : region_(std::move(region)) {}
    std::string type() const override { return "replace_region"; }
    json toJson() const override { return {{"type", type()}, {"region", region_}}; }

    ApplyResult apply(Project& p) const override {
        std::size_t idx = 0;
        Track* t = p.findTrackOfRegion(region_.id, &idx);
        if (!t) return fail("not_found", "no such region");
        if (auto e = checkRegion(p, t->kind, region_)) return fail(*e);
        Region previous = std::move(t->regions[idx]);
        t->regions[idx] = region_;
        return success(makeReplaceRegion(std::move(previous)));
    }

private:
    Region region_;
};

class ResizeRegionCmd final : public Command {
public:
    ResizeRegionCmd(Uuid id, std::int64_t start, std::int64_t length) : id_(id), start_(start), length_(length) {}
    std::string type() const override { return "resize_region"; }
    json toJson() const override { return {{"type", type()}, {"regionId", id_}, {"start", start_}, {"length", length_}}; }

    ApplyResult apply(Project& p) const override {
        std::size_t idx = 0;
        Track* t = p.findTrackOfRegion(id_, &idx);
        if (!t) return fail("not_found", "no such region");
        if (start_ < 0 || start_ > kMaxPosition || length_ <= 0 || length_ > kMaxPosition)
            return fail("bad_region", "region start and length must be in (0, 2^40]");
        const Region old = t->regions[idx];
        Region r = old;
        r.start = start_;
        r.length = length_;
        const std::int64_t moved = start_ - old.start;
        if (t->kind == TrackKind::Audio) {
            r.sourceOffsetFrames = old.sourceOffsetFrames + framesBetween(p, old.timeBase, old.start, start_);
            if (r.sourceOffsetFrames < 0) return fail("bad_region", "the start cannot move before the beginning of the media");
        } else {
            r.notes.clear();
            for (MidiNote n : old.notes) {
                n.start -= moved;
                if (n.start + n.length <= 0 || n.start >= length_) continue;  // entirely outside the new region
                if (n.start < 0) {
                    n.length += n.start;
                    n.start = 0;
                }
                if (n.start + n.length > length_) n.length = length_ - n.start;  // crossing the new right edge
                r.notes.push_back(n);
            }
        }
        if (auto e = checkRegion(p, t->kind, r)) return fail(*e);
        t->regions[idx] = std::move(r);
        return success(makeReplaceRegion(old));
    }

private:
    Uuid id_;
    std::int64_t start_, length_;
};

class SplitRegionCmd final : public Command {
public:
    SplitRegionCmd(Uuid id, std::int64_t at, Uuid newId) : id_(id), at_(at), newId_(newId) {}
    std::string type() const override { return "split_region"; }
    json toJson() const override { return {{"type", type()}, {"regionId", id_}, {"at", at_}, {"newRegionId", newId_}}; }

    ApplyResult apply(Project& p) const override {
        std::size_t idx = 0;
        Track* t = p.findTrackOfRegion(id_, &idx);
        if (!t) return fail("not_found", "no such region");
        if (newId_.isNull() || allRegionIds(p).count(newId_)) return fail("duplicate_id", "the id of the new region is missing or already used");
        const Region old = t->regions[idx];
        if (at_ <= old.start || at_ >= old.start + old.length) return fail("bad_region", "the split position must be inside the region");
        Region left = old, right = old;
        left.length = at_ - old.start;
        left.fadeOut = 0;   // the cut is not an end: the outer ends keep their fades
        right.fadeIn = 0;
        right.id = newId_;
        right.start = at_;
        right.length = old.start + old.length - at_;
        if (t->kind == TrackKind::Audio) {
            right.sourceOffsetFrames = old.sourceOffsetFrames + framesBetween(p, old.timeBase, old.start, at_);
        } else {
            const std::int64_t cut = at_ - old.start;
            left.notes.clear();
            right.notes.clear();
            for (MidiNote n : old.notes) {
                if (n.start < cut) {
                    n.length = std::min(n.length, cut - n.start);
                    left.notes.push_back(n);
                } else {
                    n.start -= cut;
                    right.notes.push_back(n);
                }
            }
        }
        if (auto e = checkRegion(p, t->kind, left)) return fail(*e);
        if (auto e = checkRegion(p, t->kind, right)) return fail(*e);
        t->regions[idx] = std::move(left);
        t->regions.insert(t->regions.begin() + static_cast<std::ptrdiff_t>(idx) + 1, std::move(right));
        std::vector<CommandPtr> undo;
        undo.push_back(makeRemoveRegion(newId_));
        undo.push_back(makeReplaceRegion(old));
        return success(makeTransaction(std::move(undo)));
    }

private:
    Uuid id_;
    std::int64_t at_;
    Uuid newId_;
};

class JoinRegionsCmd final : public Command {
public:
    explicit JoinRegionsCmd(std::vector<Uuid> ids) : ids_(std::move(ids)) {}
    std::string type() const override { return "join_regions"; }
    json toJson() const override { return {{"type", type()}, {"regionIds", ids_}}; }

    ApplyResult apply(Project& p) const override {
        if (ids_.size() < 2) return fail("bad_value", "join needs at least two regions");
        Track* track = nullptr;
        std::vector<std::size_t> order;  // indices into track->regions
        for (const Uuid& id : ids_) {
            std::size_t idx = 0;
            Track* t = p.findTrackOfRegion(id, &idx);
            if (!t) return fail("not_found", "no such region");
            if (track && t != track) return fail("bad_region", "regions must be on the same track");
            track = t;
            if (std::find(order.begin(), order.end(), idx) != order.end()) return fail("duplicate_id", "a region is listed twice");
            order.push_back(idx);
        }
        auto& regions = track->regions;
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return regions[a].start < regions[b].start; });
        for (std::size_t i = 1; i < order.size(); ++i) {
            const Region& a = regions[order[i - 1]];
            const Region& b = regions[order[i]];
            if (a.timeBase != b.timeBase || a.start + a.length != b.start) return fail("bad_region", "regions must be adjacent");
            if (a.gainDb != b.gainDb) return fail("bad_region", "regions must have the same gain");
            if (track->kind == TrackKind::Audio) {
                if (a.mediaId != b.mediaId) return fail("bad_region", "regions must use the same media");
                if (b.sourceOffsetFrames != a.sourceOffsetFrames + framesBetween(p, a.timeBase, a.start, b.start))
                    return fail("bad_region", "the audio of the regions is not continuous");
            }
        }
        const Region first = regions[order.front()];
        const Region& last = regions[order.back()];
        Region joined = first;
        joined.length = last.start + last.length - first.start;
        joined.fadeOut = last.fadeOut;
        if (track->kind != TrackKind::Audio) {
            for (std::size_t i = 1; i < order.size(); ++i) {
                const Region& r = regions[order[i]];
                for (MidiNote n : r.notes) {
                    n.start += r.start - first.start;
                    joined.notes.push_back(n);
                }
            }
        }
        if (auto e = checkRegion(p, track->kind, joined)) return fail(*e);

        std::vector<std::pair<std::size_t, Region>> removed;  // original index, region
        for (std::size_t i = 1; i < order.size(); ++i) removed.emplace_back(order[i], regions[order[i]]);
        std::sort(removed.begin(), removed.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [idx, r] : removed) regions.erase(regions.begin() + static_cast<std::ptrdiff_t>(idx));
        std::size_t firstIdx = 0;
        p.findTrackOfRegion(first.id, &firstIdx);
        regions[firstIdx] = joined;

        std::vector<CommandPtr> undo;
        undo.push_back(makeReplaceRegion(first));
        std::reverse(removed.begin(), removed.end());  // ascending original index restores the original order
        for (const auto& [idx, r] : removed) undo.push_back(makeAddRegion(track->id, r, static_cast<int>(idx)));
        return success(makeTransaction(std::move(undo)));
    }

private:
    std::vector<Uuid> ids_;
};

// ---------------------------------------------------------------- sends

class AddSendCmd final : public Command {
public:
    AddSendCmd(Uuid trackId, Send send, int index) : trackId_(trackId), send_(send), index_(index) {}
    std::string type() const override { return "add_send"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"send", send_}, {"index", index_}}; }

    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        if (t->kind == TrackKind::Master) return fail("invalid_kind", "the master track cannot have sends");
        if (allSendIds(p).count(send_.id)) return fail("duplicate_id", "send id already used");
        if (auto e = checkSendFields(p, trackId_, send_)) return fail(*e);
        if (reaches(p, send_.targetTrackId, trackId_)) return fail("cycle", "this send would create a routing loop");
        if (index_ < -1 || index_ > static_cast<int>(t->strip.sends.size())) return fail("bad_index", "send index out of range");
        const int idx = index_ < 0 ? static_cast<int>(t->strip.sends.size()) : index_;
        t->strip.sends.insert(t->strip.sends.begin() + idx, send_);
        return success(makeRemoveSend(send_.id));
    }

private:
    Uuid trackId_;
    Send send_;
    int index_;
};

class RemoveSendCmd final : public Command {
public:
    explicit RemoveSendCmd(Uuid id) : id_(id) {}
    std::string type() const override { return "remove_send"; }
    json toJson() const override { return {{"type", type()}, {"sendId", id_}}; }

    ApplyResult apply(Project& p) const override {
        for (Track& t : p.tracks) {
            auto it = std::find_if(t.strip.sends.begin(), t.strip.sends.end(), [&](const Send& s) { return s.id == id_; });
            if (it == t.strip.sends.end()) continue;
            const Send removed = *it;
            const int idx = static_cast<int>(it - t.strip.sends.begin());
            t.strip.sends.erase(it);
            return success(makeAddSend(t.id, removed, idx));
        }
        return fail("not_found", "no such send");
    }

private:
    Uuid id_;
};

// ---------------------------------------------------------------- inserts

class SetInsertsCmd final : public Command {
public:
    SetInsertsCmd(Uuid trackId, std::vector<ProcessorRef> inserts) : trackId_(trackId), inserts_(std::move(inserts)) {}
    std::string type() const override { return "set_inserts"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"inserts", inserts_}}; }

    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        for (const ProcessorRef& r : inserts_)
            if (auto e = checkInsert(r)) return fail(*e);
        std::vector<ProcessorRef> previous = std::move(t->strip.inserts);
        t->strip.inserts = inserts_;
        return success(makeSetInserts(trackId_, std::move(previous)));
    }

private:
    Uuid trackId_;
    std::vector<ProcessorRef> inserts_;
};

// ---------------------------------------------------------------- transaction

class TransactionCmd final : public Command {
public:
    explicit TransactionCmd(std::vector<CommandPtr> cmds) : cmds_(std::move(cmds)) {}
    std::string type() const override { return "transaction"; }
    json toJson() const override {
        json arr = json::array();
        for (const auto& c : cmds_) arr.push_back(c->toJson());
        return {{"type", type()}, {"commands", arr}};
    }

    ApplyResult apply(Project& p) const override {
        // A command refused as in_use may need one that comes later (a bus is removed before the tracks sending to it):
        // refused commands wait and are retried until none can go, so the order of the commands does not matter.
        std::vector<CommandPtr> inverses;  // in the order they were applied
        const auto rollback = [&] {
            for (auto it = inverses.rbegin(); it != inverses.rend(); ++it) (*it)->apply(p);  // inverses cannot fail
        };
        std::vector<std::size_t> waiting(cmds_.size());
        for (std::size_t i = 0; i < waiting.size(); ++i) waiting[i] = i;
        std::size_t refusedIndex = 0;
        std::string refusedCode, refusedMessage;
        while (!waiting.empty()) {
            std::vector<std::size_t> refused;
            for (const std::size_t i : waiting) {
                ApplyResult r = cmds_[i]->apply(p);
                if (r.ok()) {
                    inverses.push_back(std::move(r.inverse));
                } else if (r.error->code == "in_use") {
                    if (refused.empty()) {  // the message for the case that no command in this pass can go
                        refusedIndex = i;
                        refusedCode = r.error->code;
                        refusedMessage = r.error->message;
                    }
                    refused.push_back(i);
                } else {
                    rollback();
                    return fail(r.error->code, "command " + std::to_string(i) + ": " + r.error->message);
                }
            }
            if (refused.size() == waiting.size()) {  // no progress: what is left cannot be applied
                rollback();
                return fail(refusedCode, "command " + std::to_string(refusedIndex) + ": " + refusedMessage);
            }
            waiting = std::move(refused);
        }
        std::reverse(inverses.begin(), inverses.end());
        return success(makeTransaction(std::move(inverses)));
    }

private:
    std::vector<CommandPtr> cmds_;
};

}  // namespace

CommandPtr makeAddTrack(Track track, int index) { return std::make_unique<AddTrackCmd>(std::move(track), index); }
CommandPtr makeRemoveTrack(Uuid trackId) { return std::make_unique<RemoveTrackCmd>(trackId); }
CommandPtr makeSetStrip(Uuid trackId, StripPatch patch) { return std::make_unique<SetStripCmd>(trackId, patch); }
CommandPtr makeSetTempo(Ticks tick, double bpm) { return std::make_unique<SetTempoCmd>(tick, bpm); }
CommandPtr makeSetAutomation(Uuid trackId, std::string target, std::vector<AutomationPoint> points) { return std::make_unique<SetAutomationCmd>(trackId, std::move(target), std::move(points)); }
CommandPtr makeSetGroups(std::vector<Group> groups) { return std::make_unique<SetGroupsCmd>(std::move(groups)); }
CommandPtr makeSetTrackOrder(std::vector<Uuid> order) { return std::make_unique<SetTrackOrderCmd>(std::move(order)); }
CommandPtr makeSetProjectName(std::string name) { return std::make_unique<SetProjectNameCmd>(std::move(name)); }
CommandPtr makeSetMarkers(std::vector<Marker> markers) { return std::make_unique<SetMarkersCmd>(std::move(markers)); }
CommandPtr makeRemoveTempo(Ticks tick) { return std::make_unique<RemoveTempoCmd>(tick); }
CommandPtr makeSetTrackProps(Uuid trackId, TrackPatch patch) { return std::make_unique<SetTrackPropsCmd>(trackId, std::move(patch)); }
CommandPtr makeSetSignature(Ticks tick, int numerator, int denominator) { return std::make_unique<SetSignatureCmd>(tick, numerator, denominator); }
CommandPtr makeRemoveSignature(Ticks tick) { return std::make_unique<RemoveSignatureCmd>(tick); }
CommandPtr makeAddMedia(MediaItem item, int index) { return std::make_unique<AddMediaCmd>(std::move(item), index); }
CommandPtr makeRemoveMedia(Uuid mediaId) { return std::make_unique<RemoveMediaCmd>(mediaId); }
CommandPtr makeAddRegion(Uuid trackId, Region region, int index) { return std::make_unique<AddRegionCmd>(trackId, std::move(region), index); }
CommandPtr makeRemoveRegion(Uuid regionId) { return std::make_unique<RemoveRegionCmd>(regionId); }
CommandPtr makeMoveRegion(Uuid regionId, std::int64_t newStart) { return std::make_unique<MoveRegionCmd>(regionId, newStart); }
CommandPtr makeReplaceRegion(Region region) { return std::make_unique<ReplaceRegionCmd>(std::move(region)); }
CommandPtr makeResizeRegion(Uuid regionId, std::int64_t start, std::int64_t length) { return std::make_unique<ResizeRegionCmd>(regionId, start, length); }
CommandPtr makeSplitRegion(Uuid regionId, std::int64_t at, Uuid newRegionId) { return std::make_unique<SplitRegionCmd>(regionId, at, newRegionId); }
CommandPtr makeJoinRegions(std::vector<Uuid> regionIds) { return std::make_unique<JoinRegionsCmd>(std::move(regionIds)); }
CommandPtr makeAddSend(Uuid trackId, Send send, int index) { return std::make_unique<AddSendCmd>(trackId, send, index); }
CommandPtr makeRemoveSend(Uuid sendId) { return std::make_unique<RemoveSendCmd>(sendId); }
CommandPtr makeSetInserts(Uuid trackId, std::vector<ProcessorRef> inserts) { return std::make_unique<SetInsertsCmd>(trackId, std::move(inserts)); }
CommandPtr makeTransaction(std::vector<CommandPtr> commands) { return std::make_unique<TransactionCmd>(std::move(commands)); }

CommandPtr commandFromJson(const nlohmann::json& j) {
    try {
        if (!j.is_object()) throw std::runtime_error("command must be a JSON object");
        const std::string type = j.at("type").get<std::string>();
        if (type == "add_track") return makeAddTrack(j.at("track").get<Track>(), j.value("index", -1));
        if (type == "remove_track") return makeRemoveTrack(j.at("trackId").get<Uuid>());
        if (type == "set_strip") {
            StripPatch patch;
            if (j.contains("gainDb")) patch.gainDb = j["gainDb"].get<float>();
            if (j.contains("pan")) patch.pan = j["pan"].get<float>();
            if (j.contains("mute")) patch.mute = j["mute"].get<bool>();
            if (j.contains("solo")) patch.solo = j["solo"].get<bool>();
            if (j.contains("input")) patch.input = j["input"].get<int>();
            return makeSetStrip(j.at("trackId").get<Uuid>(), patch);
        }
        if (type == "set_tempo") return makeSetTempo(j.at("tick").get<Ticks>(), j.at("bpm").get<double>());
        if (type == "set_automation") return makeSetAutomation(j.at("trackId").get<Uuid>(), j.at("target").get<std::string>(), j.at("points").get<std::vector<AutomationPoint>>());
        if (type == "set_region_fades") return makeSetRegionFades(j.at("regionId").get<Uuid>(), j.at("fadeIn").get<std::int64_t>(), j.at("fadeOut").get<std::int64_t>());
        if (type == "set_groups") return makeSetGroups(j.at("groups").get<std::vector<Group>>());
        if (type == "set_track_order") return makeSetTrackOrder(j.at("order").get<std::vector<Uuid>>());
        if (type == "set_project_name") return makeSetProjectName(j.at("name").get<std::string>());
        if (type == "set_markers") return makeSetMarkers(j.at("markers").get<std::vector<Marker>>());
        if (type == "remove_tempo") return makeRemoveTempo(j.at("tick").get<Ticks>());
        if (type == "set_track_props") {
            TrackPatch patch;
            if (j.contains("name")) patch.name = j["name"].get<std::string>();
            if (j.contains("color")) patch.color = j["color"].get<std::string>();
            if (j.contains("showInTracks")) patch.showInTracks = j["showInTracks"].get<bool>();
            if (j.contains("automationMode")) patch.automationMode = j["automationMode"].get<std::string>();
            return makeSetTrackProps(j.at("trackId").get<Uuid>(), patch);
        }
        if (type == "set_signature") return makeSetSignature(j.at("tick").get<Ticks>(), j.at("numerator").get<int>(), j.at("denominator").get<int>());
        if (type == "remove_signature") return makeRemoveSignature(j.at("tick").get<Ticks>());
        if (type == "add_media") return makeAddMedia(j.at("item").get<MediaItem>(), j.value("index", -1));
        if (type == "remove_media") return makeRemoveMedia(j.at("mediaId").get<Uuid>());
        if (type == "add_region") return makeAddRegion(j.at("trackId").get<Uuid>(), j.at("region").get<Region>(), j.value("index", -1));
        if (type == "remove_region") return makeRemoveRegion(j.at("regionId").get<Uuid>());
        if (type == "move_region") return makeMoveRegion(j.at("regionId").get<Uuid>(), j.at("start").get<std::int64_t>());
        if (type == "replace_region") return makeReplaceRegion(j.at("region").get<Region>());
        if (type == "resize_region") return makeResizeRegion(j.at("regionId").get<Uuid>(), j.at("start").get<std::int64_t>(), j.at("length").get<std::int64_t>());
        if (type == "split_region") return makeSplitRegion(j.at("regionId").get<Uuid>(), j.at("at").get<std::int64_t>(), j.at("newRegionId").get<Uuid>());
        if (type == "join_regions") return makeJoinRegions(j.at("regionIds").get<std::vector<Uuid>>());
        if (type == "add_send") return makeAddSend(j.at("trackId").get<Uuid>(), j.at("send").get<Send>(), j.value("index", -1));
        if (type == "remove_send") return makeRemoveSend(j.at("sendId").get<Uuid>());
        if (type == "set_inserts") return makeSetInserts(j.at("trackId").get<Uuid>(), j.at("inserts").get<std::vector<ProcessorRef>>());
        if (type == "set_patch_id") return makeSetPatchId(j.at("trackId").get<Uuid>(), j.at("patchId").get<std::string>());
        if (type == "set_instrument_state") return makeSetInstrumentState(j.at("trackId").get<Uuid>(), j.at("state").get<std::string>());
        if (type == "set_instrument") return makeSetInstrument(j.at("trackId").get<Uuid>(), j.at("instrument").get<ProcessorRef>());
        if (type == "set_output") return makeSetOutput(j.at("trackId").get<Uuid>(), j.at("output").get<Uuid>());
        if (type == "set_send") {
            SendPatch patch;
            if (j.contains("levelDb")) patch.levelDb = j["levelDb"].get<float>();
            if (j.contains("preFader")) patch.preFader = j["preFader"].get<bool>();
            return makeSetSend(j.at("sendId").get<Uuid>(), patch);
        }
        if (type == "set_region_gain") return makeSetRegionGain(j.at("regionId").get<Uuid>(), j.at("gainDb").get<float>());
        if (type == "add_insert") return makeAddInsert(j.at("trackId").get<Uuid>(), j.at("insert").get<ProcessorRef>(), j.value("index", -1));
        if (type == "remove_insert") return makeRemoveInsert(j.at("trackId").get<Uuid>(), j.at("index").get<int>());
        if (type == "set_insert_param") {
            const auto& v = j.at("value");
            return makeSetInsertParam(j.at("trackId").get<Uuid>(), j.at("index").get<int>(), j.at("param").get<std::string>(),
                                      v.is_null() ? std::nullopt : std::optional<double>(v.get<double>()));
        }
        if (type == "set_insert_state")
            return makeSetInsertState(j.at("trackId").get<Uuid>(), j.at("index").get<int>(), j.at("state").get<std::string>());
        if (type == "move_insert")
            return makeMoveInsert(j.at("trackId").get<Uuid>(), j.at("from").get<int>(), j.at("to").get<int>(),
                                  j.contains("toTrackId") ? j["toTrackId"].get<Uuid>() : Uuid{});
        if (type == "set_insert_bypass")
            return makeSetInsertBypass(j.at("trackId").get<Uuid>(), j.at("index").get<int>(), j.at("bypass").get<bool>());
        if (type == "transaction") {
            std::vector<CommandPtr> inner;
            for (const auto& c : j.at("commands")) inner.push_back(commandFromJson(c));
            return makeTransaction(std::move(inner));
        }
        throw std::runtime_error("unknown command type: " + type);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("invalid command: ") + e.what());
    }
}

}  // namespace lpc
