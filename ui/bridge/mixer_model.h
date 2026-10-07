#pragma once
#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>
#include <vector>

#include "bridge/snapshot.h"

namespace jad {

// One strip per track, the master last.
class MixerModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Role { TrackId = Qt::UserRole + 1, Name, Color, IsMaster, Mute, Solo, GainDb, Pan };
    explicit MixerModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    void reset(const std::vector<TrackRow>& rows);
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    std::vector<TrackRow> rows_;
};

}  // namespace jad
