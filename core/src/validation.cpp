#include "lpc/validation.h"

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
    if (kind == TrackKind::Audio) {
        if (r.mediaId.isNull() || !p.findMedia(r.mediaId))
            return CommandError{"bad_region", "audio region needs a mediaId present in the media pool"};
        if (!r.notes.empty()) return CommandError{"bad_region", "audio regions cannot contain notes"};
        if (r.sourceOffsetFrames < 0 || r.sourceOffsetFrames > kMaxPosition)
            return CommandError{"bad_region", "sourceOffsetFrames must be in [0, 2^40]"};
    } else if (kind == TrackKind::Midi || kind == TrackKind::Instrument) {
        if (!r.mediaId.isNull()) return CommandError{"bad_region", "MIDI regions cannot reference media"};
        if (r.timeBase != TimeBase::Musical) return CommandError{"bad_region", "MIDI regions must be musical"};
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

MaybeError checkInsert(const ProcessorRef& insert) {
    if (!isKnownEffect(insert.processorId)) return CommandError{"bad_value", "unknown insert processor: " + insert.processorId};
    // builtin.gain is the only effect for now: one parameter, gainDb
    for (const auto& [name, value] : insert.params) {
        if (name != "gainDb") return CommandError{"bad_value", "unknown parameter '" + name + "' for " + insert.processorId};
        if (!inRange(value, -96.0, 24.0)) return CommandError{"bad_value", "gainDb must be a number in [-96, 24]"};
    }
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
        if (auto e = checkStripValues(t.strip.gainDb, t.strip.pan)) return e;
        const bool wantsInstrument = t.kind == TrackKind::Instrument;
        if (wantsInstrument != t.instrument.has_value() || (wantsInstrument && !isKnownInstrument(t.instrument->processorId)))
            return CommandError{"invalid_kind", "exactly instrument tracks need a known instrument"};
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
