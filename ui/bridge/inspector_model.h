#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <vector>

#include "bridge/snapshot.h"

namespace jad {

// What the Inspector, the Library and the Smart Controls show: the track the selection points at (the first selected
// track, else the track of the first selected region), its output, the region, and the buses a send can go to.
class InspectorModel : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY changed)
    Q_PROPERTY(bool hasRegion READ hasRegion NOTIFY changed)
    Q_PROPERTY(QVariantMap track READ track NOTIFY changed)
    Q_PROPERTY(QVariantMap output READ output NOTIFY changed)
    Q_PROPERTY(QVariantMap region READ region NOTIFY changed)
    Q_PROPERTY(QVariantList smartControls READ smartControls NOTIFY changed)
    Q_PROPERTY(QVariantList busTargets READ busTargets NOTIFY changed)
public:
    explicit InspectorModel(QObject* parent = nullptr) : QObject(parent) {}
    // `tracks` holds every track, the master included. Ids that do not exist are ignored.
    void update(const std::vector<TrackRow>& tracks, const std::vector<RegionRow>& regions, const QStringList& selectedTracks,
                const QStringList& selectedRegions);
    bool hasTrack() const { return !shownTrackId_.isEmpty(); }
    bool hasRegion() const { return !region_.isEmpty(); }
    QVariantMap track() const { return track_; }
    QVariantMap output() const { return output_; }
    QVariantMap region() const { return region_; }
    QVariantList smartControls() const { return smart_; }
    QVariantList busTargets() const { return targets_; }
    QString trackId() const { return shownTrackId_; }
    QString shownKind() const { return shownKind_; }
    QString shownPatchId() const { return shownPatchId_; }

signals:
    void changed();

private:
    QVariantMap track_, output_, region_;
    QVariantList smart_, targets_;
    QString shownTrackId_, shownKind_, shownPatchId_;
};

}  // namespace jad
