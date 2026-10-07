#pragma once
#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>
#include <vector>

#include "bridge/snapshot.h"

namespace jad {

class RegionModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Role { RegionId = Qt::UserRole + 1, TrackId, TrackIndex, StartBeats, LengthBeats, IsAudio, Missing, MediaId, Color };
    explicit RegionModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    void reset(const std::vector<RegionRow>& rows);
    const RegionRow* find(const QString& regionId) const;
    Q_INVOKABLE bool hasRegion(const QString& regionId) const { return find(regionId) != nullptr; }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    std::vector<RegionRow> rows_;
};

}  // namespace jad
