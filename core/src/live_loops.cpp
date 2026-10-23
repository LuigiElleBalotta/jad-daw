#include "lpc/live_loops.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace lpc {

namespace {

std::int64_t framesBetweenTicks(const Project& p, Ticks a, Ticks b) {
    return static_cast<std::int64_t>(std::llround(p.tempoMap.ticksToSamples(b, p.sampleRate) - p.tempoMap.ticksToSamples(a, p.sampleRate)));
}

}  // namespace

std::optional<Region> cellFromRegions(const Project& p, const Track& t, const std::vector<const Region*>& regions, Ticks from, Ticks to, const Uuid& newId) {
    if (to <= from) return std::nullopt;
    Region cell;
    cell.id = newId;
    cell.timeBase = TimeBase::Musical;
    cell.start = 0;
    cell.length = to - from;
    cell.loopLength = cell.length;
    bool any = false;
    for (const Region* src : regions) {
        if (!src || src->muted || src->timeBase != TimeBase::Musical) continue;
        const Ticks a = std::max(from, src->start), b = std::min(to, src->start + src->length);
        if (b <= a) continue;
        if (t.kind == TrackKind::Audio) {
            if (any) break;  // one sound per cell
            cell.mediaId = src->mediaId;
            cell.sourceOffsetFrames = src->sourceOffsetFrames + framesBetweenTicks(p, src->start, a);
            cell.gainDb = src->gainDb;
            cell.name = src->name;
            if (a > from) {  // the sound starts inside the window: the cell begins with silence, so it is shorter and starts where the sound does
                cell.length = b - a;
                cell.loopLength = cell.length;
            } else if (b < to) {
                cell.length = b - from;
                cell.loopLength = cell.length;
            }
            if (src->flex) cell.flex = src->flex;
            any = true;
            continue;
        }
        any = true;
        const Ticks shift = src->start - from;  // where the region's tick 0 lies in the cell
        for (MidiNote n : src->notes) {
            const Ticks s = shift + n.start, e = shift + n.start + n.length;
            if (e <= 0 || s >= to - from) continue;
            n.start = std::max<Ticks>(0, s);
            n.length = std::min(e, to - from) - n.start;
            if (n.length > 0) cell.notes.push_back(n);
        }
        for (MidiControl c : src->controls) {
            const Ticks s = shift + c.tick;
            if (s < 0 || s >= to - from) continue;
            c.tick = s;
            cell.controls.push_back(c);
        }
        if (cell.name.empty()) cell.name = src->name;
    }
    if (!any) return std::nullopt;
    std::stable_sort(cell.notes.begin(), cell.notes.end(), [](const MidiNote& x, const MidiNote& y) { return x.start < y.start; });
    std::stable_sort(cell.controls.begin(), cell.controls.end(), [](const MidiControl& x, const MidiControl& y) { return x.tick < y.tick; });
    return cell;
}

int firstEmptyScene(const Project& p) {
    std::set<int> used;
    for (const Track& t : p.tracks)
        for (const LiveCell& c : t.cells) used.insert(c.scene);
    int scene = 0;
    while (used.count(scene)) ++scene;
    return scene;
}

Ticks launchTick(Ticks positionTicks, Ticks quantizeTicks) {
    if (quantizeTicks <= 0) return positionTicks;
    const Ticks rem = positionTicks % quantizeTicks;
    return rem == 0 ? positionTicks : positionTicks + (quantizeTicks - rem);
}

}  // namespace lpc
