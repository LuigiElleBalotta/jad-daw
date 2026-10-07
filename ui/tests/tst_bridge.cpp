#include <QSignalSpy>
#include <QtTest>
#include <limits>

#include "bridge/project_controller.h"
#include "lpc/demo_project.h"
#include "lpc/project_io.h"
#include "lpc/wav.h"
#include "tests_temp.h"

namespace {

QUrl url(const std::filesystem::path& p) { return QUrl::fromLocalFile(QString::fromStdU16String(p.u16string())); }

std::filesystem::path makeDemo(const TempDir& dir, const char8_t* name = u8"d.lpc") {
    const auto proj = dir.path() / std::filesystem::path(name);
    std::filesystem::create_directories(proj);
    lpc::saveProject(lpc::makeDemoProject(proj), proj);
    return proj;
}

}  // namespace

class BridgeTest : public QObject {
    Q_OBJECT
private slots:
    void opensTheDemoProject() {
        TempDir dir;
        jad::ProjectController c(/*openAudioDevice=*/false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QVERIFY(c.hasProject());
        QTRY_COMPARE(c.projectName(), QStringLiteral("Demo"));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        QVERIFY(c.regions()->rowCount() > 0);
    }
    void rejectedCommandKeepsModelsAndSetsError() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const int before = c.tracks()->rowCount();
        c.submit(R"({"type":"remove_track","trackId":"00000000-0000-0000-0000-0000000000ff"})");
        QTRY_VERIFY(c.lastError().startsWith("not_found"));
        QCOMPARE(c.tracks()->rowCount(), before);
    }
    void undoRedoRoundTrip() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const int n = c.tracks()->rowCount();
        c.submit(R"({"type":"add_track","index":-1,"track":{"id":"00000000-0000-0000-0000-0000000000aa","kind":"audio","name":"New","color":"","strip":{"gainDb":0,"pan":0,"mute":false,"solo":false,"inserts":[],"sends":[],"output":"00000000-0000-0000-0000-000000000000"},"regions":[],"automation":[],"instrument":null}})");
        QTRY_COMPARE(c.tracks()->rowCount(), n + 1);
        c.undo();
        QTRY_COMPARE(c.tracks()->rowCount(), n);
        c.redo();
        QTRY_COMPARE(c.tracks()->rowCount(), n + 1);
    }
    void waveformPeaksArriveAsynchronously() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        QString mediaId;
        const auto roles = c.regions()->roleNames();
        for (int i = 0; i < c.regions()->rowCount() && mediaId.isEmpty(); ++i)
            mediaId = c.regions()->data(c.regions()->index(i), roles.key("mediaId")).toString();
        QVERIFY(!mediaId.isEmpty());
        QSignalSpy ready(&c, &jad::ProjectController::waveformReady);
        QVERIFY(c.waveformPeaks(mediaId, 64).isEmpty());  // computed on a worker
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(c.waveformPeaks(mediaId, 64).size(), 64);
        QVERIFY(c.waveformPeaks("no-such-media", 64).isEmpty());
        QVERIFY(c.waveformPeaks(mediaId, 0).isEmpty());
    }
    void invalidFolderKeepsPreviousProject() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QVERIFY(!c.openProject(url(dir.path() / std::filesystem::path(u8"not a project è"))));
        QVERIFY(!c.lastError().isEmpty());
        QVERIFY(c.hasProject());
        QTRY_COMPARE(c.projectName(), QStringLiteral("Demo"));
    }
    void nonAsciiPathOpens() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir, u8"Mia canzone è 曲.lpc"))));
        QVERIFY(c.hasProject());
    }
    void staleSnapshotsAreDropped() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        jad::Snapshot newer;
        newer.revision = 1000;
        newer.name = "Newer";
        jad::Snapshot older;
        older.revision = 5;
        older.name = "Older";
        c.applySnapshotForTest(newer);
        c.applySnapshotForTest(older);
        QCOMPARE(c.projectName(), QStringLiteral("Newer"));
    }
    void playWithoutDeviceReportsAnError() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        c.play();
        QVERIFY(c.lastError().startsWith("No audio output"));
        QVERIFY(!c.deviceError().isEmpty());
    }
    void saveWritesTheProjectBack() {
        TempDir dir;
        jad::ProjectController c(false);
        const auto proj = makeDemo(dir);
        QVERIFY(c.openProject(url(proj)));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        c.submit(R"({"type":"set_tempo","tick":0,"bpm":90})");
        QTRY_COMPARE(c.bpm(), 90.0);
        QVERIFY(c.saveProject());
        QCOMPARE(lpc::loadProject(proj).tempoMap.tempos().front().bpm, 90.0);
    }
};

QTEST_MAIN(BridgeTest)
#include "tst_bridge.moc"
