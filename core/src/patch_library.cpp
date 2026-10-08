#include "lpc/patch_library.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

#include "lpc/commands.h"
#include "lpc/model_json.h"
#include "lpc/processor_ids.h"
#include "lpc/validation.h"

namespace lpc {

namespace {

using nlohmann::json;

[[noreturn]] void bad(const std::string& message) { throw std::runtime_error(message); }

std::string requireString(const json& j, const char* key, std::size_t maxLength) {
    if (!j.contains(key) || !j[key].is_string()) bad(std::string("\"") + key + "\" must be a string");
    const std::string s = j[key].get<std::string>();
    if (s.empty() || s.size() > maxLength) bad(std::string("\"") + key + "\" must have 1 to " + std::to_string(maxLength) + " characters");
    return s;
}

double requireNumber(const json& j, const char* key, std::optional<double> fallback = std::nullopt) {
    if (!j.contains(key)) {
        if (fallback) return *fallback;
        bad(std::string("\"") + key + "\" is missing");
    }
    if (!j[key].is_number()) bad(std::string("\"") + key + "\" must be a number");
    const double v = j[key].get<double>();
    if (!std::isfinite(v)) bad(std::string("\"") + key + "\" must be finite");
    return v;
}

TrackKind kindFromString(const std::string& s) {
    if (s == "audio") return TrackKind::Audio;
    if (s == "instrument") return TrackKind::Instrument;
    if (s == "aux") return TrackKind::Aux;
    if (s == "bus") return TrackKind::Bus;
    bad("\"kind\" must be audio, instrument, aux or bus");
}

ProcessorRef parseProcessor(const json& j) {
    if (!j.is_object()) bad("a processor must be an object");
    ProcessorRef ref;
    ref.processorId = requireString(j, "processorId", 64);
    if (j.contains("params")) {
        if (!j["params"].is_object()) bad("\"params\" must be an object");
        for (const auto& [name, value] : j["params"].items()) {
            if (!value.is_number()) bad("parameter " + name + " must be a number");
            ref.params[name] = value.get<double>();
        }
    }
    return ref;
}

SmartTarget parseTarget(const json& j, const std::vector<ProcessorRef>& inserts) {
    if (!j.is_object()) bad("a target must be an object");
    SmartTarget t;
    t.path = requireString(j, "path", 64);
    t.from = requireNumber(j, "from");
    t.to = requireNumber(j, "to");
    if (t.from == t.to) bad("target " + t.path + ": from and to must differ");
    if (t.path == "strip.gainDb") {
        t.kind = SmartTarget::Kind::StripGain;
        if (auto e = checkStripValues(static_cast<float>(t.from), 0.0f)) bad("target " + t.path + ": " + e->message);
        if (auto e = checkStripValues(static_cast<float>(t.to), 0.0f)) bad("target " + t.path + ": " + e->message);
    } else if (t.path == "strip.pan") {
        t.kind = SmartTarget::Kind::StripPan;
        if (auto e = checkStripValues(0.0f, static_cast<float>(t.from))) bad("target " + t.path + ": " + e->message);
        if (auto e = checkStripValues(0.0f, static_cast<float>(t.to))) bad("target " + t.path + ": " + e->message);
    } else if (t.path.rfind("insert.", 0) == 0) {
        const std::size_t dot = t.path.find('.', 7);
        if (dot == std::string::npos) bad("target " + t.path + ": expected insert.<index>.<param>");
        const std::string indexText = t.path.substr(7, dot - 7);
        if (indexText.empty() || indexText.size() > 3 || !std::all_of(indexText.begin(), indexText.end(), [](unsigned char c) { return std::isdigit(c); }))
            bad("target " + t.path + ": bad insert index");
        t.kind = SmartTarget::Kind::InsertParam;
        t.index = static_cast<std::size_t>(std::stoul(indexText));
        t.param = t.path.substr(dot + 1);
        if (t.index >= inserts.size()) bad("target " + t.path + ": the patch has no such insert");
        for (const double v : {t.from, t.to}) {
            ProcessorRef probe = inserts[t.index];
            probe.params[t.param] = v;
            if (auto e = checkInsert(probe)) bad("target " + t.path + ": " + e->message);
        }
    } else {
        bad("target path " + t.path + " is not strip.gainDb, strip.pan or insert.<index>.<param>");
    }
    return t;
}

SmartControl parseControl(const json& j, const std::vector<ProcessorRef>& inserts) {
    if (!j.is_object()) bad("a smart control must be an object");
    SmartControl c;
    c.id = requireString(j, "id", 32);
    c.label = requireString(j, "label", 32);
    c.group = j.contains("group") ? requireString(j, "group", 32) : std::string();
    c.min = requireNumber(j, "min");
    c.max = requireNumber(j, "max");
    c.def = requireNumber(j, "default");
    if (!(c.min < c.max)) bad("smart control " + c.id + ": min must be below max");
    if (c.def < c.min || c.def > c.max) bad("smart control " + c.id + ": default is outside min..max");
    if (!j.contains("targets") || !j["targets"].is_array() || j["targets"].empty()) bad("smart control " + c.id + ": needs at least one target");
    for (const json& t : j["targets"]) c.targets.push_back(parseTarget(t, inserts));
    return c;
}

Patch parsePatch(const json& j) {
    if (!j.is_object()) bad("a patch must be an object");
    Patch p;
    p.id = requireString(j, "id", 64);
    if (!validPatchId(p.id)) bad("the id may only contain letters, digits, '.', '_' and '-'");
    p.category = requireString(j, "category", 64);
    p.name = requireString(j, "name", 64);
    p.kind = kindFromString(requireString(j, "kind", 16));
    const bool hasInstrument = j.contains("instrument") && !j["instrument"].is_null();
    if (p.kind == TrackKind::Instrument) {
        if (!hasInstrument) bad("an instrument patch needs an instrument");
        p.instrument = parseProcessor(j["instrument"]);
        if (!isKnownInstrument(p.instrument->processorId)) bad("unknown instrument: " + p.instrument->processorId);
    } else if (hasInstrument) {
        bad("only instrument patches have an instrument");
    }
    if (j.contains("strip")) {
        if (!j["strip"].is_object()) bad("\"strip\" must be an object");
        p.gainDb = static_cast<float>(requireNumber(j["strip"], "gainDb", 0.0));
        p.pan = static_cast<float>(requireNumber(j["strip"], "pan", 0.0));
        if (auto e = checkStripValues(p.gainDb, p.pan)) bad(e->message);
    }
    if (j.contains("inserts")) {
        if (!j["inserts"].is_array()) bad("\"inserts\" must be an array");
        for (const json& i : j["inserts"]) {
            p.inserts.push_back(parseProcessor(i));
            if (auto e = checkInsert(p.inserts.back())) bad(e->message);
        }
    }
    if (j.contains("smartControls")) {
        if (!j["smartControls"].is_array()) bad("\"smartControls\" must be an array");
        std::set<std::string> ids;
        for (const json& c : j["smartControls"]) {
            p.smartControls.push_back(parseControl(c, p.inserts));
            if (!ids.insert(p.smartControls.back().id).second) bad("smart control id " + p.smartControls.back().id + " used twice");
        }
    }
    return p;
}

double mapRange(const SmartControl& c, double value, const SmartTarget& t) {
    const double v = std::clamp(value, c.min, c.max);
    return t.from + (v - c.min) / (c.max - c.min) * (t.to - t.from);
}

}  // namespace

PatchLibrary PatchLibrary::fromJson(const json& doc) {
    PatchLibrary lib;
    if (!doc.is_object() || !doc.contains("patches") || !doc["patches"].is_array()) {
        lib.problems_.push_back({"", "the catalogue must be an object with a \"patches\" array"});
        return lib;
    }
    std::set<std::string> seen;
    std::size_t index = 0;
    for (const json& entry : doc["patches"]) {
        std::string where = "#" + std::to_string(index++);
        if (entry.is_object() && entry.contains("id") && entry["id"].is_string()) where = entry["id"].get<std::string>();
        try {
            Patch p = parsePatch(entry);
            if (!seen.insert(p.id).second) bad("duplicate patch id");
            lib.patches_.push_back(std::move(p));
        } catch (const std::exception& e) {
            lib.problems_.push_back({where, e.what()});
        }
    }
    return lib;
}

PatchLibrary PatchLibrary::fromFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        PatchLibrary lib;
        lib.problems_.push_back({file.string(), "cannot open the patch catalogue"});
        return lib;
    }
    try {
        return fromJson(json::parse(in));
    } catch (const std::exception& e) {
        PatchLibrary lib;
        lib.problems_.push_back({file.string(), std::string("not valid JSON: ") + e.what()});
        return lib;
    }
}

