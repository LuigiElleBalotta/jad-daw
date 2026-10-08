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

// The value of one track row field (by role name) for the track `id`; invalid when there is no such track.
QVariant trackField(jad::ProjectController& c, const QString& id, const char* role) {
    const int key = c.tracks()->roleNames().key(role);
    for (int i = 0; i < c.tracks()->rowCount(); ++i)
        if (c.tracks()->trackIdAt(i) == id) return c.tracks()->data(c.tracks()->index(i), key);
    return {};
}
bool trackFlag(jad::ProjectController& c, const QString& id, const char* role) { return trackField(c, id, role).toBool(); }

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
    void tempoAndSignatureFromTheLcd() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        QSignalSpy sent(&c, &jad::ProjectController::commandSent);
        c.setTempo(std::numeric_limits<double>::quiet_NaN());
        c.setTempo(5.0);  // clamped to 20
        QTRY_COMPARE(c.bpm(), 20.0);
        c.setTempo(5000.0);  // clamped to 999
        QTRY_COMPARE(c.bpm(), 999.0);
        QCOMPARE(sent.count(), 2);
        c.setSignature(3, 4);
        QTRY_COMPARE(c.signatureText(), QStringLiteral("3/4"));
        c.setSignature(3, 5);  // invalid: nothing sent, error shown
        QVERIFY(!c.lastError().isEmpty());
        QCOMPARE(c.signatureText(), QStringLiteral("3/4"));
    }
    void masterGainGoesThroughSetStrip() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.mixer()->rowCount() >= 3);
        c.setMasterGain(-6.0);
        QTRY_COMPARE(c.masterGainDb(), -6.0);
        c.setMasterGain(1000.0);
        QTRY_COMPARE(c.masterGainDb(), 24.0);
    }
    void barNavigationNeverGoesBelowZero() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        const double bar = c.property("barBeats").toDouble();
        c.barForward();  // to the second bar
        QTRY_COMPARE(c.positionBeats(), bar);
        c.barBack();  // to the start
        QTRY_COMPARE(c.positionBeats(), 0.0);
        for (int i = 0; i < 3; ++i) {  // past zero: the position clamps at 0, so a step forward lands on the first bar again
            c.barBack();
            c.barForward();
            QTRY_COMPARE(c.positionBeats(), bar);
        }
    }
    void mixerVisibilityIsAnObservableFlag() {
        jad::ProjectController c(false);
        QSignalSpy spy(&c, &jad::ProjectController::mixerVisibleChanged);
        QVERIFY(c.mixerVisible());  // visible by default, as before
        c.setMixerVisible(false);
        QVERIFY(!c.mixerVisible());
        QCOMPARE(spy.count(), 1);
        c.setMixerVisible(false);  // no change: no signal
        QCOMPARE(spy.count(), 1);
    }
    void trackSelectionModesAndStaleIds() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const QString a = c.tracks()->trackIdAt(0), b = c.tracks()->trackIdAt(1);
        c.selectTrack(a, "replace");
        QCOMPARE(c.selectedTrackIds(), QStringList{a});
        c.selectTrack(b, "extend");
        QCOMPARE(c.selectedTrackIds().size(), 2);
        c.selectTrack(a, "toggle");
        QCOMPARE(c.selectedTrackIds(), QStringList{b});
        c.selectTrack("nope", "replace");  // unknown ids are ignored
        QCOMPARE(c.selectedTrackIds(), QStringList{b});
        c.selectRegion(c.regions()->regionIdAt(0), "replace");
        QCOMPARE(c.selectedTrackIds(), QStringList{b});  // selecting a region does not touch the track selection
        QCOMPARE(c.selectedRegionIds().size(), 1);
        c.clearSelection();
        QVERIFY(c.selectedTrackIds().isEmpty());
        QVERIFY(c.selectedRegionIds().isEmpty());
    }
    void selectionIsPrunedWhenTracksDisappear() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        c.addTrack("audio");
        QTRY_COMPARE(c.tracks()->rowCount(), 4);
        c.selectTrack(c.tracks()->trackIdAt(3), "replace");
        QCOMPARE(c.selectedTrackIds().size(), 1);
        c.undo();  // the track disappears
        QTRY_COMPARE(c.tracks()->rowCount(), 3);
        QTRY_VERIFY(c.selectedTrackIds().isEmpty());
    }
    void muteAndSoloActOnSelectedTracksOnly() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        QSignalSpy sent(&c, &jad::ProjectController::commandSent);
        c.toggleMuteSelected();  // nothing selected: nothing happens
        c.toggleSoloSelected();
        QCOMPARE(sent.count(), 0);
        c.selectTrack(c.tracks()->trackIdAt(0), "replace");
        c.toggleMuteSelected();
        QTRY_COMPARE(sent.count(), 1);
    }
    void trackManagement() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        c.addTrack("instrument");
        QTRY_COMPARE(c.tracks()->rowCount(), 4);
        const QString id = c.tracks()->trackIdAt(3);
        c.renameTrack(id, "Lead");
        QTRY_COMPARE(c.tracks()->data(c.tracks()->index(3), c.tracks()->roleNames().key("name")).toString(), QStringLiteral("Lead"));
        c.renameTrack(id, "");  // rejected by the Core: shown, nothing changes
        QTRY_VERIFY(c.lastError().startsWith("bad_value"));
        c.setTrackColor(id, "orange");
        QTRY_COMPARE(c.tracks()->data(c.tracks()->index(3), c.tracks()->roleNames().key("color")).toString(), QStringLiteral("orange"));
        c.selectTrack(id, "replace");
        c.deleteSelectedTracks();
        QTRY_COMPARE(c.tracks()->rowCount(), 3);
        c.undo();
        QTRY_COMPARE(c.tracks()->rowCount(), 4);
    }
    void deletingATrackThatIsRoutedToIsRefused() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));  // the demo's Keys track sends to the Reverb Bus
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        QString busId;
        for (int i = 0; i < c.tracks()->rowCount(); ++i)
            if (c.tracks()->data(c.tracks()->index(i), c.tracks()->roleNames().key("kind")).toString() == "bus") busId = c.tracks()->trackIdAt(i);
        QVERIFY(!busId.isEmpty());
        c.selectTrack(busId, "replace");
        c.deleteSelectedTracks();
        QTRY_VERIFY(c.lastError().startsWith("in_use"));
        QCOMPARE(c.tracks()->rowCount(), 3);
    }
    void trackHeightIsClampedToTheFourSteps() {
        jad::ProjectController c(false);
        QCOMPARE(c.trackHeightIndex(), 1);
        c.setTrackHeightIndex(9);
        QCOMPARE(c.trackHeightIndex(), 3);
        c.setTrackHeightIndex(-4);
        QCOMPARE(c.trackHeightIndex(), 0);
    }
    void toolAndSnapState() {
        jad::ProjectController c(false);
        QCOMPARE(c.tool(), QStringLiteral("pointer"));
        c.setTool("scissors");
        QCOMPARE(c.tool(), QStringLiteral("scissors"));
        c.setTool("banana");  // unknown: ignored
        QCOMPARE(c.tool(), QStringLiteral("scissors"));
        QCOMPARE(c.snap(), QStringLiteral("quarter"));
        QCOMPARE(c.snapBeats(), 1.0);
        c.setSnap("sixteenth");
        QCOMPARE(c.snapBeats(), 0.25);
        c.setSnap("off");
        QCOMPARE(c.snapBeats(), 0.0);
        c.setSnap("bar");
        QCOMPARE(c.snapBeats(), 4.0);  // 4/4 project
        c.setSnap("zzz");
        QCOMPARE(c.snap(), QStringLiteral("bar"));
        QVERIFY(c.followPlayhead());
    }
    void splitThenJoinThroughTheController() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const int n = c.regions()->rowCount();
        const QString id = c.regions()->regionIdAt(0);
        const double start = c.regions()->data(c.regions()->index(0), c.regions()->roleNames().key("startBeats")).toDouble();
        c.setSnap("off");
        c.splitRegion(id, start + 0.5);
        QTRY_COMPARE(c.regions()->rowCount(), n + 1);
        c.joinWithNext(id);
        QTRY_COMPARE(c.regions()->rowCount(), n);
        c.undo();
        QTRY_COMPARE(c.regions()->rowCount(), n + 1);
    }
    void splitOutsideTheRegionIsRefusedWithAMessage() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const QString id = c.regions()->regionIdAt(0);
        c.splitRegion(id, -5.0);
        QTRY_VERIFY(!c.lastError().isEmpty());
        c.clearError();
        c.splitRegion(id, std::numeric_limits<double>::quiet_NaN());  // ignored, nothing sent
        QVERIFY(c.lastError().isEmpty());
    }
    void joinWithNothingAfterItReportsIt() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        QSignalSpy notices(&c, &jad::ProjectController::notice);
        c.joinWithNext(c.regions()->regionIdAt(0));
        QCOMPARE(notices.count(), 1);
        QVERIFY(notices.at(0).at(0).toString().contains("Nothing to join"));
        QVERIFY(c.lastError().isEmpty());
    }
    void pencilCreatesRegionsOnlyOnInstrumentTracks() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        QString instrument, audio;
        for (int i = 0; i < c.tracks()->rowCount(); ++i) {
            const QString kind = c.tracks()->data(c.tracks()->index(i), c.tracks()->roleNames().key("kind")).toString();
            if (kind == "instrument") instrument = c.tracks()->trackIdAt(i);
            if (kind == "audio") audio = c.tracks()->trackIdAt(i);
        }
        const int n = c.regions()->rowCount();
        c.createRegion(audio, 8.0, 4.0);
        QVERIFY(c.lastError().isEmpty());
        QCOMPARE(c.regions()->rowCount(), n);
        c.createRegion(instrument, 40.0, 4.0);
        QTRY_COMPARE(c.regions()->rowCount(), n + 1);
        c.createRegion(QStringLiteral("no-such-track"), 0.0, 1.0);
        QVERIFY(!c.lastError().isEmpty());
    }
    void resizeSendsOneCommandAndClamps() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const QString id = c.regions()->regionIdAt(0);
        QSignalSpy sent(&c, &jad::ProjectController::commandSent);
        c.resizeRegion(id, std::numeric_limits<double>::quiet_NaN(), 2.0);  // ignored
        c.resizeRegion(id, 0.0, std::numeric_limits<double>::infinity());   // ignored
        QCOMPARE(sent.count(), 0);
        c.setSnap("off");
        c.resizeRegion(id, -5.0, 0.0);  // clamped to start 0 and the minimum length
        QTRY_COMPARE(sent.count(), 1);
        QCOMPARE(sent.at(0).at(0).toString(), QStringLiteral("resize_region"));
        c.undo();
        QTRY_VERIFY(c.regions()->rowCount() > 0);
    }
    void resizingAShortRegionKeepsItShort() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() > 0);
        QString instrument;
        for (int i = 0; i < c.tracks()->rowCount(); ++i) {
            const QString kind = c.tracks()->data(c.tracks()->index(i), c.tracks()->roleNames().key("kind")).toString();
            if (kind == "instrument") instrument = c.tracks()->trackIdAt(i);
        }
        QVERIFY(!instrument.isEmpty());
        const int n = c.regions()->rowCount();
        c.createRegion(instrument, 8.0, 2.0);  // half a bar at snap "bar"
        QTRY_COMPARE(c.regions()->rowCount(), n + 1);
        const auto row = [&c]() {
            for (int i = 0; i < c.regions()->rowCount(); ++i) {
                const auto index = c.regions()->index(i);
                if (c.regions()->data(index, c.regions()->roleNames().key("startBeats")).toDouble() == 8.0)
                    return c.regions()->regionIdAt(i);
            }
            return QString();
        };
        const QString id = row();
        QVERIFY(!id.isEmpty());
        const auto lengthOf = [&c, id]() {
            for (int i = 0; i < c.regions()->rowCount(); ++i)
                if (c.regions()->regionIdAt(i) == id)
                    return c.regions()->data(c.regions()->index(i), c.regions()->roleNames().key("lengthBeats")).toDouble();
            return -1.0;
        };
        c.setSnap("bar");
        QSignalSpy sent(&c, &jad::ProjectController::commandSent);
        c.resizeRegion(id, 8.0, 0.5);  // the right edge dragged left: it must not grow the region
        QTRY_COMPARE(sent.count(), 1);
        QTest::qWait(300);             // the host applies it on its own thread
        QCOMPARE(lengthOf(), 2.0);
        c.resizeRegion(id, 8.0, 2.0);  // the left edge dragged right: the end stays where it was
        QTRY_COMPARE(sent.count(), 2);
        QTest::qWait(300);
        QCOMPARE(lengthOf(), 2.0);
    }
    // a bar is numerator * 4 / denominator quarter notes: 6/8 is 3 beats, 4/4 is 4
    void barLengthFollowsTheDenominator() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        c.setSnap("bar");
        c.locateBeats(0.0);
        c.setSignature(6, 8);
        QTRY_COMPARE(c.property("barBeats").toDouble(), 3.0);
        QCOMPARE(c.snapBeats(), 3.0);
        c.barForward();
        QTRY_COMPARE(c.positionBeats(), 3.0);
        c.setSignature(4, 4);
        QTRY_COMPARE(c.property("barBeats").toDouble(), 4.0);
        QCOMPARE(c.snapBeats(), 4.0);
    }
    void splitAtPlayheadWorksOnSelectedRegionsOnly() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        QSignalSpy notices(&c, &jad::ProjectController::notice);
        c.splitSelectedAtPlayhead();  // nothing selected
        QCOMPARE(notices.count(), 1);
        QVERIFY(notices.at(0).at(0).toString().contains("No selected region"));
    }
    void joinSelectedNeedsTwoRegions() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        c.selectRegion(c.regions()->regionIdAt(0), "replace");
        QSignalSpy notices(&c, &jad::ProjectController::notice);
        c.joinSelected();
        QCOMPARE(notices.count(), 1);
        QVERIFY(notices.at(0).at(0).toString().contains("at least two"));
    }
    void toolNoticesGoToTheToastNotTheErrorBar() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        QString audio;
        for (int i = 0; i < c.tracks()->rowCount(); ++i)
            if (c.tracks()->data(c.tracks()->index(i), c.tracks()->roleNames().key("kind")).toString() == "audio")
                audio = c.tracks()->trackIdAt(i);
        QVERIFY(!audio.isEmpty());
        QSignalSpy notices(&c, &jad::ProjectController::notice);
        c.createRegion(audio, 8.0, 4.0);  // pencil on a track that cannot hold MIDI
        QCOMPARE(notices.count(), 1);
        QVERIFY(notices.at(0).at(0).toString().contains("MIDI"));
        QVERIFY(c.lastError().isEmpty());
        c.splitSelectedAtPlayhead();  // nothing selected
        QCOMPARE(notices.count(), 2);
        QVERIFY(c.lastError().isEmpty());
        c.joinSelected();  // fewer than two selected
        QCOMPARE(notices.count(), 3);
        QVERIFY(c.lastError().isEmpty());
        c.joinWithNext(c.regions()->regionIdAt(0));  // nothing starts where it ends
        QCOMPARE(notices.count(), 4);
        QVERIFY(c.lastError().isEmpty());
    }
    void trackToggleStateSurvivesTrackChanges() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 2);
        const QString id = c.tracks()->trackIdAt(1);
        c.setTrackToggle("track.recordArm", id, true);
        QVERIFY(trackFlag(c, id, "recordArm"));
        QVERIFY(!trackFlag(c, id, "inputMonitor"));
        c.selectTrack(id, "replace");
        QVERIFY(c.property("selectedRecordArm").toBool());  // the Track menu shows the selected track's state
        QVERIFY(!c.property("selectedInputMonitor").toBool());
        c.selectTrack(c.tracks()->trackIdAt(0), "replace");
        QVERIFY(!c.property("selectedRecordArm").toBool());
        c.selectTrack(id, "replace");
        const int n = c.tracks()->rowCount();
        c.addTrack("audio");
        QTRY_COMPARE(c.tracks()->rowCount(), n + 1);
        QVERIFY(trackFlag(c, id, "recordArm"));
        QVERIFY(c.property("selectedRecordArm").toBool());
        c.setTrackToggle("track.inputMonitor", id, true);
        QVERIFY(trackFlag(c, id, "inputMonitor"));
        QVERIFY(trackFlag(c, id, "recordArm"));
    }
    void selectedMuteIsOneUndoStepAndUniform() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString a = c.tracks()->trackIdAt(0), b = c.tracks()->trackIdAt(1), d = c.tracks()->trackIdAt(2);
        c.setMute(a, true);
        c.setMute(d, true);  // mixed: a and d on, b off
        QTRY_VERIFY(trackFlag(c, a, "mute") && !trackFlag(c, b, "mute") && trackFlag(c, d, "mute"));
        c.selectTrack(a, "replace");
        c.selectTrack(b, "extend");
        c.selectTrack(d, "extend");
        c.toggleMuteSelected();  // one of them is off: all go on
        QTRY_VERIFY(trackFlag(c, a, "mute") && trackFlag(c, b, "mute") && trackFlag(c, d, "mute"));
        c.undo();  // one step back to the mixed state
        QTRY_VERIFY(trackFlag(c, a, "mute") && !trackFlag(c, b, "mute") && trackFlag(c, d, "mute"));
        c.toggleMuteSelected();  // one of them is off again: all go on
        QTRY_VERIFY(trackFlag(c, a, "mute") && trackFlag(c, b, "mute") && trackFlag(c, d, "mute"));
        c.toggleMuteSelected();  // all on now: all go off
        QTRY_VERIFY(!trackFlag(c, a, "mute") && !trackFlag(c, b, "mute") && !trackFlag(c, d, "mute"));
    }
    void selectedColorIsOneUndoStep() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString a = c.tracks()->trackIdAt(0), b = c.tracks()->trackIdAt(1), d = c.tracks()->trackIdAt(2);
        const QVariant colorA = trackField(c, a, "color"), colorB = trackField(c, b, "color"), colorD = trackField(c, d, "color");
        c.selectTrack(a, "replace");
        c.selectTrack(b, "extend");
        c.selectTrack(d, "extend");
        c.setSelectedColor("red");
        QTRY_VERIFY(trackField(c, a, "color") == "red" && trackField(c, b, "color") == "red" && trackField(c, d, "color") == "red");
        c.undo();
        QTRY_VERIFY(trackField(c, a, "color") == colorA && trackField(c, b, "color") == colorB && trackField(c, d, "color") == colorD);
    }
    void rectangleSelectionSelectsAllIds() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() >= 2);
        QStringList ids{c.regions()->regionIdAt(0), c.regions()->regionIdAt(1), "ghost"};
        c.selectRegions(ids, "replace");
        QCOMPARE(c.selectedRegionIds().size(), 2);  // the unknown id is dropped
        c.clearSelection();
        c.selectRegionsIn(0.0, 1000.0, 0, 99, "replace");  // every row, every beat
        QCOMPARE(c.selectedRegionIds().size(), c.regions()->rowCount());
        c.clearSelection();
        c.selectRegionsIn(900.0, 1000.0, 0, 99, "replace");  // nothing out there
        QVERIFY(c.selectedRegionIds().isEmpty());
    }
};

QTEST_MAIN(BridgeTest)
#include "tst_bridge.moc"
