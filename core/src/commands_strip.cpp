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
        if (!isKnownInstrument(instrument_.processorId)) return fail("bad_value", "unknown instrument: " + instrument_.processorId);
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

}  // namespace

CommandPtr makeSetPatchId(Uuid trackId, std::string patchId) { return std::make_unique<SetPatchIdCmd>(trackId, std::move(patchId)); }
CommandPtr makeSetInstrument(Uuid trackId, ProcessorRef instrument) { return std::make_unique<SetInstrumentCmd>(trackId, std::move(instrument)); }
CommandPtr makeSetOutput(Uuid trackId, Uuid output) { return std::make_unique<SetOutputCmd>(trackId, output); }

}  // namespace lpc