const std::vector<Patch>& PatchLibrary::patches() const { return patches_; }
const std::vector<PatchProblem>& PatchLibrary::problems() const { return problems_; }

const Patch* PatchLibrary::find(const std::string& id) const {
    const auto it = std::find_if(patches_.begin(), patches_.end(), [&](const Patch& p) { return p.id == id; });
    return it == patches_.end() ? nullptr : &*it;
}

std::vector<std::string> PatchLibrary::categories(TrackKind kind) const {
    std::set<std::string> out;
    for (const Patch& p : patches_)
        if (p.kind == kind) out.insert(p.category);
    return {out.begin(), out.end()};
}

std::vector<const Patch*> PatchLibrary::inCategory(TrackKind kind, const std::string& category) const {
    std::vector<const Patch*> out;
    for (const Patch& p : patches_)
        if (p.kind == kind && p.category == category) out.push_back(&p);
    return out;
}

CommandPtr PatchLibrary::applyCommand(const Uuid& trackId, TrackKind trackKind, const std::string& patchId) const {
    const Patch* p = find(patchId);
    if (!p || p->kind != trackKind) return nullptr;
    std::vector<CommandPtr> steps;
    steps.push_back(makeSetPatchId(trackId, p->id));
    if (p->instrument) steps.push_back(makeSetInstrument(trackId, *p->instrument));
    steps.push_back(makeSetInserts(trackId, p->inserts));
    StripPatch strip;
    strip.gainDb = p->gainDb;
    strip.pan = p->pan;
    steps.push_back(makeSetStrip(trackId, strip));
    return makeTransaction(std::move(steps));
}

