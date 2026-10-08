#pragma once
#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>
#include <vector>

#include "bridge/snapshot.h"

namespace jad {

// Tracks in project order (the master excluded by the controller): the rows of the track list and the timeline.
class TrackListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Role { TrackId = Qt::UserRole + 1, Name, Kind, Color, IsMaster, RegionCount, Mute, Solo, GainDb, Pan };
    explicit TrackListModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    void reset(const std::vector<TrackRow>& rows);
    const TrackRow* find(const QString& trackId) const;
    Q_INVOKABLE QString trackIdAt(int row) const { return row >= 0 && row < static_cast<int>(rows_.size()) ? rows_[static_cast<std::size_t>(row)].id : QString(); }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    std::vector<TrackRow> rows_;
};

}  // namespace jad
