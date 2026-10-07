#include "bridge/track_list_model.h"

namespace jad {

void TrackListModel::reset(const std::vector<TrackRow>& rows) {
    if (sameIds(rows_, rows)) {
        rows_ = rows;
        if (!rows_.empty()) emit dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1));
        return;
    }
    beginResetModel();
    rows_ = rows;
    endResetModel();
}

const TrackRow* TrackListModel::find(const QString& trackId) const {
    for (const TrackRow& t : rows_)
        if (t.id == trackId) return &t;
    return nullptr;
}

QVariant TrackListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) return {};
    const TrackRow& r = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case TrackId: return r.id;
        case Name: return r.name;
        case Kind: return r.kind;
        case Color: return r.color;
        case IsMaster: return r.master;
        case RegionCount: return r.regionCount;
    }
    return {};
}

QHash<int, QByteArray> TrackListModel::roleNames() const {
    return {{TrackId, "trackId"}, {Name, "name"}, {Kind, "kind"}, {Color, "color"}, {IsMaster, "isMaster"}, {RegionCount, "regionCount"}};
}

}  // namespace jad
