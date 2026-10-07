#include "lpc/commands.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

#include "lpc/model_json.h"
#include "lpc/processor_ids.h"

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

using MaybeError = std::optional<CommandError>;

bool isBusLike(TrackKind k) { return k == TrackKind::Bus || k == TrackKind::Aux; }
bool isSourceKind(TrackKind k) { return k == TrackKind::Audio || k == TrackKind::Midi || k == TrackKind::Instrument; }
bool inRange(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }

MaybeError checkStripValues(float gainDb, float pan) {
    if (!inRange(gainDb, -96.0f, 24.0f)) return CommandError{"bad_value", "gainDb must be a number in [-96, 24]"};
    if (!inRange(pan, -1.0f, 1.0f)) return CommandError{"bad_value", "pan must be a number in [-1, 1]"};
    return std::nullopt;
}

// A region is valid for a given track kind and project (media must exist, notes must be sane).
MaybeError checkRegion(const Project& p, TrackKind kind, const Region& r) {
    if (r.id.isNull()) return CommandError{"duplicate_id", "region id is missing"};
    if (r.start < 0 || r.length <= 0) return CommandError{"bad_region", "region needs start >= 0 and length > 0"};
    if (!inRange(r.gainDb, -96.0f, 24.0f)) return CommandError{"bad_value", "region gainDb must be in [-96, 24]"};
    if (kind == TrackKind::Audio) {
        if (r.mediaId.isNull() || !p.findMedia(r.mediaId))
            return CommandError{"bad_region", "audio region needs a mediaId present in the media pool"};
        if (!r.notes.empty()) return CommandError{"bad_region", "audio regions cannot contain notes"};
        if (r.sourceOffsetFrames < 0) return CommandError{"bad_region", "sourceOffsetFrames must be >= 0"};
    } else if (kind == TrackKind::Midi || kind == TrackKind::Instrument) {
        if (!r.mediaId.isNull()) return CommandError{"bad_region", "MIDI regions cannot reference media"};
        if (r.timeBase != TimeBase::Musical) return CommandError{"bad_region", "MIDI regions must be musical"};
        for (const MidiNote& n : r.notes) {
            if (n.start < 0 || n.length <= 0 || n.note > 127 || n.velocity < 1 || n.velocity > 127)
                return CommandError{"bad_region", "invalid MIDI note"};
        }
    } else {
        return CommandError{"invalid_kind", "this track kind cannot hold regions"};
    }
    return std::nullopt;
}

MaybeError checkSendFields(const Project& p, const Uuid& owner, const Send& s) {
    if (s.id.isNull()) return CommandError{"duplicate_id", "send id is missing"};
    if (!inRange(s.levelDb, -96.0f, 12.0f)) return CommandError{"bad_value", "send levelDb must be in [-96, 12]"};
    const Track* target = p.findTrack(s.targetTrackId);
    if (!target || !isBusLike(target->kind) || target->id == owner)
        return CommandError{"bad_target", "send target must be another existing bus or aux track"};
    return std::nullopt;
}

