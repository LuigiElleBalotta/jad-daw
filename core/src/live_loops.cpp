#include "lpc/live_loops.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <system_error>

#include "lpc/tempo_analysis.h"
#include "lpc/uuid.h"
#include "lpc/wav.h"

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

CellSpan cellSpan(const LiveCell& c) {
    CellSpan s;
    s.length = std::max<Ticks>(1, c.region.length);
    s.loop = c.loop;
    s.loopStart = std::clamp<Ticks>(c.loopStart, 0, s.length - 1);
    const Ticks len = c.loopLength > 0 ? std::min(c.loopLength, s.length - s.loopStart) : s.length - s.loopStart;
    s.loopEnd = s.loopStart + std::max<Ticks>(1, len);
    return s;
}

Ticks cellPositionAt(const LiveCell& c, Ticks offset, Ticks elapsed, bool* finished) {
    const CellSpan s = cellSpan(c);
    if (finished) *finished = false;
    Ticks o = std::clamp<Ticks>(offset, 0, s.length - 1);
    if (!s.loop) {
        const Ticks at = o + elapsed;
        if (at >= s.length) {
            if (finished) *finished = true;
            return s.length;
        }
        return at;
    }
    if (o >= s.loopEnd) o = s.loopStart;
    if (o + elapsed < s.loopEnd) return o + elapsed;
    const Ticks loopLen = s.loopEnd - s.loopStart;
    return s.loopStart + (o + elapsed - s.loopEnd) % loopLen;
}

Ticks ticksToCellEnd(const LiveCell& c, Ticks offset, Ticks elapsed) {
    const CellSpan s = cellSpan(c);
    Ticks o = std::clamp<Ticks>(offset, 0, s.length - 1);
    if (!s.loop) return o + elapsed >= s.length ? -1 : s.length - (o + elapsed);
    if (o >= s.loopEnd) o = s.loopStart;
    const Ticks pos = cellPositionAt(c, o, elapsed);
    return s.loopEnd - pos;
}

std::vector<Region> playedRegions(const Project& p, const Track& t, const LiveRun& run, const LiveCell& cell) {
    std::vector<Region> out;
    const CellSpan s = cellSpan(cell);
    const Ticks o0 = std::clamp<Ticks>(run.offset, 0, s.length - 1);
    const auto piece = [&](Ticks from, Ticks to, Ticks at, bool looped) {
        if (to <= from) return;
        Ticks length = looped ? (to - from) * 1024 : to - from;
        if (run.end >= 0) length = std::min(length, run.end - at);
        if (length <= 0) return;
        const std::vector<const Region*> src{&cell.region};
        auto r = cellFromRegions(p, t, src, from, to, cell.region.id);
        if (!r) return;
        r->start = at;
        r->loopLength = looped ? to - from : 0;
        r->length = length;
        out.push_back(std::move(*r));
    };
    if (!s.loop) {
        piece(o0, s.length, run.start, false);
        return out;
    }
    Ticks o = o0 >= s.loopEnd ? s.loopStart : o0;
    if (o < s.loopStart) {  // the intro, then the loop
        piece(o, s.loopStart, run.start, false);
        piece(s.loopStart, s.loopEnd, run.start + (s.loopStart - o), true);
    } else if (o > s.loopStart) {  // the rest of the loop, then the loop
        piece(o, s.loopEnd, run.start, false);
        piece(s.loopStart, s.loopEnd, run.start + (s.loopEnd - o), true);
    } else {
        piece(s.loopStart, s.loopEnd, run.start, true);
    }
    return out;
}

