#pragma once
#include <vector>

#include "lpc/model.h"

namespace lpc {

// The value of an automation lane at a tick: the first value holds before the first point and the last one after the last; a bent segment runs along t^(4^curve).
// `fallback` is the value of a lane without points.
double automationValueAt(const std::vector<AutomationPoint>& points, Ticks tick, double fallback);

// The part of a lane between two ticks (from may be negative, to may lie beyond the last point), counted from `from`: the points inside, plus a point at 0 and one at
// to - from that carry the value the lane has there. Used when a region is split, trimmed or joined, so its lanes keep sounding the same.
std::vector<AutomationPoint> cutAutomation(const std::vector<AutomationPoint>& points, Ticks from, Ticks to);

// The lanes a track plays: its own, with the lanes of its regions laid over them (Region automation). Inside a region that has a lane for a target the region's lane
// drives the parameter and the track's lane takes over again at its end. Project settings: regionAutomationPriority (default on; off keeps the track's lane wherever it
// has points) and presetValueForRegions (a region without a lane holds the parameter's own value; off, and no lane of the track, the last value before it holds).
std::vector<AutomationLane> playingLanes(const Project& p, const Track& t);
// The same with the two choices given.
std::vector<AutomationLane> overlayRegionLanes(const Project& p, const Track& t, bool priority, bool preset);

}  // namespace lpc
