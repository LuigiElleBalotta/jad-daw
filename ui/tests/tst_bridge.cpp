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
private:
    static QString firstAudioTrackId(jad::ProjectController& c) {
        const auto roles = c.tracks()->roleNames();
        for (int i = 0; i < c.tracks()->rowCount(); ++i)
            if (c.tracks()->data(c.tracks()->index(i), roles.key("kind")).toString() == "audio")
                return c.tracks()->data(c.tracks()->index(i), roles.key("trackId")).toString();
        return {};
    }
    static int countFiles(const std::filesystem::path& dir) {
        if (!std::filesystem::exists(dir)) return 0;
        int n = 0;
        for (const auto& e : std::filesystem::directory_iterator(dir)) { (void)e; ++n; }
        return n;
    }
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
    void moveRegionClampsAndSendsOneCommand() {
        TempDir dir; const auto proj = dir.path() / "d.lpc";
        std::filesystem::create_directories(proj); lpc::saveProject(lpc::makeDemoProject(proj), proj);
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(proj)));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const QString id = c.regions()->data(c.regions()->index(0), c.regions()->roleNames().key("regionId")).toString();
        QSignalSpy sent(&c, &jad::ProjectController::commandSent);
        c.moveRegion(id, -50.0);                       // negative: clamped to 0
        QTRY_COMPARE(sent.count(), 1);
        c.moveRegion(id, std::numeric_limits<double>::quiet_NaN());   // NaN: ignored, no command
        c.moveRegion(id, 1e30);                        // huge: clamped, accepted or rejected, never crashes
        QTRY_VERIFY(sent.count() >= 2);
    }
    void deleteRegionsIsOneTransaction() {
        TempDir dir; const auto proj = dir.path() / "d.lpc";
        std::filesystem::create_directories(proj); lpc::saveProject(lpc::makeDemoProject(proj), proj);
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(proj)));
        QTRY_VERIFY(c.regions()->rowCount() >= 2);
        const int n = c.regions()->rowCount();
        QStringList ids;
        for (int i = 0; i < 2; ++i) ids << c.regions()->data(c.regions()->index(i), c.regions()->roleNames().key("regionId")).toString();
        QSignalSpy sent(&c, &jad::ProjectController::commandSent);
        c.deleteRegions(ids);
        QTRY_COMPARE(c.regions()->rowCount(), n - 2);
        QCOMPARE(sent.count(), 1);
        c.undo();
        QTRY_COMPARE(c.regions()->rowCount(), n);      // one undo step restores both
    }
    void deleteRegionsWithEmptyListSendsNothing() {
        TempDir dir; const auto proj = dir.path() / "d.lpc";
        std::filesystem::create_directories(proj); lpc::saveProject(lpc::makeDemoProject(proj), proj);
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(proj)));
        QSignalSpy sent(&c, &jad::ProjectController::commandSent);
        c.deleteRegions({});
        QCOMPARE(sent.count(), 0);
    }
    void importAudioRejectsWrongRateAndLeavesNoFile() {
        TempDir dir; const auto proj = dir.path() / "d.lpc";
        std::filesystem::create_directories(proj); lpc::saveProject(lpc::makeDemoProject(proj), proj);
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(proj)));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        lpc::writeWav(dir.path() / "x44.wav", 44100, 2, std::vector<float>(2 * 4410, 0.1f), lpc::WavFormat::Float32);
        const QString audioTrack = firstAudioTrackId(c);
        const auto before = countFiles(proj / "audio");
        c.importAudio(url(dir.path() / "x44.wav"), audioTrack, 0.0);
        QTRY_VERIFY(c.lastError().contains("48000"));
        QCOMPARE(countFiles(proj / "audio"), before);
    }
    void importAudioOnANonAudioTrackIsRefused() {
        TempDir dir; const auto proj = dir.path() / "d.lpc";
        std::filesystem::create_directories(proj); lpc::saveProject(lpc::makeDemoProject(proj), proj);
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(proj)));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        lpc::writeWav(dir.path() / "ok.wav", 48000, 2, std::vector<float>(2 * 480, 0.1f), lpc::WavFormat::Float32);
        const auto before = countFiles(proj / "audio");
        c.importAudio(url(dir.path() / "ok.wav"), QStringLiteral("00000000-0000-0000-0000-0000000000ff"), 0.0);
        QTRY_VERIFY(!c.lastError().isEmpty());
        QCOMPARE(countFiles(proj / "audio"), before);
    }
    void importAudioAddsMediaAndRegionInOneStep() {
        TempDir dir; const auto proj = dir.path() / "d.lpc";
        std::filesystem::create_directories(proj); lpc::saveProject(lpc::makeDemoProject(proj), proj);
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(proj)));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const int n = c.regions()->rowCount();
        lpc::writeWav(dir.path() / "ok.wav", 48000, 2, std::vector<float>(2 * 48000, 0.1f), lpc::WavFormat::Float32);
        c.importAudio(url(dir.path() / "ok.wav"), firstAudioTrackId(c), 4.0);
        QTRY_COMPARE(c.regions()->rowCount(), n + 1);
        c.undo();
        QTRY_COMPARE(c.regions()->rowCount(), n);
    }
    void importAudioMakesTheFileNameUnique() {
        TempDir dir; const auto proj = dir.path() / "d.lpc";
        std::filesystem::create_directories(proj); lpc::saveProject(lpc::makeDemoProject(proj), proj);
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(proj)));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        lpc::writeWav(dir.path() / "tone.wav", 48000, 2, std::vector<float>(2 * 480, 0.1f), lpc::WavFormat::Float32);  // the demo already has audio/tone.wav
        const auto before = countFiles(proj / "audio");
        c.importAudio(url(dir.path() / "tone.wav"), firstAudioTrackId(c), 0.0);
        QTRY_COMPARE(countFiles(proj / "audio"), before + 1);
        QVERIFY(std::filesystem::exists(proj / "audio" / "tone (2).wav"));
    }
    void setGainClampsAndIgnoresNaN() {
        TempDir dir; const auto proj = dir.path() / "d.lpc";
        std::filesystem::create_directories(proj); lpc::saveProject(lpc::makeDemoProject(proj), proj);
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(proj)));
        QTRY_VERIFY(c.mixer()->rowCount() >= 3);
        const QString id = c.mixer()->data(c.mixer()->index(0), c.mixer()->roleNames().key("trackId")).toString();
        QSignalSpy sent(&c, &jad::ProjectController::commandSent);
        c.setGain(id, std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(sent.count(), 0);
        c.setGain(id, 1000.0);
        QTRY_COMPARE(sent.count(), 1);
        QTRY_COMPARE(c.mixer()->data(c.mixer()->index(0), c.mixer()->roleNames().key("gainDb")).toDouble(), 24.0);
        c.setPan(id, -5.0);
        QTRY_COMPARE(c.mixer()->data(c.mixer()->index(0), c.mixer()->roleNames().key("pan")).toDouble(), -1.0);
        c.setMute(id, true);
        QTRY_VERIFY(c.mixer()->data(c.mixer()->index(0), c.mixer()->roleNames().key("mute")).toBool());
        c.setSolo(id, true);
        QTRY_VERIFY(c.mixer()->data(c.mixer()->index(0), c.mixer()->roleNames().key("solo")).toBool());
    }
    void toggleMuteAndSoloFlipTheCurrentState() {
        TempDir dir; const auto proj = dir.path() / "d.lpc";
        std::filesystem::create_directories(proj); lpc::saveProject(lpc::makeDemoProject(proj), proj);
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(proj)));
        QTRY_VERIFY(c.tracks()->rowCount() >= 2);
        const auto roles = c.tracks()->roleNames();
        const QString id = c.tracks()->data(c.tracks()->index(0), roles.key("trackId")).toString();
        const auto muted = [&] { return c.mixer()->data(c.mixer()->index(0), c.mixer()->roleNames().key("mute")).toBool(); };
        QVERIFY(!muted());
        c.toggleMute(id);
        QTRY_VERIFY(muted());
        c.toggleMute(id);
        QTRY_VERIFY(!muted());
        QSignalSpy sent(&c, &jad::ProjectController::commandSent);
        c.toggleSolo("no-such-track");  // unknown track: nothing sent
        QCOMPARE(sent.count(), 0);
    }
    void snapshotFromAPreviousProjectIsDropped() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir, u8"a.lpc"))));
        const auto oldGeneration = c.generationForTest();
        QVERIFY(c.openProject(url(makeDemo(dir, u8"b.lpc"))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        jad::Snapshot stale;
        stale.revision = 500;
        stale.name = "StaleFromA";
        c.applySnapshotForTest(stale, oldGeneration);        // arrives late from the project that was replaced
        QCOMPARE(c.projectName(), QStringLiteral("Demo"));
        jad::Snapshot fresh;                                    // and it must not push the shown revision up
        fresh.revision = 1;
        fresh.name = "Fresh";
        c.applySnapshotForTest(fresh, c.generationForTest());
        QCOMPARE(c.projectName(), QStringLiteral("Fresh"));
    }
    void newProjectRefusesAnExistingProject() {
        TempDir dir;
        jad::ProjectController c(false);
        const auto proj = makeDemo(dir);
        const auto before = std::filesystem::file_size(proj / "project.json");
        QVERIFY(!c.newProject(url(proj)));
        QVERIFY(!c.lastError().isEmpty());
        QVERIFY(!c.hasProject());
        QCOMPARE(std::filesystem::file_size(proj / "project.json"), before);
        QVERIFY(!std::filesystem::exists(proj / "project.json.bak"));
    }
    void playReportsAStalledEngine() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        c.forceDegradedForTest(true);
        c.play();
        QVERIFY2(c.lastError().contains("not running"), qPrintable(c.lastError()));
    }
    void importAudioFilesLaysTheClipsBackToBack() {
        TempDir dir; const auto proj = dir.path() / "d.lpc";
        std::filesystem::create_directories(proj); lpc::saveProject(lpc::makeDemoProject(proj), proj);
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(proj)));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const int n = c.regions()->rowCount();
        lpc::writeWav(dir.path() / "one.wav", 48000, 2, std::vector<float>(2 * 48000, 0.1f), lpc::WavFormat::Float32);   // 1 s = 2 beats at 120 bpm
        lpc::writeWav(dir.path() / "two.wav", 48000, 2, std::vector<float>(2 * 48000, 0.1f), lpc::WavFormat::Float32);
        c.importAudioFiles({url(dir.path() / "one.wav"), url(dir.path() / "two.wav")}, firstAudioTrackId(c), 4.0);
        QTRY_COMPARE(c.regions()->rowCount(), n + 2);
        QList<double> starts;
        const auto roles = c.regions()->roleNames();
        for (int i = 0; i < c.regions()->rowCount(); ++i)
            starts << c.regions()->data(c.regions()->index(i), roles.key("startBeats")).toDouble();
        const auto has = [&](double v) { return std::any_of(starts.begin(), starts.end(), [v](double s) { return qAbs(s - v) < 0.01; }); };
        QVERIFY(has(4.0));
        QVERIFY(has(6.0));  // the second one starts where the first one ends
    }
    void importAudioFilesKeepsGoingAfterABadFile() {
        TempDir dir; const auto proj = dir.path() / "d.lpc";
        std::filesystem::create_directories(proj); lpc::saveProject(lpc::makeDemoProject(proj), proj);
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(proj)));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const int n = c.regions()->rowCount();
        lpc::writeWav(dir.path() / "bad44.wav", 44100, 2, std::vector<float>(2 * 441, 0.1f), lpc::WavFormat::Float32);
        lpc::writeWav(dir.path() / "good.wav", 48000, 2, std::vector<float>(2 * 4800, 0.1f), lpc::WavFormat::Float32);
        c.importAudioFiles({url(dir.path() / "bad44.wav"), url(dir.path() / "good.wav")}, firstAudioTrackId(c), 0.0);
        QTRY_COMPARE(c.regions()->rowCount(), n + 1);
        QVERIFY(c.lastError().contains("44100"));
    }
    void openingAnInvalidProjectEmitsTheLoaderMessage() {
        TempDir dir;
        jad::ProjectController c(false);
        QSignalSpy failed(&c, &jad::ProjectController::projectOpenFailed);
        QVERIFY(!c.openProject(url(dir.path() / "nothing here")));
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.first().first().toString().contains("Cannot open project"));
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
