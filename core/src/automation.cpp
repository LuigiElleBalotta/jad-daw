#include "lpc/automation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>

namespace lpc {

double automationValueAt(const std::vector<AutomationPoint>& pts, Ticks tick, double fallback) {
    if (pts.empty()) return fallback;
    if (tick <= pts.front().tick) return pts.front().value;
    if (tick >= pts.back().tick) return pts.back().value;
    std::size_t i = 0;
    while (i + 1 < pts.size() && pts[i + 1].tick <= tick) ++i;
    const AutomationPoint& a = pts[i];
    const AutomationPoint& b = pts[i + 1];
    double t = static_cast<double>(tick - a.tick) / static_cast<double>(std::max<Ticks>(1, b.tick - a.tick));
    if (a.curve != 0.0) t = std::pow(t, std::exp2(2.0 * a.curve));
    return a.value + (b.value - a.value) * t;
}

std::vector<AutomationPoint> cutAutomation(const std::vector<AutomationPoint>& pts, Ticks from, Ticks to) {
    std::vector<AutomationPoint> out;
    if (pts.empty() || to <= from) return out;
    auto curveAt = [&](Ticks tick) {  // the bend of the segment that holds the tick
        double c = 0.0;
        for (const AutomationPoint& p : pts)
            if (p.tick <= tick) c = p.curve;
        return c;
    };
    out.push_back(AutomationPoint{0, automationValueAt(pts, from, 0.0), curveAt(from)});
    for (const AutomationPoint& p : pts)
        if (p.tick > from && p.tick < to) out.push_back(AutomationPoint{p.tick - from, p.value, p.curve});
    out.push_back(AutomationPoint{to - from, automationValueAt(pts, to, 0.0), 0.0});
    return out;
}


namespace {
// A region's own position in ticks, for the lanes of a region are counted from its start.
Ticks regionTick(const Project& p, const Region& r, std::int64_t value) {
    if (r.timeBase == TimeBase::Musical) return value;
    return p.tempoMap.samplesToTicks(static_cast<double>(value) * p.sampleRate / 1e6, p.sampleRate);
}

// What a lane holds when nothing automates it: the fader, the pan, the level of a send; no Relative offset; unknown for a plug-in parameter (NaN).
double laneBase(const Track& t, const std::string& target) {
    constexpr double unknown = std::numeric_limits<double>::quiet_NaN();
    if (target == "volume") return t.strip.gainDb;
    if (target == "pan") return t.strip.pan;
    if (target == "volume.rel" || target == "pan.rel") return 0.0;
    if (target.rfind("send:", 0) == 0)
        if (const auto id = Uuid::parse(target.substr(5)))
            for (const Send& s : t.strip.sends)
                if (s.id == *id) return s.levelDb;
    return unknown;
}

}  // namespace

// The lanes a track plays: its own, with the lanes of its regions laid over them (Region automation). Inside a region that has a lane for a target the region's
// lane drives the parameter and the track's lane takes over again at its end; the jump is made one tick wide. Project settings: regionAutomationPriority (default
// on; off keeps the track's lane wherever it has points) and presetValueForRegions (a region without a lane for a target some other region of the track automates
// holds the parameter's own value; with it off, and no lane of the track, the last value of the region before it holds).
std::vector<AutomationLane> overlayRegionLanes(const Project& p, const Track& t, bool priority, bool preset) {
    std::vector<AutomationLane> out = t.automation;
    std::set<std::string> targets;
    for (const Region& r : t.regions)
        if (!r.muted)
            for (const AutomationLane& lane : r.automation)
                if (!lane.points.empty()) targets.insert(lane.target);
    if (targets.empty()) return out;
    std::vector<const Region*> ordered;
    for (const Region& r : t.regions)
        if (!r.muted) ordered.push_back(&r);
    std::stable_sort(ordered.begin(), ordered.end(), [](const Region* a, const Region* b) { return a->start < b->start; });
    for (const std::string& target : targets) {
        auto own = std::find_if(out.begin(), out.end(), [&](const AutomationLane& l) { return l.target == target; });
        if (!priority && own != out.end() && !own->points.empty()) continue;
        const std::vector<AutomationPoint> trackPoints = own != out.end() ? own->points : std::vector<AutomationPoint>{};
        const double base = laneBase(t, target);
        const bool trackLane = !trackPoints.empty();
        double held = base;  // without a lane of the track: what the parameter holds between the regions (its own value until a region with a lane has played)
        bool haveHeld = !std::isnan(base);
        const auto outside = [&](Ticks tick) {
            if (trackLane) return automationValueAt(trackPoints, tick, base);
            return preset && !std::isnan(base) ? base : held;
        };
        struct Span {
            Ticks from, to;
            const AutomationLane* lane;
        };
        std::vector<Span> spans;
        for (const Region* r : ordered) {
            const AutomationLane* lane = nullptr;
            for (const AutomationLane& l : r->automation)
                if (l.target == target && !l.points.empty()) lane = &l;
            if (!lane && !(preset && !std::isnan(base))) continue;
            spans.push_back({regionTick(p, *r, r->start), regionTick(p, *r, r->start + r->length), lane});
        }
        for (std::size_t i = 0; i + 1 < spans.size(); ++i) spans[i].to = std::min(spans[i].to, spans[i + 1].from);  // the later region wins where they overlap
        std::vector<AutomationPoint> pts = trackPoints;
        for (const Span& s : spans) {
            if (s.to <= s.from) continue;
            pts.erase(std::remove_if(pts.begin(), pts.end(), [&](const AutomationPoint& pt) { return pt.tick >= s.from && pt.tick < s.to; }), pts.end());
            const bool haveOutside = trackLane || haveHeld;
            if (haveOutside && s.from > 0) pts.push_back(AutomationPoint{s.from - 1, outside(s.from), 0.0});
            if (s.lane) {
                for (const AutomationPoint& pt : cutAutomation(s.lane->points, 0, s.to - s.from)) {
                    const Ticks tick = pt.tick >= s.to - s.from ? std::max(s.from, s.to - 1) : s.from + pt.tick;
                    pts.push_back(AutomationPoint{tick, pt.value, pt.curve});
                    held = pt.value;
                    haveHeld = true;
                }
            } else {
                pts.push_back(AutomationPoint{s.from, base, 0.0});
                pts.push_back(AutomationPoint{std::max(s.from, s.to - 1), base, 0.0});
                held = base;
            }
            if (trackLane || haveHeld) pts.push_back(AutomationPoint{s.to, outside(s.to), 0.0});
        }
        std::stable_sort(pts.begin(), pts.end(), [](const AutomationPoint& a, const AutomationPoint& b) { return a.tick < b.tick; });
        if (own != out.end()) {
            own->points = std::move(pts);
        } else {
            AutomationLane lane;
            lane.target = target;
            lane.points = std::move(pts);
            out.push_back(std::move(lane));
        }
    }
    return out;
}

std::vector<AutomationLane> playingLanes(const Project& p, const Track& t) {
    const auto flag = [&](const char* key, bool fallback) {
        const auto it = p.settings.find(key);
        return it == p.settings.end() ? fallback : it->second == "1" || it->second == "true";
    };
    return overlayRegionLanes(p, t, flag("regionAutomationPriority", true), flag("presetValueForRegions", false));
}

}  // namespace lpc
