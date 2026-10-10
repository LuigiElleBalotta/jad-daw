#include "bridge/region_model.h"

namespace jad {

void RegionModel::reset(const std::vector<RegionRow>& rows) {
    if (sameIds(rows_, rows)) {
        rows_ = rows;
        if (!rows_.empty()) emit dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1));
        return;
    }
    beginResetModel();
    rows_ = rows;
    endResetModel();
}

const RegionRow* RegionModel::find(const QString& regionId) const {
    for (const RegionRow& r : rows_)
        if (r.id == regionId) return &r;
    return nullptr;
}

int RegionModel::takeCount(const RegionRow& r) const {
    if (r.takeGroup.isEmpty()) return 0;
    int n = 0;
    for (const RegionRow& o : rows_)
        if (o.trackId == r.trackId && o.takeGroup == r.takeGroup) ++n;
    return n;
}

QVariant RegionModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) return {};
    const RegionRow& r = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case RegionId: return r.id;
        case TrackId: return r.trackId;
        case TrackIndex: return r.trackIndex;
        case StartBeats: return r.startBeats;
        case LengthBeats: return r.lengthBeats;
        case IsAudio: return r.audio;
        case Missing: return r.missing;
        case MediaId: return r.mediaId;
        case Color: return r.color;
        case Muted: return r.muted;
        case FadeInBeats: return r.fadeInBeats;
        case FadeOutBeats: return r.fadeOutBeats;
        case LoopBeats: return r.loopBeats;
        case Takes: return takeCount(r);
        case GainDb: return r.gainDb;
    }
    return {};
}

QHash<int, QByteArray> RegionModel::roleNames() const {
    return {{RegionId, "regionId"},       {TrackId, "trackId"},   {TrackIndex, "trackIndex"}, {StartBeats, "startBeats"},
            {LengthBeats, "lengthBeats"}, {IsAudio, "isAudio"},   {Missing, "missing"},       {MediaId, "mediaId"},       {Color, "trackColor"}, {Muted, "muted"},
            {FadeInBeats, "fadeInBeats"}, {FadeOutBeats, "fadeOutBeats"}, {LoopBeats, "loopBeats"}, {Takes, "takes"}, {GainDb, "gainDb"}};
}

}  // namespace jad