ImportedGrid importLiveLoopsGrid(const Project& target, const std::filesystem::path& targetDir, const Project& source, const std::filesystem::path& sourceDir) {
    ImportedGrid out;
    std::map<Uuid, Uuid> mediaMap;
    std::set<std::string> taken;
    for (const MediaItem& m : target.mediaPool) taken.insert(m.path);
    const auto bring = [&](const Uuid& id) -> Uuid {
        if (id.isNull()) return id;
        const auto done = mediaMap.find(id);
        if (done != mediaMap.end()) return done->second;
        const MediaItem* item = source.findMedia(id);
        if (!item) return Uuid{};
        std::filesystem::path from = std::filesystem::path(std::u8string(item->path.begin(), item->path.end()));
        if (from.is_relative()) from = sourceDir / from;
        const std::filesystem::path name = from.filename();
        std::filesystem::create_directories(targetDir / "audio");
        std::filesystem::path to;
        std::string relative;
        for (int n = 1;; ++n) {
            const std::u16string stem = name.stem().u16string(), ext = name.extension().u16string();
            to = targetDir / "audio" / (n == 1 ? stem + ext : stem + u" (" + std::u16string(1, static_cast<char16_t>(u'0' + n % 10)) + u")" + ext);
            const std::u8string rel = (std::filesystem::path("audio") / to.filename()).generic_u8string();
            relative.assign(rel.begin(), rel.end());
            if (!std::filesystem::exists(to) && !taken.count(relative)) break;
        }
        std::error_code ec;
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::none, ec);
        if (ec) return Uuid{};
        MediaItem copy = *item;
        copy.id = Uuid::random();
        copy.path = relative;
        taken.insert(relative);
        out.media.push_back(copy);
        mediaMap[id] = copy.id;
        return copy.id;
    };
    for (const Track& t : source.tracks) {
        if (t.cells.empty() || (t.kind != TrackKind::Audio && t.kind != TrackKind::Instrument && t.kind != TrackKind::Midi)) continue;
        Track nt = t;
        nt.id = Uuid::random();
        nt.regions.clear();
        nt.automation.clear();
        nt.alternatives.clear();
        nt.alternative.clear();
        nt.liveRuns.clear();
        nt.liveCells = false;
        nt.freeze.reset();
        nt.parent = Uuid{};
        nt.stack.clear();
        nt.strip.sends.clear();
        nt.strip.output = Uuid{};
        if (nt.instrument && nt.instrument->processorId == "builtin.sampler") nt.instrument->state.clear();  // its zones point into the other project's audio
        nt.cells.clear();
        for (const LiveCell& c : t.cells) {
            LiveCell nc = c;
            nc.id = Uuid::random();
            std::map<Uuid, Uuid> regionIds;
            const auto fix = [&](Region& r) {
                const auto known = regionIds.find(r.id);
                if (known != regionIds.end()) r.id = known->second;
                else {
                    const Uuid fresh = Uuid::random();
                    regionIds[r.id] = fresh;
                    r.id = fresh;
                }
                r.mediaId = bring(r.mediaId);
                r.flex.reset();  // the flexed sound is another file of the other project
            };
            for (LiveTake& take : nc.takes) fix(take.region);
            fix(nc.region);
            if (!nc.takes.empty()) nc.region = nc.takes[static_cast<std::size_t>(std::clamp(nc.activeTake, 0, static_cast<int>(nc.takes.size()) - 1))].region;
            const bool audio = t.kind == TrackKind::Audio;
            if (audio && nc.region.mediaId.isNull()) continue;  // its audio could not be found
            nt.cells.push_back(std::move(nc));
        }
        if (!nt.cells.empty()) out.tracks.push_back(std::move(nt));
    }
    return out;
}

std::optional<LoopSuggestion> suggestLoop(const WavData& audio, const Project& project) {
    if (audio.frames() < audio.sampleRate) return std::nullopt;
    const TempoAnalysis a = analyseTempo(audio, 60.0, 200.0, 0, false);
    if (a.bpm <= 0.0 || a.beats.size() < 4) return std::nullopt;
    const int perBar = std::max(1, a.beatsPerBar);
    const std::size_t first = static_cast<std::size_t>(a.firstDownbeat);
    if (first >= a.beats.size()) return std::nullopt;
    const std::int64_t startFrame = a.beats[first];
    const double framesPerBar = static_cast<double>(audio.sampleRate) * 60.0 / a.bpm * perBar;
    const double room = static_cast<double>(audio.frames() - startFrame);
    int bars = 0;
    for (const int candidate : {8, 4, 2, 1})
        if (candidate * framesPerBar <= room + framesPerBar * 0.1) {
            bars = candidate;
            break;
        }
    if (bars == 0) return std::nullopt;
    const double beatsPerBar = project.tempoMap.signatures().empty() ? 4.0 : project.tempoMap.signatures().front().numerator * 4.0 / project.tempoMap.signatures().front().denominator;
    LoopSuggestion s;
    s.bars = bars;
    s.start = std::max<Ticks>(0, project.tempoMap.samplesToTicks(static_cast<double>(startFrame) * project.sampleRate / audio.sampleRate, project.sampleRate));  // the cell plays at the project's tempo
    s.length = static_cast<Ticks>(std::llround(bars * beatsPerBar * kPPQ));
    return s;
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
