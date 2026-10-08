#include "bridge/inspector_model.h"

#include "bridge/row_maps.h"

namespace jad {

void InspectorModel::update(const std::vector<TrackRow>& tracks, const std::vector<RegionRow>& regions, const QStringList& selectedTracks,
                            const QStringList& selectedRegions) {
    const auto findTrack = [&](const QString& id) -> const TrackRow* {
        for (const TrackRow& t : tracks)
            if (t.id == id && !t.master) return &t;
        return nullptr;
    };
    const auto findOutput = [&](const QString& id) -> const TrackRow* {
        for (const TrackRow& t : tracks)
            if (t.id == id) return &t;
        return nullptr;
    };

    const RegionRow* shownRegion = nullptr;
    for (const QString& id : selectedRegions) {
        for (const RegionRow& r : regions)
            if (r.id == id) {
                shownRegion = &r;
                break;
            }
        if (shownRegion) break;
    }
    const TrackRow* shown = nullptr;
    for (const QString& id : selectedTracks)
        if ((shown = findTrack(id))) break;
    if (!shown && shownRegion) shown = findTrack(shownRegion->trackId);

    QVariantMap track, output, region;
    QVariantList smart, targets;
    if (shown) {
        track = trackToMap(*shown);
        if (const TrackRow* out = findOutput(shown->outputId)) output = trackToMap(*out);
        for (const SmartRow& s : shown->smart)
            smart.append(QVariantMap{{"id", s.id}, {"label", s.label}, {"group", s.group}, {"min", s.min}, {"max", s.max}, {"value", s.value}, {"def", s.def}});
    }
    if (shownRegion) {
        region = {{"regionId", shownRegion->id},
                  {"trackId", shownRegion->trackId},
                  {"trackName", shown ? shown->name : QString()},
                  {"audio", shownRegion->audio},
                  {"gainDb", shownRegion->gainDb},
                  {"startBeats", shownRegion->startBeats},
                  {"lengthBeats", shownRegion->lengthBeats}};
    }
    for (const TrackRow& t : tracks)
        if (t.kind == "bus" || t.kind == "aux") targets.append(QVariantMap{{"id", t.id}, {"name", t.name}});

    const QString id = shown ? shown->id : QString();
    const bool same = track == track_ && output == output_ && region == region_ && smart == smart_ && targets == targets_ && id == shownTrackId_;
    track_ = std::move(track);
    output_ = std::move(output);
    region_ = std::move(region);
    smart_ = std::move(smart);
    targets_ = std::move(targets);
    shownTrackId_ = id;
    shownKind_ = shown ? shown->kind : QString();
    shownPatchId_ = shown ? shown->patchId : QString();
    if (!same) emit changed();
}

}  // namespace jad
