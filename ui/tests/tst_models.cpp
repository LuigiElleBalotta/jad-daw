#include <QtTest>
#include <algorithm>

#include "bridge/region_model.h"
#include "bridge/snapshot.h"
#include "bridge/track_list_model.h"
#include "lpc/demo_project.h"
#include "tests_temp.h"

class ModelsTest : public QObject {
    Q_OBJECT
private slots:
    void snapshotOfDemoProject() {
        TempDir dir;
        const lpc::Project p = lpc::makeDemoProject(dir.path());
        const jad::Snapshot s = jad::makeSnapshot(p, 7, [](const lpc::MediaItem&) { return true; });
        QCOMPARE(s.revision, quint64(7));
        QCOMPARE(s.name, QStringLiteral("Demo"));
        QCOMPARE(s.bpm, 120.0);
        QCOMPARE(int(s.tracks.size()), int(p.tracks.size()));
        QVERIFY(std::any_of(s.tracks.begin(), s.tracks.end(), [](const jad::TrackRow& t) { return t.master; }));
        const auto audio = std::find_if(s.regions.begin(), s.regions.end(), [](const jad::RegionRow& r) { return r.audio; });
        QVERIFY(audio != s.regions.end());
        QVERIFY(audio->lengthBeats > 0.0);
    }
    void missingMediaIsFlagged() {
        TempDir dir;
        const lpc::Project p = lpc::makeDemoProject(dir.path());
        const jad::Snapshot s = jad::makeSnapshot(p, 1, [](const lpc::MediaItem&) { return false; });
        const auto audio = std::find_if(s.regions.begin(), s.regions.end(), [](const jad::RegionRow& r) { return r.audio; });
        QVERIFY(audio != s.regions.end());
        QVERIFY(audio->missing);
    }
    void modelsResetAndExposeRoles() {
        jad::TrackListModel model;
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        jad::TrackRow row;
        row.id = "id1";
        row.name = "Keys";
        row.kind = "instrument";
        row.color = "purple";
        row.regionCount = 2;
        model.reset({row});
        QCOMPARE(reset.count(), 1);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), model.roleNames().key("name")).toString(), QStringLiteral("Keys"));
        QCOMPARE(model.data(model.index(0), model.roleNames().key("regionCount")).toInt(), 2);
    }
    void regionModelExposesBeats() {
        jad::RegionModel model;
        jad::RegionRow r;
        r.id = "r1";
        r.startBeats = 4.0;
        r.lengthBeats = 2.5;
        model.reset({r});
        QCOMPARE(model.data(model.index(0), model.roleNames().key("startBeats")).toDouble(), 4.0);
        QCOMPARE(model.data(model.index(0), model.roleNames().key("lengthBeats")).toDouble(), 2.5);
    }
};

QTEST_MAIN(ModelsTest)
#include "tst_models.moc"
