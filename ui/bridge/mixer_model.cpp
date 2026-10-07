#include "bridge/mixer_model.h"

#include <algorithm>

namespace jad {

void MixerModel::reset(const std::vector<TrackRow>& rows) {
    beginResetModel();
    rows_ = rows;
    std::stable_partition(rows_.begin(), rows_.end(), [](const TrackRow& r) { return !r.master; });  // master goes last
    endResetModel();
}

QVariant MixerModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) return {};
    const TrackRow& r = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case TrackId: return r.id;
        case Name: return r.name;
        case Color: return r.color;
        case IsMaster: return r.master;
        case Mute: return r.mute;
        case Solo: return r.solo;
        case GainDb: return r.gainDb;
        case Pan: return r.pan;
    }
    return {};
}

QHash<int, QByteArray> MixerModel::roleNames() const {
    return {{TrackId, "trackId"}, {Name, "name"}, {Color, "color"}, {IsMaster, "isMaster"},
            {Mute, "mute"},       {Solo, "solo"}, {GainDb, "gainDb"}, {Pan, "pan"}};
}

}  // namespace jad
