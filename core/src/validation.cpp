#include "lpc/validation.h"
#include "lpc/effect_specs.h"

#include <cctype>
#include <cmath>
#include <unordered_set>
#include <vector>

#include "lpc/audio/render_graph.h"
#include "lpc/processor_ids.h"

namespace lpc {

static_assert(kMaxProjectTracks == static_cast<std::size_t>(audio::kMaxTracks), "project and engine track limits must match");

namespace {
bool inRange(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }
bool inRange(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
}  // namespace

MaybeError checkStripValues(float gainDb, float pan) {
    if (!inRange(gainDb, -96.0f, 24.0f)) return CommandError{"bad_value", "gainDb must be a number in [-96, 24]"};
    if (!inRange(pan, -1.0f, 1.0f)) return CommandError{"bad_value", "pan must be a number in [-1, 1]"};
    return std::nullopt;
}

// A region is valid for a given track kind and project (media must exist, notes must be sane).
MaybeError checkRegion(const Project& p, TrackKind kind, const Region& r) {
    if (r.id.isNull()) return CommandError{"duplicate_id", "region id is missing"};
    if (r.start < 0 || r.length <= 0) return CommandError{"bad_region", "region needs start >= 0 and length > 0"};
    if (r.start > kMaxPosition || r.length > kMaxPosition) return CommandError{"bad_region", "region start and length must be at most 2^40"};
    if (!inRange(r.gainDb, -96.0f, 24.0f)) return CommandError{"bad_value", "region gainDb must be in [-96, 24]"};
    if (r.transpose < -48 || r.transpose > 48 || r.velocityOffset < -127 || r.velocityOffset > 127 || r.quantize < 0 || r.quantize > 4 * kPPQ)
        return CommandError{"bad_value", "region transpose is -48..48, velocity -127..127 and quantize 0 to a whole note"};
    if (r.takeGroup.size() > 64) return CommandError{"bad_value", "a take group name has at most 64 characters"};
    if (r.loopLength < 0 || r.loopLength > r.length) return CommandError{"bad_region", "a region loop is between 0 and the length of the region"};
    if (kind == TrackKind::Audio) {
        if (r.mediaId.isNull() || !p.findMedia(r.mediaId))
            return CommandError{"bad_region", "audio region needs a mediaId present in the media pool"};
        if (!r.notes.empty() || !r.controls.empty()) return CommandError{"bad_region", "audio regions cannot contain notes"};
        if (r.sourceOffsetFrames < 0 || r.sourceOffsetFrames > kMaxPosition)
            return CommandError{"bad_region", "sourceOffsetFrames must be in [0, 2^40]"};
    } else if (kind == TrackKind::Midi || kind == TrackKind::Instrument) {
        if (!r.mediaId.isNull()) return CommandError{"bad_region", "MIDI regions cannot reference media"};
        if (r.timeBase != TimeBase::Musical) return CommandError{"bad_region", "MIDI regions must be musical"};
        if (r.controls.size() > 16384) return CommandError{"bad_region", "a region holds at most 16384 controller events"};
        for (std::size_t i = 0; i < r.controls.size(); ++i) {
            const MidiControl& c = r.controls[i];
            const bool kind = c.status == 0xB0 || c.status == 0xD0 || c.status == 0xE0;
            if (!kind || c.tick < 0 || c.tick > r.length || c.data1 > 127 || c.data2 > 127 || (i > 0 && c.tick < r.controls[i - 1].tick))
                return CommandError{"bad_region", "invalid MIDI controller event (kinds 0xB0, 0xD0, 0xE0; data 0..127; sorted; inside the region)"};
        }
        for (const MidiNote& n : r.notes) {
            if (n.start < 0 || n.length <= 0 || n.start > kMaxPosition || n.length > kMaxPosition || n.note > 127 || n.velocity < 1 ||
                n.velocity > 127)
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

bool validBase64(const std::string& s) {
    if (s.size() % 4 != 0) return false;
    std::size_t padding = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '=') {
            ++padding;
            if (i < s.size() - 2) return false;  // padding only in the last two places
            continue;
        }
        if (padding > 0) return false;
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/';
        if (!ok) return false;
    }
    return padding <= 2;
}

MaybeError checkInsert(const ProcessorRef& insert) {
    if (insert.label.size() > kMaxInsertLabelBytes) return CommandError{"bad_value", "insert label is too long"};
    if (isVst3Id(insert.processorId)) {
        if (!insert.params.empty()) return CommandError{"bad_value", "plug-in inserts take no parameters"};
        if (insert.state.size() > kMaxPluginStateChars || !validBase64(insert.state))
            return CommandError{"bad_value", "plug-in state must be base64 of at most 16 MiB"};
        return std::nullopt;
    }
    if (insert.processorId.rfind("vst3:", 0) == 0) return CommandError{"bad_value", "malformed plug-in id: " + insert.processorId};
    if (!isKnownEffect(insert.processorId)) return CommandError{"bad_value", "unknown insert processor: " + insert.processorId};
    const EffectSpec* spec = findEffectSpec(insert.processorId);  // every parameter must be one of the effect's, inside its range
    for (const auto& [name, value] : insert.params) {
        const EffectParam* param = spec ? spec->find(name) : nullptr;
        if (!param) return CommandError{"bad_value", "unknown parameter '" + name + "' for " + insert.processorId};
        if (!inRange(value, param->min, param->max))
            return CommandError{"bad_value", name + " must be a number in [" + std::to_string(param->min) + ", " + std::to_string(param->max) + "]"};
    }
    return std::nullopt;
}

MaybeError checkInstrument(const ProcessorRef& instrument) {
    if (isVst3Id(instrument.processorId)) return checkInsert(instrument);
    if (instrument.processorId.rfind("vst3:", 0) == 0) return CommandError{"bad_value", "malformed plug-in id: " + instrument.processorId};
    if (!isKnownInstrument(instrument.processorId)) return CommandError{"bad_value", "unknown instrument: " + instrument.processorId};
    return std::nullopt;
}

MaybeError checkMidiShaping(const MidiShaping& m) {
    if (m.transpose < -48 || m.transpose > 48 || m.velocity < -127 || m.velocity > 127)
        return CommandError{"bad_value", "track transpose is -48..48 and velocity -127..127"};
    if (m.keyLow < 0 || m.keyHigh > 127 || m.keyLow > m.keyHigh) return CommandError{"bad_value", "the key limit is 0..127 with low <= high"};
    if (m.velocityLow < 1 || m.velocityHigh > 127 || m.velocityLow > m.velocityHigh) return CommandError{"bad_value", "the velocity limit is 1..127 with low <= high"};
    return std::nullopt;
}

bool isKnownTrackColor(const std::string& color) {
    static const char* const names[] = {"purple", "indigo", "blue", "teal", "green", "yellow", "orange", "red", "pink", "magenta"};
    if (color.empty()) return true;
    for (const char* n : names)
        if (color == n) return true;
    return false;
}

bool validTrackName(const std::string& name) {
    std::size_t points = 0;
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7f) return false;
        if ((c & 0xC0) != 0x80) ++points;  // count lead bytes, skip continuation bytes
    }
    return points >= 1 && points <= 64;
}

MaybeError checkTrackProps(const std::string& name, const std::string& color) {
    if (!validTrackName(name)) return CommandError{"bad_value", "a track name has 1 to 64 characters and no control characters"};
    if (!isKnownTrackColor(color)) return CommandError{"bad_value", "unknown track colour: " + color};
    return std::nullopt;
}

MaybeError checkShowInTracks(TrackKind kind, bool show) {
    if (!show && kind == TrackKind::Master) return CommandError{"bad_value", "the master track cannot be hidden from the Tracks area"};
    return std::nullopt;
}

bool validPatchId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (const unsigned char c : id)
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) return false;
    return true;
}

