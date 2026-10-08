#pragma once
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lpc/command.h"
#include "lpc/model.h"

namespace lpc {

// What a Smart Control moves: one parameter of the track, the control range mapped linearly onto from..to.
struct SmartTarget {
    enum class Kind { StripGain, StripPan, InsertParam };
    Kind kind = Kind::StripGain;
    std::size_t index = 0;    // insert index (InsertParam)
    std::string param;        // insert parameter name (InsertParam)
    std::string path;         // as written in the file
    double from = 0, to = 1;  // either direction
};

struct SmartControl {
    std::string id, label, group;
    double min = 0, max = 1, def = 0;
    std::vector<SmartTarget> targets;
};

struct Patch {
    std::string id, category, name;
    TrackKind kind = TrackKind::Audio;  // Audio, Instrument, Aux or Bus
    std::optional<ProcessorRef> instrument;
    float gainDb = 0.0f, pan = 0.0f;
    std::vector<ProcessorRef> inserts;
    std::vector<SmartControl> smartControls;
};

struct PatchProblem {
    std::string where;  // the entry id, "#<index>" or ""
    std::string message;
};

// The built-in patch catalogue (core/data/patches.json) and the transactions that apply a patch or move a knob.
class PatchLibrary {
public:
    static PatchLibrary fromJson(const nlohmann::json& doc);          // never throws; bad entries are skipped and reported
    static PatchLibrary fromFile(const std::filesystem::path& file);  // a missing or unreadable file gives a problem

    const std::vector<Patch>& patches() const;
    const std::vector<PatchProblem>& problems() const;
    const Patch* find(const std::string& id) const;
    std::vector<std::string> categories(TrackKind kind) const;                                // sorted
    std::vector<const Patch*> inCategory(TrackKind kind, const std::string& category) const;  // file order

    // One undo step; nullptr when the patch is unknown or is for another kind of track.
    CommandPtr applyCommand(const Uuid& trackId, TrackKind trackKind, const std::string& patchId) const;
    // One undo step moving every target of the control; the value is clamped to the control's range. nullptr: NaN.
    static CommandPtr smartControlCommand(const Uuid& trackId, const SmartControl& control, double value);
    // The control's position read back from the track through its first target; nullopt when the insert it drives is gone.
    static std::optional<double> smartControlValue(const Track& track, const SmartControl& control);

private:
    std::vector<Patch> patches_;
    std::vector<PatchProblem> problems_;
};

}  // namespace lpc