CommandPtr PatchLibrary::smartControlCommand(const Uuid& trackId, const SmartControl& control, double value) {
    if (!std::isfinite(value)) return nullptr;
    StripPatch strip;
    std::vector<CommandPtr> steps;
    for (const SmartTarget& t : control.targets) {
        const double v = mapRange(control, value, t);
        switch (t.kind) {
            case SmartTarget::Kind::StripGain: strip.gainDb = static_cast<float>(v); break;
            case SmartTarget::Kind::StripPan: strip.pan = static_cast<float>(v); break;
            case SmartTarget::Kind::InsertParam:
                steps.push_back(makeSetInsertParam(trackId, static_cast<int>(t.index), t.param, v));
                break;
        }
    }
    if (strip.gainDb || strip.pan) steps.insert(steps.begin(), makeSetStrip(trackId, strip));
    return makeTransaction(std::move(steps));
}

std::optional<double> PatchLibrary::smartControlValue(const Track& track, const SmartControl& control) {
    if (control.targets.empty()) return std::nullopt;
    const SmartTarget& t = control.targets.front();
    double current = 0.0;
    switch (t.kind) {
        case SmartTarget::Kind::StripGain: current = track.strip.gainDb; break;
        case SmartTarget::Kind::StripPan: current = track.strip.pan; break;
        case SmartTarget::Kind::InsertParam: {
            if (t.index >= track.strip.inserts.size()) return std::nullopt;
            const auto& params = track.strip.inserts[t.index].params;
            const auto it = params.find(t.param);
            current = it == params.end() ? 0.0 : it->second;
            break;
        }
    }
    const double fraction = (current - t.from) / (t.to - t.from);
    return std::clamp(control.min + fraction * (control.max - control.min), control.min, control.max);
}

}  // namespace lpc
