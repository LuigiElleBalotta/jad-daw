#include "bridge/track_list_model.h"

namespace jad {

void TrackListModel::reset(const std::vector<TrackRow>& rows) {
    std::vector<std::size_t> visible;
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].showInTracks) visible.push_back(i);
    bool same = visible.size() == visible_.size();
    for (std::size_t i = 0; same && i < visible.size(); ++i) same = rows[visible[i]].id == rows_[visible_[i]].id;
    if (same) {
        rows_ = rows;
        visible_ = visible;
        if (!visible_.empty()) emit dataChanged(index(0), index(static_cast<int>(visible_.size()) - 1));
        return;
    }
    beginResetModel();
    rows_ = rows;
    visible_ = visible;
    endResetModel();
}

void TrackListModel::setToggle(const QString& trackId, Role role, bool on) {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        TrackRow& t = rows_[i];
        if (t.id != trackId) continue;
        bool& field = role == RecordArm ? t.recordArm : t.inputMonitor;
        if (field == on) return;
        field = on;
        for (std::size_t v = 0; v < visible_.size(); ++v)
            if (visible_[v] == i) emit dataChanged(index(static_cast<int>(v)), index(static_cast<int>(v)), {role});
        return;
    }
}

const TrackRow* TrackListModel::find(const QString& trackId) const {
    for (const TrackRow& t : rows_)
        if (t.id == trackId) return &t;
    return nullptr;
}

QVariant TrackListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(visible_.size())) return {};
    const TrackRow& r = rows_[visible_[static_cast<std::size_t>(index.row())]];
    switch (role) {
        case TrackId: return r.id;
        case Name: return r.name;
        case Kind: return r.kind;
        case Color: return r.color;
        case IsMaster: return r.master;
        case RegionCount: return r.regionCount;
        case Mute: return r.mute;
        case Solo: return r.solo;
        case GainDb: return r.gainDb;
        case Pan: return r.pan;
        case RecordArm: return r.recordArm;
        case InputMonitor: return r.inputMonitor;
        case Hidden: return r.hidden;
        case Frozen: return r.frozen;
    }
    return {};
}

QHash<int, QByteArray> TrackListModel::roleNames() const {
    return {{TrackId, "trackId"}, {Name, "name"}, {Kind, "kind"}, {Color, "color"}, {IsMaster, "isMaster"}, {RegionCount, "regionCount"}, {Mute, "mute"}, {Solo, "solo"}, {GainDb, "gainDb"}, {Pan, "pan"}, {RecordArm, "recordArm"}, {InputMonitor, "inputMonitor"}, {Hidden, "hidden"}, {Frozen, "frozen"}};
}

}  // namespace jad
