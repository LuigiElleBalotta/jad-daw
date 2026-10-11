#include "lpc/flex_model.h"

#include <algorithm>
#include <cmath>

namespace lpc {

namespace {
constexpr std::size_t kMaxMarkers = 4096, kMaxNotes = 4096, kMaxBeats = 65536;
constexpr std::int64_t kLimit = std::int64_t{1} << 40;
bool finite(double v) { return std::isfinite(v); }
}  // namespace

std::vector<FlexAnchor> flexAnchors(const FlexData& f) {
    std::vector<FlexAnchor> a;
    a.reserve(f.markers.size());
    for (const FlexMarker& m : f.markers) a.push_back({m.src, m.dst});
    return a;
}

std::vector<FlexNote> flexNotes(const FlexData& f) {
    std::vector<FlexNote> out;
    out.reserve(f.notes.size());
    for (const FlexNoteData& n : f.notes) out.push_back({n.start, n.length, n.pitch});
    return out;
}

std::vector<NoteEdit> flexEdits(const FlexData& f) {
    std::vector<NoteEdit> out;
    out.reserve(f.notes.size());
    for (const FlexNoteData& n : f.notes) {
        NoteEdit e;
        e.semitones = n.semitones;
        e.fineCents = n.fineCents;
        e.correct = n.correct;
        e.vibrato = n.vibrato;
        e.driftIn = n.driftIn;
        e.driftOut = n.driftOut;
        e.gainDb = n.gainDb;
        e.formantCents = n.formantCents;
        out.push_back(e);
    }
    return out;
}

std::vector<FlexNoteData> flexNoteData(const std::vector<FlexNote>& notes) {
    std::vector<FlexNoteData> out;
    out.reserve(notes.size());
    for (const FlexNote& n : notes) {
        FlexNoteData d;
        d.start = n.start;
        d.length = n.length;
        d.pitch = n.pitch;
        out.push_back(d);
    }
    return out;
}

std::optional<FlexData> flexWindow(const FlexData& f, std::int64_t total, std::int64_t from, std::int64_t length, std::int64_t mediaFrames) {
    if (length <= 0 || total <= 0) return std::nullopt;
    const TimeMap map(f.sourceLength, total, flexAnchors(f));
    FlexData out = f;
    // where the new first and last frames come from in the source
    std::int64_t srcFrom, srcTo;
    if (from <= 0) srcFrom = static_cast<std::int64_t>(std::llround(map.toSource(0.0))) + from;           // before the start: 1:1
    else srcFrom = static_cast<std::int64_t>(std::llround(map.toSource(static_cast<double>(from))));
    const std::int64_t end = from + length;
    if (end >= total) srcTo = f.sourceLength + (end - total);                                              // after the end: 1:1
    else srcTo = static_cast<std::int64_t>(std::llround(map.toSource(static_cast<double>(end))));
    if (srcTo <= srcFrom) srcTo = srcFrom + 1;
    const std::int64_t newOffset = f.sourceOffset + srcFrom;
    if (newOffset < 0 || newOffset + (srcTo - srcFrom) > mediaFrames) return std::nullopt;
    out.sourceOffset = newOffset;
    out.sourceLength = srcTo - srcFrom;
    out.markers.clear();
    for (const FlexMarker& m : f.markers) {
        const std::int64_t src = m.src - srcFrom, dst = m.dst - from;
        if (src <= 0 || src >= out.sourceLength || dst <= 0 || dst >= length) continue;
        out.markers.push_back({src, dst});
    }
    out.notes.clear();
    for (FlexNoteData n : f.notes) {
        n.start -= srcFrom;
        if (n.start < 0) { n.length += n.start; n.start = 0; }
        if (n.start + n.length > out.sourceLength) n.length = out.sourceLength - n.start;
        if (n.length <= 0) continue;
        out.notes.push_back(n);
    }
    if (f.beats) {
        FlexBeats& nb = *out.beats;
        nb.beats.clear();
        std::int64_t removed = 0;
        for (std::int64_t t : f.beats->beats) {
            const std::int64_t src = t - srcFrom;
            if (src < 0) { ++removed; continue; }
            if (src >= out.sourceLength) continue;
            nb.beats.push_back(src);
        }
        const int bpb = std::max(1, f.beats->beatsPerBar);
        nb.firstDownbeat = static_cast<int>((((f.beats->firstDownbeat - removed) % bpb) + bpb) % bpb);
        if (nb.beats.empty()) out.beats.reset();
    }
    return out;
}

std::optional<FlexData> flexJoin(const FlexData& a, std::int64_t aTotal, const FlexData& b) {
    if (a.sourceMediaId != b.sourceMediaId || a.sourceOffset + a.sourceLength != b.sourceOffset) return std::nullopt;
    FlexData out = a;
    out.sourceLength = a.sourceLength + b.sourceLength;
    for (FlexMarker m : b.markers) {
        m.src += a.sourceLength;
        m.dst += aTotal;
        out.markers.push_back(m);
    }
    // the cut itself is a stretch boundary: a marker there keeps the two halves as they were
    if (a.sourceLength != aTotal || !a.markers.empty() || !b.markers.empty()) out.markers.push_back({a.sourceLength, aTotal});
    std::sort(out.markers.begin(), out.markers.end(), [](const FlexMarker& x, const FlexMarker& y) { return x.src < y.src; });
    out.markers.erase(std::unique(out.markers.begin(), out.markers.end(), [](const FlexMarker& x, const FlexMarker& y) { return x.src == y.src; }), out.markers.end());
    for (FlexNoteData n : b.notes) {
        n.start += a.sourceLength;
        out.notes.push_back(n);
    }
    if (b.beats) {
        if (!out.beats) {
            out.beats = FlexBeats{};
            out.beats->follow = b.beats->follow;
            out.beats->bpm = b.beats->bpm;
            out.beats->beatsPerBar = b.beats->beatsPerBar;
            out.beats->firstDownbeat = b.beats->firstDownbeat;
        }
        for (std::int64_t t : b.beats->beats) out.beats->beats.push_back(t + a.sourceLength);
    }
    return out;
}

std::string flexProblem(const FlexData& f, std::int64_t mediaFrames) {
    if (f.sourceMediaId.isNull()) return "flex data needs the media it was made from";
    if (f.sourceOffset < 0 || f.sourceLength <= 0 || f.sourceLength > kLimit || f.sourceOffset + f.sourceLength > mediaFrames) return "flex data reaches outside its source media";
    if (f.markers.size() > kMaxMarkers) return "a region holds at most 4096 flex markers";
    std::int64_t lastSrc = 0, lastDst = 0;
    for (const FlexMarker& m : f.markers) {
        if (m.src <= lastSrc || m.src >= f.sourceLength || m.dst <= lastDst || m.dst > kLimit) return "flex markers must be in order and inside the sound";
        lastSrc = m.src;
        lastDst = m.dst;
    }
    if (f.notes.size() > kMaxNotes) return "a region holds at most 4096 flex notes";
    for (const FlexNoteData& n : f.notes) {
        if (n.start < 0 || n.length <= 0 || n.start + n.length > f.sourceLength) return "a flex note must lie inside the sound";
        if (!finite(n.pitch) || n.pitch < 0 || n.pitch > 127) return "a flex note pitch is 0..127";
        if (!finite(n.semitones) || std::abs(n.semitones) > 48 || !finite(n.fineCents) || std::abs(n.fineCents) > 100 || !finite(n.correct) || n.correct < 0 || n.correct > 1 ||
            !finite(n.vibrato) || n.vibrato < 0 || n.vibrato > 4 || !finite(n.driftIn) || n.driftIn < 0 || n.driftIn > 1 || !finite(n.driftOut) || n.driftOut < 0 || n.driftOut > 1 ||
            !finite(n.gainDb) || std::abs(n.gainDb) > 24 || !finite(n.formantCents) || std::abs(n.formantCents) > 1200)
            return "a flex note edit is out of range";
    }
    if (f.beats) {
        const FlexBeats& b = *f.beats;
        if (b.beats.size() > kMaxBeats) return "a region holds at most 65536 beats";
        if (!finite(b.bpm) || b.bpm < 0 || b.bpm > 1000 || b.beatsPerBar < 1 || b.beatsPerBar > 32 || b.firstDownbeat < 0 || b.firstDownbeat >= b.beatsPerBar || !finite(b.anchor) || std::abs(b.anchor) > 1e7) return "the beats of a flex region are out of range";
        std::int64_t last = -1;
        for (std::int64_t t : b.beats) {
            if (t <= last || t >= f.sourceLength) return "beats must be in order and inside the sound";
            last = t;
        }
    }
    return {};
}

}  // namespace lpc
