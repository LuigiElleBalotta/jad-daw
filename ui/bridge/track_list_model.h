#pragma once
#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>
#include <vector>

#include "bridge/snapshot.h"

namespace jad {

// Tracks in project order (the master excluded by the controller). The model lists the tracks shown in the Tracks area (the
// rows of the track list and the timeline); `find` and `totalCount` also see the buses and auxes hidden from it.
class TrackListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Role { TrackId = Qt::UserRole + 1, Name, Kind, Color, IsMaster, RegionCount, Mute, Solo, GainDb, Pan, RecordArm, InputMonitor };
    explicit TrackListModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    void reset(const std::vector<TrackRow>& rows);
    void setToggle(const QString& trackId, Role role, bool on);  // RecordArm or InputMonitor: one row, one dataChanged
    const TrackRow* find(const QString& trackId) const;
    Q_INVOKABLE QString nameAt(int row) const { return row >= 0 && row < static_cast<int>(visible_.size()) ? rows_[visible_[static_cast<std::size_t>(row)]].name : QString(); }
    Q_INVOKABLE QString trackIdAt(int row) const { return row >= 0 && row < static_cast<int>(visible_.size()) ? rows_[visible_[static_cast<std::size_t>(row)]].id : QString(); }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(visible_.size()); }
    int totalCount() const { return static_cast<int>(rows_.size()); }  // shown and hidden
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    std::vector<TrackRow> rows_;         // every non-master track
    std::vector<std::size_t> visible_;   // indexes into rows_ of the rows with showInTracks
};

}  // namespace jad
