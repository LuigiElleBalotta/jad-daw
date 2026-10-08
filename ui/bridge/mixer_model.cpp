#include "bridge/mixer_model.h"

#include <algorithm>

#include "bridge/row_maps.h"

namespace jad {

void MixerModel::reset(const std::vector<TrackRow>& rows) {
    std::vector<TrackRow> ordered = rows;
    std::stable_partition(ordered.begin(), ordered.end(), [](const TrackRow& r) { return !r.master; });  // master goes last
    if (sameIds(rows_, ordered)) {
        rows_ = std::move(ordered);
        if (!rows_.empty()) emit dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1));
        return;
    }
    beginResetModel();
    rows_ = std::move(ordered);
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
        case Info: return trackToMap(r);
    }
    return {};
}

QHash<int, QByteArray> MixerModel::roleNames() const {
    return {{TrackId, "trackId"}, {Name, "name"}, {Color, "color"}, {IsMaster, "isMaster"},
            {Mute, "mute"},       {Solo, "solo"}, {GainDb, "gainDb"}, {Pan, "pan"}, {Info, "info"}};
}

}  // namespace jad
