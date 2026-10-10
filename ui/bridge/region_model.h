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
    enum Role { RegionId = Qt::UserRole + 1, TrackId, TrackIndex, StartBeats, LengthBeats, IsAudio, Missing, MediaId, Color, Muted, FadeInBeats, FadeOutBeats, LoopBeats };
    explicit RegionModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    void reset(const std::vector<RegionRow>& rows);
    const RegionRow* find(const QString& regionId) const;
    const std::vector<RegionRow>& rows() const { return rows_; }
    Q_INVOKABLE bool hasRegion(const QString& regionId) const { return find(regionId) != nullptr; }
    Q_INVOKABLE QString regionIdAt(int row) const { return row >= 0 && row < static_cast<int>(rows_.size()) ? rows_[static_cast<std::size_t>(row)].id : QString(); }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    std::vector<RegionRow> rows_;
};

}  // namespace jad