// True when `goal` can be reached from `from` by following outputs and sends.
bool reaches(const Project& p, const Uuid& from, const Uuid& goal) {
    std::vector<Uuid> stack{from};
    std::unordered_set<Uuid> seen;
    while (!stack.empty()) {
        const Uuid cur = stack.back();
        stack.pop_back();
        if (cur == goal) return true;
        if (!seen.insert(cur).second) continue;
        const Track* t = p.findTrack(cur);
        if (!t) continue;
        if (!t->strip.output.isNull()) stack.push_back(t->strip.output);
        for (const Send& s : t->strip.sends) stack.push_back(s.targetTrackId);
    }
    return false;
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

bool validRelativeMediaPath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos ||
        path.find('\\') != std::string::npos)
        return false;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
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
        if (track_.id.isNull() || p.findTrack(track_.id)) return CommandError{"duplicate_id", "track id missing or already used"};
        if (index_ < -1 || index_ > static_cast<int>(p.tracks.size())) return CommandError{"bad_index", "track index out of range"};
        if (auto e = checkStripValues(track_.strip.gainDb, track_.strip.pan)) return e;
        if (!track_.strip.output.isNull()) {
            const Track* out = p.findTrack(track_.strip.output);
            if (!out || !isBusLike(out->kind))
                return CommandError{"bad_output", "output must be an existing bus or aux track (or null for master)"};
        }
        const bool wantsInstrument = track_.kind == TrackKind::Instrument;
        if (wantsInstrument != track_.instrument.has_value() ||
            (wantsInstrument && !isKnownInstrument(track_.instrument->processorId)))
            return CommandError{"invalid_kind", "exactly instrument tracks need a known instrument"};
        for (const ProcessorRef& ins : track_.strip.inserts)
            if (!isKnownEffect(ins.processorId)) return CommandError{"bad_value", "unknown insert processor: " + ins.processorId};
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
        return success(makeAddTrack(std::move(removed), index));
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

// ---------------------------------------------------------------- media

class AddMediaCmd final : public Command {
public:
    explicit AddMediaCmd(MediaItem item) : item_(std::move(item)) {}
    std::string type() const override { return "add_media"; }
    json toJson() const override { return {{"type", type()}, {"item", item_}}; }

    ApplyResult apply(Project& p) const override {
        if (item_.id.isNull() || p.findMedia(item_.id)) return fail("duplicate_id", "media id missing or already used");
        if (!validRelativeMediaPath(item_.path)) return fail("bad_media", "media path must be relative, inside the project, with '/' separators");
        if (item_.sampleRate != p.sampleRate) return fail("bad_media", "media sample rate must equal the project sample rate (no resampling)");
        if (item_.channels < 1 || item_.channels > 2) return fail("bad_media", "media must be mono or stereo");
        if (item_.frames < 0) return fail("bad_media", "media frame count must be >= 0");
        p.mediaPool.push_back(item_);
        return success(makeRemoveMedia(item_.id));
    }

private:
    MediaItem item_;
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
        p.mediaPool.erase(it);
        return success(makeAddMedia(std::move(removed)));
    }

private:
    Uuid id_;
};

}  // namespace

CommandPtr makeAddTrack(Track track, int index) { return std::make_unique<AddTrackCmd>(std::move(track), index); }
CommandPtr makeRemoveTrack(Uuid trackId) { return std::make_unique<RemoveTrackCmd>(trackId); }
CommandPtr makeSetStrip(Uuid trackId, StripPatch patch) { return std::make_unique<SetStripCmd>(trackId, patch); }
CommandPtr makeSetTempo(Ticks tick, double bpm) { return std::make_unique<SetTempoCmd>(tick, bpm); }
CommandPtr makeRemoveTempo(Ticks tick) { return std::make_unique<RemoveTempoCmd>(tick); }
CommandPtr makeAddMedia(MediaItem item) { return std::make_unique<AddMediaCmd>(std::move(item)); }
CommandPtr makeRemoveMedia(Uuid mediaId) { return std::make_unique<RemoveMediaCmd>(mediaId); }

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
            return makeSetStrip(j.at("trackId").get<Uuid>(), patch);
        }
        if (type == "set_tempo") return makeSetTempo(j.at("tick").get<Ticks>(), j.at("bpm").get<double>());
        if (type == "remove_tempo") return makeRemoveTempo(j.at("tick").get<Ticks>());
        if (type == "add_media") return makeAddMedia(j.at("item").get<MediaItem>());
        if (type == "remove_media") return makeRemoveMedia(j.at("mediaId").get<Uuid>());
        // Task 6 adds its command types above this line.
        throw std::runtime_error("unknown command type: " + type);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("invalid command: ") + e.what());
    }
}

}  // namespace lpc
