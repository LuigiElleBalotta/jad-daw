#include <algorithm>
#include <cmath>
#include <optional>

#include "command_result.h"
#include "lpc/commands.h"
#include "lpc/model_json.h"
#include "lpc/processor_ids.h"
#include "lpc/validation.h"

namespace lpc {

namespace {

using nlohmann::json;
using detail::fail;
using detail::success;

class SetPatchIdCmd final : public Command {
public:
    SetPatchIdCmd(Uuid id, std::string patchId) : id_(id), patchId_(std::move(patchId)) {}
    std::string type() const override { return "set_patch_id"; }
    json toJson() const override { return {{"type", type()}, {"trackId", id_}, {"patchId", patchId_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(id_);
        if (!t) return fail("not_found", "no such track");
        if (t->kind == TrackKind::Master) return fail("invalid_kind", "the master track has no patch");
        if (auto e = checkPatchId(patchId_)) return fail(*e);
        std::string previous = std::move(t->patchId);
        t->patchId = patchId_;
        return success(makeSetPatchId(id_, std::move(previous)));
    }

private:
    Uuid id_;
    std::string patchId_;
};

class SetInstrumentCmd final : public Command {
public:
    SetInstrumentCmd(Uuid id, ProcessorRef instrument) : id_(id), instrument_(std::move(instrument)) {}
    std::string type() const override { return "set_instrument"; }
    json toJson() const override { return {{"type", type()}, {"trackId", id_}, {"instrument", instrument_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(id_);
        if (!t) return fail("not_found", "no such track");
        if (t->kind != TrackKind::Instrument || !t->instrument) return fail("invalid_kind", "only instrument tracks have an instrument");
        if (auto e = checkInstrument(instrument_)) return fail(*e);
        ProcessorRef previous = std::move(*t->instrument);
        t->instrument = instrument_;
        return success(makeSetInstrument(id_, std::move(previous)));
    }

private:
    Uuid id_;
    ProcessorRef instrument_;
};

class SetOutputCmd final : public Command {
public:
    SetOutputCmd(Uuid id, Uuid output) : id_(id), output_(output) {}
    std::string type() const override { return "set_output"; }
    json toJson() const override { return {{"type", type()}, {"trackId", id_}, {"output", output_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(id_);
        if (!t) return fail("not_found", "no such track");
        if (t->kind == TrackKind::Master) return fail("invalid_kind", "the master track has no output");
        if (!output_.isNull()) {
            const Track* out = p.findTrack(output_);
            if (!out || !isBusLike(out->kind) || out->id == id_)
                return fail("bad_output", "output must be another existing bus or aux track (or null for master)");
            if (reaches(p, output_, id_)) return fail("cycle", "this output would create a routing loop");
        }
        const Uuid previous = t->strip.output;
        t->strip.output = output_;
        return success(makeSetOutput(id_, previous));
    }

private:
    Uuid id_;
    Uuid output_;
};

class SetSendCmd final : public Command {
public:
    SetSendCmd(Uuid id, SendPatch patch) : id_(id), patch_(patch) {}
    std::string type() const override { return "set_send"; }
    json toJson() const override {
        json j = {{"type", type()}, {"sendId", id_}};
        if (patch_.levelDb) j["levelDb"] = *patch_.levelDb;
        if (patch_.preFader) j["preFader"] = *patch_.preFader;
        return j;
    }
    ApplyResult apply(Project& p) const override {
        for (Track& t : p.tracks) {
            for (Send& s : t.strip.sends) {
                if (s.id != id_) continue;
                Send next = s;
                if (patch_.levelDb) next.levelDb = *patch_.levelDb;
                if (patch_.preFader) next.preFader = *patch_.preFader;
                if (auto e = checkSendFields(p, t.id, next)) return fail(*e);
                SendPatch previous;
                if (patch_.levelDb) previous.levelDb = s.levelDb;
                if (patch_.preFader) previous.preFader = s.preFader;
                s = next;
                return success(makeSetSend(id_, previous));
            }
        }
        return fail("not_found", "no such send");
    }

private:
    Uuid id_;
    SendPatch patch_;
};

class SetRegionGainCmd final : public Command {
public:
    SetRegionGainCmd(Uuid id, float gainDb) : id_(id), gainDb_(gainDb) {}
    std::string type() const override { return "set_region_gain"; }
    json toJson() const override { return {{"type", type()}, {"regionId", id_}, {"gainDb", gainDb_}}; }
    ApplyResult apply(Project& p) const override {
        std::size_t index = 0;
        Track* t = p.findTrackOfRegion(id_, &index);
        if (!t) return fail("not_found", "no such region");
        if (auto e = checkStripValues(gainDb_, 0.0f)) return fail(*e);  // the same -96..24 dB range as a strip
        const float previous = t->regions[index].gainDb;
        t->regions[index].gainDb = gainDb_;
        return success(makeSetRegionGain(id_, previous));
    }

private:
    Uuid id_;
    float gainDb_;
};

class SetRegionLoopCmd final : public Command {
public:
    SetRegionLoopCmd(Uuid id, std::int64_t loopLength) : id_(id), loop_(loopLength) {}
    std::string type() const override { return "set_region_loop"; }
    json toJson() const override { return {{"type", type()}, {"regionId", id_}, {"loopLength", loop_}}; }
    ApplyResult apply(Project& p) const override {
        std::size_t index = 0;
        Track* t = p.findTrackOfRegion(id_, &index);
        if (!t) return fail("not_found", "no such region");
        Region& r = t->regions[index];
        if (loop_ < 0 || loop_ > r.length) return fail("bad_value", "a loop is between 0 and the length of the region");
        const std::int64_t previous = r.loopLength;
        r.loopLength = loop_;
        return success(makeSetRegionLoop(id_, previous));
    }

private:
    Uuid id_;
    std::int64_t loop_;
};

class SetRegionFadesCmd final : public Command {
public:
    SetRegionFadesCmd(Uuid id, std::int64_t fadeIn, std::int64_t fadeOut) : id_(id), in_(fadeIn), out_(fadeOut) {}
    std::string type() const override { return "set_region_fades"; }
    json toJson() const override { return {{"type", type()}, {"regionId", id_}, {"fadeIn", in_}, {"fadeOut", out_}}; }
    ApplyResult apply(Project& p) const override {
        std::size_t index = 0;
        Track* t = p.findTrackOfRegion(id_, &index);
        if (!t) return fail("not_found", "no such region");
        Region& r = t->regions[index];
        if (in_ < 0 || out_ < 0 || in_ > r.length || out_ > r.length) return fail("bad_value", "a fade must be between 0 and the length of the region");
        const std::int64_t pi = r.fadeIn, po = r.fadeOut;
        r.fadeIn = in_;
        r.fadeOut = out_;
        return success(makeSetRegionFades(id_, pi, po));
    }

private:
    Uuid id_;
    std::int64_t in_, out_;
};

class AddInsertCmd final : public Command {
public:
    AddInsertCmd(Uuid trackId, ProcessorRef insert, int index) : trackId_(trackId), insert_(std::move(insert)), index_(index) {}
    std::string type() const override { return "add_insert"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"insert", insert_}, {"index", index_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        if (auto e = checkInsert(insert_)) return fail(*e);
        auto& chain = t->strip.inserts;
        if (index_ < -1 || index_ > static_cast<int>(chain.size())) return fail("bad_index", "insert index out of range");
        const int at = index_ < 0 ? static_cast<int>(chain.size()) : index_;
        chain.insert(chain.begin() + at, insert_);
        return success(makeRemoveInsert(trackId_, at));
    }

private:
    Uuid trackId_;
    ProcessorRef insert_;
    int index_;
};

class RemoveInsertCmd final : public Command {
public:
    RemoveInsertCmd(Uuid trackId, int index) : trackId_(trackId), index_(index) {}
    std::string type() const override { return "remove_insert"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"index", index_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        auto& chain = t->strip.inserts;
        if (index_ < 0 || index_ >= static_cast<int>(chain.size())) return fail("bad_index", "insert index out of range");
        ProcessorRef removed = std::move(chain[static_cast<std::size_t>(index_)]);
        chain.erase(chain.begin() + index_);
        return success(makeAddInsert(trackId_, std::move(removed), index_));
    }

private:
    Uuid trackId_;
    int index_;
};

class SetInsertParamCmd final : public Command {
public:
    SetInsertParamCmd(Uuid trackId, int index, std::string param, std::optional<double> value)
        : trackId_(trackId), index_(index), param_(std::move(param)), value_(value) {}
    std::string type() const override { return "set_insert_param"; }
    json toJson() const override {
        return {{"type", type()}, {"trackId", trackId_}, {"index", index_}, {"param", param_},
                {"value", value_ ? json(*value_) : json(nullptr)}};
    }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        auto& chain = t->strip.inserts;
        if (index_ < 0 || index_ >= static_cast<int>(chain.size())) return fail("bad_index", "insert index out of range");
        ProcessorRef next = chain[static_cast<std::size_t>(index_)];
        std::optional<double> previous;
        if (const auto it = next.params.find(param_); it != next.params.end()) previous = it->second;
        if (value_) next.params[param_] = *value_;
        else next.params.erase(param_);
        if (auto e = checkInsert(next)) return fail(*e);
        chain[static_cast<std::size_t>(index_)] = std::move(next);
        return success(makeSetInsertParam(trackId_, index_, param_, previous));
    }

private:
    Uuid trackId_;
    int index_;
    std::string param_;
    std::optional<double> value_;
};

class SetInsertStateCmd final : public Command {
public:
    SetInsertStateCmd(Uuid trackId, int index, std::string state) : trackId_(trackId), index_(index), state_(std::move(state)) {}
    std::string type() const override { return "set_insert_state"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"index", index_}, {"state", state_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        auto& chain = t->strip.inserts;
        if (index_ < 0 || index_ >= static_cast<int>(chain.size())) return fail("bad_index", "insert index out of range");
        ProcessorRef next = chain[static_cast<std::size_t>(index_)];
        if (!isVst3Id(next.processorId)) return fail("bad_value", "only plug-in inserts have a state");
        std::string previous = std::move(next.state);
        next.state = state_;
        if (auto e = checkInsert(next)) return fail(*e);
        chain[static_cast<std::size_t>(index_)] = std::move(next);
        return success(makeSetInsertState(trackId_, index_, std::move(previous)));
    }

private:
    Uuid trackId_;
    int index_;
    std::string state_;
};

class SetInstrumentStateCmd final : public Command {
public:
    SetInstrumentStateCmd(Uuid trackId, std::string state) : trackId_(trackId), state_(std::move(state)) {}
    std::string type() const override { return "set_instrument_state"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"state", state_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        if (t->kind != TrackKind::Instrument || !t->instrument) return fail("invalid_kind", "only instrument tracks have an instrument");
        if (!isVst3Id(t->instrument->processorId)) return fail("bad_value", "only plug-in instruments have a state");
        ProcessorRef next = *t->instrument;
        std::string previous = std::move(next.state);
        next.state = state_;
        if (auto e = checkInstrument(next)) return fail(*e);
        t->instrument = std::move(next);
        return success(makeSetInstrumentState(trackId_, std::move(previous)));
    }

private:
    Uuid trackId_;
    std::string state_;
};

class MoveInsertCmd final : public Command {
public:
    MoveInsertCmd(Uuid trackId, int from, int to, Uuid toTrackId) : trackId_(trackId), from_(from), to_(to), toTrackId_(toTrackId) {}
    std::string type() const override { return "move_insert"; }
    json toJson() const override {
        json j = {{"type", type()}, {"trackId", trackId_}, {"from", from_}, {"to", to_}};
        if (!toTrackId_.isNull()) j["toTrackId"] = toTrackId_;
        return j;
    }
    ApplyResult apply(Project& p) const override {
        Track* src = p.findTrack(trackId_);
        if (!src) return fail("not_found", "no such track");
        auto& from = src->strip.inserts;
        if (from_ < 0 || from_ >= static_cast<int>(from.size())) return fail("bad_index", "insert index out of range");
        if (toTrackId_.isNull() || toTrackId_ == trackId_) {
            if (to_ < 0 || to_ >= static_cast<int>(from.size())) return fail("bad_index", "insert index out of range");
            if (to_ != from_) {
                ProcessorRef moved = std::move(from[static_cast<std::size_t>(from_)]);
                from.erase(from.begin() + from_);
                from.insert(from.begin() + to_, std::move(moved));
            }
            return success(makeMoveInsert(trackId_, to_, from_));
        }
        Track* dst = p.findTrack(toTrackId_);
        if (!dst) return fail("not_found", "no such target track");
        if (dst->kind == TrackKind::Master) return fail("bad_target", "the master track cannot hold inserts");
        auto& into = dst->strip.inserts;
        if (to_ < 0 || to_ > static_cast<int>(into.size())) return fail("bad_index", "insert index out of range");
        ProcessorRef moved = std::move(from[static_cast<std::size_t>(from_)]);
        from.erase(from.begin() + from_);
        into.insert(into.begin() + to_, std::move(moved));
        return success(makeMoveInsert(toTrackId_, to_, from_, trackId_));
    }

private:
    Uuid trackId_;
    int from_, to_;
    Uuid toTrackId_;
};

class SetInsertBypassCmd final : public Command {
public:
    SetInsertBypassCmd(Uuid trackId, int index, bool bypass) : trackId_(trackId), index_(index), bypass_(bypass) {}
    std::string type() const override { return "set_insert_bypass"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"index", index_}, {"bypass", bypass_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        auto& chain = t->strip.inserts;
        if (index_ < 0 || index_ >= static_cast<int>(chain.size())) return fail("bad_index", "insert index out of range");
        ProcessorRef& insert = chain[static_cast<std::size_t>(index_)];
        const bool previous = insert.bypass;
        insert.bypass = bypass_;
        return success(makeSetInsertBypass(trackId_, index_, previous));
    }

private:
    Uuid trackId_;
    int index_;
    bool bypass_;
};

}  // namespace

CommandPtr makeMoveInsert(Uuid trackId, int from, int to, Uuid toTrackId) { return std::make_unique<MoveInsertCmd>(trackId, from, to, toTrackId); }
CommandPtr makeSetInsertBypass(Uuid trackId, int index, bool bypass) { return std::make_unique<SetInsertBypassCmd>(trackId, index, bypass); }
CommandPtr makeSetInsertState(Uuid trackId, int index, std::string state) {
    return std::make_unique<SetInsertStateCmd>(trackId, index, std::move(state));
}
CommandPtr makeSetSend(Uuid sendId, SendPatch patch) { return std::make_unique<SetSendCmd>(sendId, patch); }
CommandPtr makeSetRegionFades(Uuid regionId, std::int64_t fadeIn, std::int64_t fadeOut) { return std::make_unique<SetRegionFadesCmd>(regionId, fadeIn, fadeOut); }
CommandPtr makeSetRegionGain(Uuid regionId, float gainDb) { return std::make_unique<SetRegionGainCmd>(regionId, gainDb); }
CommandPtr makeAddInsert(Uuid trackId, ProcessorRef insert, int index) { return std::make_unique<AddInsertCmd>(trackId, std::move(insert), index); }
CommandPtr makeRemoveInsert(Uuid trackId, int index) { return std::make_unique<RemoveInsertCmd>(trackId, index); }
CommandPtr makeSetInsertParam(Uuid trackId, int index, std::string param, std::optional<double> value) {
    return std::make_unique<SetInsertParamCmd>(trackId, index, std::move(param), value);
}

CommandPtr makeSetPatchId(Uuid trackId, std::string patchId) { return std::make_unique<SetPatchIdCmd>(trackId, std::move(patchId)); }
CommandPtr makeSetRegionLoop(Uuid regionId, std::int64_t loopLength) { return std::make_unique<SetRegionLoopCmd>(regionId, loopLength); }
CommandPtr makeSetInstrumentState(Uuid trackId, std::string state) { return std::make_unique<SetInstrumentStateCmd>(trackId, std::move(state)); }
CommandPtr makeSetInstrument(Uuid trackId, ProcessorRef instrument) { return std::make_unique<SetInstrumentCmd>(trackId, std::move(instrument)); }
CommandPtr makeSetOutput(Uuid trackId, Uuid output) { return std::make_unique<SetOutputCmd>(trackId, output); }

}  // namespace lpc
