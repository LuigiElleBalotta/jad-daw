#pragma once
#include <QAbstractListModel>
#include <QSet>
#include <QtQml/qqmlregistration.h>
#include <vector>

#include "bridge/snapshot.h"

namespace jad {

// One strip per track, the master last.
class MixerModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Role { TrackId = Qt::UserRole + 1, Name, Color, IsMaster, Mute, Solo, GainDb, Pan, Info };
    explicit MixerModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    void reset(const std::vector<TrackRow>& rows);
    enum Toggle { RecordArm, InputMonitor, SoloSafe };
    void setToggle(const QString& trackId, Toggle which, bool on);  // R, I and solo-safe: one row, one dataChanged
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    std::vector<TrackRow> rows_;
    QSet<QString> used_;  // the ids of the buses and auxes that a send or an output points to
};

}  // namespace jad
