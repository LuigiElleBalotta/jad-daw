#include <QtTest>
#include <algorithm>

#include "bridge/mixer_model.h"
#include "bridge/region_model.h"
#include "bridge/row_maps.h"
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
    void snapshotCarriesTheStripDetailsOfEachTrack() {
        TempDir dir;
        const lpc::Project p = lpc::makeDemoProject(dir.path());
        const jad::Snapshot s = jad::makeSnapshot(p, 1, [](const lpc::MediaItem&) { return true; });
        const auto keys = std::find_if(s.tracks.begin(), s.tracks.end(), [](const jad::TrackRow& t) { return t.kind == "instrument"; });
        QVERIFY(keys != s.tracks.end());
        QCOMPARE(keys->instrument, QStringLiteral("builtin.sine"));
        QCOMPARE(int(keys->sends.size()), 1);
        QCOMPARE(keys->sends[0].targetName, QStringLiteral("Reverb Bus"));
        QVERIFY(!keys->outputId.isEmpty());  // the master
        QVERIFY(keys->patchId.isEmpty());
        QVERIFY(keys->smart.empty());
        const auto master = std::find_if(s.tracks.begin(), s.tracks.end(), [](const jad::TrackRow& t) { return t.master; });
        QVERIFY(master != s.tracks.end());
        QVERIFY(master->outputId.isEmpty());
        QVERIFY(!s.regions.empty());
        QCOMPARE(s.regions.front().gainDb, 0.0);
    }
    void sameRowsAreUpdatedInPlace() {
        // a model reset rebuilds every delegate (and drops a drag in progress): same ids must only emit dataChanged
        auto track = [](const char* id, double gain) { jad::TrackRow t; t.id = id; t.name = id; t.kind = "audio"; t.gainDb = gain; return t; };
        auto region = [](const char* id, double start) { jad::RegionRow r; r.id = id; r.startBeats = start; r.lengthBeats = 1; return r; };

        jad::MixerModel mixer;
        mixer.reset({track("a", 0), track("b", 0)});
        QSignalSpy mixerReset(&mixer, &QAbstractItemModel::modelReset), mixerChanged(&mixer, &QAbstractItemModel::dataChanged);
        mixer.reset({track("a", -3), track("b", 0)});
        QCOMPARE(mixerReset.count(), 0);
        QCOMPARE(mixerChanged.count(), 1);
        QCOMPARE(mixer.data(mixer.index(0), mixer.roleNames().key("gainDb")).toDouble(), -3.0);
        mixer.reset({track("a", -3), track("c", 0)});  // a different id: rebuilt
        QCOMPARE(mixerReset.count(), 1);

        jad::TrackListModel tracks;
        tracks.reset({track("a", 0)});
        QSignalSpy tracksReset(&tracks, &QAbstractItemModel::modelReset);
        tracks.reset({track("a", 1)});
        QCOMPARE(tracksReset.count(), 0);
        tracks.reset({});
        QCOMPARE(tracksReset.count(), 1);

        jad::RegionModel regions;
        regions.reset({region("r1", 0), region("r2", 4)});
        QSignalSpy regionsReset(&regions, &QAbstractItemModel::modelReset);
        regions.reset({region("r1", 2), region("r2", 4)});
        QCOMPARE(regionsReset.count(), 0);
        QCOMPARE(regions.data(regions.index(0), regions.roleNames().key("startBeats")).toDouble(), 2.0);
        regions.reset({region("r1", 2)});
        QCOMPARE(regionsReset.count(), 1);
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
    void mixerRowsCarryTheStripViewForQml() {
        jad::MixerModel model;
        jad::TrackRow a;
        a.id = "a";
        a.kind = "audio";
        a.name = "Vox";
        a.outputName = "Stereo Out";
        a.inserts.push_back({"builtin.gain", 3.0});
        jad::TrackRow m;
        m.id = "m";
        m.kind = "master";
        m.master = true;
        model.reset({m, a});
        const int role = model.roleNames().key("info");
        QVERIFY(role != 0);
        const QVariantMap info = model.data(model.index(0), role).toMap();  // the master goes last: row 0 is the track
        QCOMPARE(info.value("name").toString(), QStringLiteral("Vox"));
        QCOMPARE(info.value("inserts").toList().size(), 1);
        QVERIFY(model.data(model.index(1), role).toMap().value("master").toBool());
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
