#pragma once
#include <random>
#include <string>
#include <vector>

#include "lpc/commands.h"
#include "lpc/processor_ids.h"

namespace lpc::test {

// Returns a random command that is valid or invalid depending on the current project (so rejected
// commands are exercised too), or nullptr when the chosen kind cannot be built from the current state.
inline CommandPtr randomCommand(const Project& p, std::mt19937_64& rng) {
    auto pick = [&](std::size_t n) { return static_cast<std::size_t>(rng() % n); };
    auto chance = [&](int percent) { return static_cast<int>(rng() % 100) < percent; };

    std::vector<const Track*> busLike, sources, nonMaster, withRegions, withSends;
    for (const Track& t : p.tracks) {
        if (t.kind == TrackKind::Bus || t.kind == TrackKind::Aux) busLike.push_back(&t);
        if (t.kind == TrackKind::Audio || t.kind == TrackKind::Midi || t.kind == TrackKind::Instrument) sources.push_back(&t);
        if (t.kind != TrackKind::Master) nonMaster.push_back(&t);
        if (!t.regions.empty()) withRegions.push_back(&t);
        if (!t.strip.sends.empty()) withSends.push_back(&t);
    }
    std::vector<const Track*> senders = sources;
    senders.insert(senders.end(), busLike.begin(), busLike.end());

    switch (pick(13)) {
        case 0: {  // add track
            static const TrackKind kinds[] = {TrackKind::Audio, TrackKind::Midi, TrackKind::Instrument, TrackKind::Bus, TrackKind::Aux};
            Track t;
            t.id = Uuid::random(rng);
            t.kind = kinds[pick(5)];
            t.name = "T" + std::to_string(pick(1000));
            if (t.kind == TrackKind::Instrument) t.instrument = ProcessorRef{kProcSine, {}, ""};
            if (!busLike.empty() && chance(30)) t.strip.output = busLike[pick(busLike.size())]->id;
            return makeAddTrack(std::move(t));
        }
        case 1:  // remove track (often rejected with in_use)
            if (nonMaster.empty()) return nullptr;
            return makeRemoveTrack(nonMaster[pick(nonMaster.size())]->id);
        case 2: {  // set strip
            if (nonMaster.empty()) return nullptr;
            StripPatch patch;
            if (chance(50)) patch.gainDb = static_cast<float>(static_cast<int>(pick(60)) - 40);
            if (chance(50)) patch.pan = static_cast<float>(static_cast<int>(pick(21)) - 10) / 10.0f;
            if (chance(30)) patch.mute = chance(50);
            if (chance(30)) patch.solo = chance(50);
            return makeSetStrip(nonMaster[pick(nonMaster.size())]->id, patch);
        }
        case 3:
            return makeSetTempo(static_cast<Ticks>(pick(8)) * kPPQ * 4, 60.0 + static_cast<double>(pick(120)));
        case 4:
            return makeRemoveTempo(static_cast<Ticks>(pick(8)) * kPPQ * 4);
        case 5:
            return makeAddMedia(MediaItem{Uuid::random(rng), "audio/m" + std::to_string(pick(1000)) + ".wav", "h", p.sampleRate, 2, 48000});
        case 6:
            if (p.mediaPool.empty()) return nullptr;
            return makeRemoveMedia(p.mediaPool[pick(p.mediaPool.size())].id);
        case 7: {  // add region
            if (sources.empty()) return nullptr;
            const Track& t = *sources[pick(sources.size())];
            Region r;
            r.id = Uuid::random(rng);
            r.start = static_cast<Ticks>(pick(16)) * kPPQ;
            r.length = static_cast<Ticks>(1 + pick(8)) * kPPQ;
            if (t.kind == TrackKind::Audio) {
                if (p.mediaPool.empty()) return nullptr;
                r.mediaId = p.mediaPool[pick(p.mediaPool.size())].id;
                if (chance(30)) {
                    r.timeBase = TimeBase::Absolute;
                    r.start *= 1000;
                    r.length *= 1000;
                }
            } else {
                for (std::size_t i = 0, n = pick(4); i < n; ++i)
                    r.notes.push_back({static_cast<Ticks>(pick(4)) * kPPQ / 2, kPPQ / 2,
                                       static_cast<std::uint8_t>(36 + pick(48)), static_cast<std::uint8_t>(1 + pick(127))});
            }
            return makeAddRegion(t.id, std::move(r));
        }
        case 8:
            if (withRegions.empty()) return nullptr;
            {
                const Track& t = *withRegions[pick(withRegions.size())];
                return makeRemoveRegion(t.regions[pick(t.regions.size())].id);
            }
        case 9:
            if (withRegions.empty()) return nullptr;
            {
                const Track& t = *withRegions[pick(withRegions.size())];
                return makeMoveRegion(t.regions[pick(t.regions.size())].id, static_cast<Ticks>(pick(32)) * kPPQ);
            }
        case 10: {  // add send (cycles and self-sends are rejected naturally)
            if (senders.empty() || busLike.empty()) return nullptr;
            const Track& from = *senders[pick(senders.size())];
            const Track& to = *busLike[pick(busLike.size())];
            return makeAddSend(from.id, Send{Uuid::random(rng), to.id, static_cast<float>(-static_cast<int>(pick(20))), chance(30)});
        }
        case 11:
            if (withSends.empty()) return nullptr;
            {
                const Track& t = *withSends[pick(withSends.size())];
                return makeRemoveSend(t.strip.sends[pick(t.strip.sends.size())].id);
            }
        case 12: {  // set inserts on any track, master included
            std::vector<ProcessorRef> chain;
            for (std::size_t i = 0, n = pick(3); i < n; ++i)
                chain.push_back(ProcessorRef{kProcGain, {{"gainDb", static_cast<double>(pick(12)) - 6.0}}, ""});
            return makeSetInserts(p.tracks[pick(p.tracks.size())].id, std::move(chain));
        }
        default:
            return nullptr;
    }
}

}  // namespace lpc::test
