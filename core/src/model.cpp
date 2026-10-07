#include "lpc/model.h"

#include <algorithm>

namespace lpc {

Project::Project() : Project(Uuid::random()) {}

Project::Project(Uuid masterId) {
    Track m;
    m.id = masterId;
    m.kind = TrackKind::Master;
    m.name = "Master";
    tracks.push_back(std::move(m));
}

Track* Project::findTrack(const Uuid& id) {
    auto it = std::find_if(tracks.begin(), tracks.end(), [&](const Track& t) { return t.id == id; });
    return it == tracks.end() ? nullptr : &*it;
}

const Track* Project::findTrack(const Uuid& id) const { return const_cast<Project*>(this)->findTrack(id); }

Track* Project::findTrackOfRegion(const Uuid& regionId, std::size_t* regionIndex) {
    for (Track& t : tracks) {
        for (std::size_t i = 0; i < t.regions.size(); ++i) {
            if (t.regions[i].id == regionId) {
                if (regionIndex) *regionIndex = i;
                return &t;
            }
        }
    }
    return nullptr;
}

const MediaItem* Project::findMedia(const Uuid& id) const {
    auto it = std::find_if(mediaPool.begin(), mediaPool.end(), [&](const MediaItem& m) { return m.id == id; });
    return it == mediaPool.end() ? nullptr : &*it;
}

const Track* Project::master() const {
    auto it = std::find_if(tracks.begin(), tracks.end(), [](const Track& t) { return t.kind == TrackKind::Master; });
    return it == tracks.end() ? nullptr : &*it;
}

}  // namespace lpc
