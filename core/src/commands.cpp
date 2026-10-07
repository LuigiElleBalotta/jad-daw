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
        std::vector<CommandPtr> inverses;
        for (std::size_t i = 0; i < cmds_.size(); ++i) {
            ApplyResult r = cmds_[i]->apply(p);
            if (!r.ok()) {
                for (auto it = inverses.rbegin(); it != inverses.rend(); ++it) (*it)->apply(p);  // inverses cannot fail
                return fail(r.error->code, "command " + std::to_string(i) + ": " + r.error->message);
            }
            inverses.push_back(std::move(r.inverse));
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
CommandPtr makeRemoveTempo(Ticks tick) { return std::make_unique<RemoveTempoCmd>(tick); }
CommandPtr makeAddMedia(MediaItem item, int index) { return std::make_unique<AddMediaCmd>(std::move(item), index); }
CommandPtr makeRemoveMedia(Uuid mediaId) { return std::make_unique<RemoveMediaCmd>(mediaId); }
CommandPtr makeAddRegion(Uuid trackId, Region region, int index) { return std::make_unique<AddRegionCmd>(trackId, std::move(region), index); }
CommandPtr makeRemoveRegion(Uuid regionId) { return std::make_unique<RemoveRegionCmd>(regionId); }
CommandPtr makeMoveRegion(Uuid regionId, std::int64_t newStart) { return std::make_unique<MoveRegionCmd>(regionId, newStart); }
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
            return makeSetStrip(j.at("trackId").get<Uuid>(), patch);
        }
        if (type == "set_tempo") return makeSetTempo(j.at("tick").get<Ticks>(), j.at("bpm").get<double>());
        if (type == "remove_tempo") return makeRemoveTempo(j.at("tick").get<Ticks>());
        if (type == "add_media") return makeAddMedia(j.at("item").get<MediaItem>(), j.value("index", -1));
        if (type == "remove_media") return makeRemoveMedia(j.at("mediaId").get<Uuid>());
        if (type == "add_region") return makeAddRegion(j.at("trackId").get<Uuid>(), j.at("region").get<Region>(), j.value("index", -1));
        if (type == "remove_region") return makeRemoveRegion(j.at("regionId").get<Uuid>());
        if (type == "move_region") return makeMoveRegion(j.at("regionId").get<Uuid>(), j.at("start").get<std::int64_t>());
        if (type == "add_send") return makeAddSend(j.at("trackId").get<Uuid>(), j.at("send").get<Send>(), j.value("index", -1));
        if (type == "remove_send") return makeRemoveSend(j.at("sendId").get<Uuid>());
        if (type == "set_inserts") return makeSetInserts(j.at("trackId").get<Uuid>(), j.at("inserts").get<std::vector<ProcessorRef>>());
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