MaybeError checkPatchId(const std::string& id) {
    if (!id.empty() && !validPatchId(id)) return CommandError{"bad_value", "a patch id has 1 to 64 characters of letters, digits, '.', '_' or '-'"};
    return std::nullopt;
}

bool validRelativeMediaPath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos || path.find('\\') != std::string::npos) return false;
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

MaybeError checkProject(const Project& p) {
    if (p.sampleRate < 8000 || p.sampleRate > 384000) return CommandError{"bad_value", "unsupported sample rate"};
    if (p.tracks.size() > kMaxProjectTracks) return CommandError{"limit", "a project holds at most 1024 tracks"};

    std::unordered_set<Uuid> trackIds, mediaIds, regionIds, sendIds;
    for (const Track& t : p.tracks)
        if (t.id.isNull() || !trackIds.insert(t.id).second) return CommandError{"duplicate_id", "track id missing or used twice"};

    std::unordered_set<Uuid> groupIds, grouped;
    if (p.groups.size() > 64) return CommandError{"limit", "a project holds at most 64 groups"};
    for (const Group& g : p.groups) {
        if (g.id.isNull() || !groupIds.insert(g.id).second) return CommandError{"duplicate_id", "group id missing or used twice"};
        if (g.name.empty() || g.name.size() > 64) return CommandError{"bad_value", "a group name has 1 to 64 bytes"};
        for (const Uuid& m : g.members) {
            const Track* t = p.findTrack(m);
            if (!t || t->kind == TrackKind::Master) return CommandError{"not_found", "a group member is not a track (or is the master)"};
            if (!grouped.insert(m).second) return CommandError{"bad_value", "a track is in two groups"};
        }
    }

    for (const MediaItem& m : p.mediaPool) {
        if (m.id.isNull() || !mediaIds.insert(m.id).second) return CommandError{"duplicate_id", "media id missing or used twice"};
        if (!validRelativeMediaPath(m.path)) return CommandError{"bad_media", "media path must be relative, inside the project, with '/' separators"};
        if (m.sampleRate != p.sampleRate) return CommandError{"bad_media", "media sample rate must equal the project sample rate"};
        if (m.channels < 1 || m.channels > 2) return CommandError{"bad_media", "media must be mono or stereo"};
        if (m.frames < 0 || m.frames > kMaxPosition) return CommandError{"bad_media", "media frame count out of range"};
    }

    for (const Track& t : p.tracks) {
        if (auto e = checkTrackProps(t.name, t.color)) return e;
        if (auto e = checkPatchId(t.patchId)) return e;
        if (auto e = checkShowInTracks(t.kind, t.showInTracks)) return e;
        if (auto e = checkMidiShaping(t.midi)) return e;
        if (!(t.delayMs >= -1000.0 && t.delayMs <= 1000.0)) return CommandError{"bad_value", "track delay is -1000..1000 ms"};
        if (auto e = checkStripValues(t.strip.gainDb, t.strip.pan)) return e;
        const bool wantsInstrument = t.kind == TrackKind::Instrument;
        if (wantsInstrument != t.instrument.has_value()) return CommandError{"invalid_kind", "exactly instrument tracks need an instrument"};
        if (wantsInstrument)
            if (auto e = checkInstrument(*t.instrument)) return e;
        for (const ProcessorRef& ins : t.strip.inserts)
            if (auto e = checkInsert(ins)) return e;
        if (!t.strip.output.isNull()) {
            const Track* out = p.findTrack(t.strip.output);
            if (!out || !isBusLike(out->kind) || out->id == t.id)
                return CommandError{"bad_output", "output must be another existing bus or aux track (or null for master)"};
        }
        for (const Send& s : t.strip.sends) {
            if (auto e = checkSendFields(p, t.id, s)) return e;
            if (!sendIds.insert(s.id).second) return CommandError{"duplicate_id", "send id used twice"};
        }
        for (const Region& r : t.regions) {
            if (auto e = checkRegion(p, t.kind, r)) return e;
            if (!regionIds.insert(r.id).second) return CommandError{"duplicate_id", "region id used twice"};
        }
    }

    for (const Track& t : p.tracks) {
        if (!t.strip.output.isNull() && reaches(p, t.strip.output, t.id)) return CommandError{"cycle", "routing loop through track output"};
        for (const Send& s : t.strip.sends)
            if (reaches(p, s.targetTrackId, t.id)) return CommandError{"cycle", "routing loop through a send"};
    }
    return std::nullopt;
}

}  // namespace lpc
