#include <QSettings>
#include <QDir>
#include <QSignalSpy>
#include <QtTest>
#include <cmath>
#include <fstream>
#include <limits>

#include "bridge/project_controller.h"
#include "lpc/audio/engine.h"
#include "lpc/demo_project.h"
#include "lpc/project_io.h"
#include "lpc/audio_decode.h"
#include "lpc/audio_ops.h"
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
    static QString trackIdOfKind(jad::ProjectController& c, const char* kind) {
        const auto roles = c.tracks()->roleNames();
        for (int i = 0; i < c.tracks()->rowCount(); ++i)
            if (c.tracks()->data(c.tracks()->index(i), roles.key("kind")).toString() == kind)
                return c.tracks()->data(c.tracks()->index(i), roles.key("trackId")).toString();
        return {};
    }
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
    void importAudioConvertsAnotherRateToTheProjectRate() {
        TempDir dir; const auto proj = dir.path() / "d.lpc";
        std::filesystem::create_directories(proj); lpc::saveProject(lpc::makeDemoProject(proj), proj);
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(proj)));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        lpc::writeWav(dir.path() / "x44.wav", 44100, 2, std::vector<float>(2 * 4410, 0.1f), lpc::WavFormat::Float32);
        const QString audioTrack = firstAudioTrackId(c);
        const auto before = countFiles(proj / "audio");
        const int regions = c.regions()->rowCount();
        c.importAudio(url(dir.path() / "x44.wav"), audioTrack, 0.0);
        QTRY_COMPARE(c.regions()->rowCount(), regions + 1);   // converted from 44.1 kHz, no error
        QVERIFY(c.lastError().isEmpty());
        QCOMPARE(countFiles(proj / "audio"), before + 1);
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
        { std::ofstream bad(dir.path() / "bad.wav", std::ios::binary); bad << "this is not audio"; }
        lpc::writeWav(dir.path() / "good.wav", 48000, 2, std::vector<float>(2 * 4800, 0.1f), lpc::WavFormat::Float32);
        c.importAudioFiles({url(dir.path() / "bad.wav"), url(dir.path() / "good.wav")}, firstAudioTrackId(c), 0.0);
        QTRY_COMPARE(c.regions()->rowCount(), n + 1);
        QVERIFY(c.lastError().contains("bad.wav"));
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
    void aProjectWithAudioDisabledPlaysOnTheEngineDriver() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QVERIFY(!c.deviceError().isEmpty());       // there is no device ("audio output disabled")
        c.play();
        QTRY_VERIFY(c.playing());                  // the driver runs the engine in real time instead
        QVERIFY(c.lastError().isEmpty());
        c.stop();
        QTRY_VERIFY(!c.playing());
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
    void copyAndPasteMakeAnotherRegionAtThePlayheadAndSelectIt() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const int n = c.regions()->rowCount();
        const QString id = c.regions()->regionIdAt(0);
        c.selectRegion(id, "replace");
        c.copySelectedRegions();
        c.locateBeats(40.0);
        QTRY_VERIFY(std::abs(c.positionBeats() - 40.0) < 0.01);  // the Core moves the playhead on its own thread
        c.pasteRegions(false);
        QTRY_COMPARE(c.regions()->rowCount(), n + 1);
        QTRY_VERIFY(c.selectedRegionIds().size() == 1 && c.selectedRegionIds().first() != id);
        const jad::RegionRow* pasted = c.regions()->find(c.selectedRegionIds().first());
        QVERIFY(pasted);
        QVERIFY(std::abs(pasted->startBeats - 40.0) < 0.01);
        c.undo();
        QTRY_COMPARE(c.regions()->rowCount(), n);
    }
    void duplicateGoesRightAfterTheSelection() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const int n = c.regions()->rowCount();
        const QString id = c.regions()->regionIdAt(0);
        const jad::RegionRow before = *c.regions()->find(id);
        c.selectRegion(id, "replace");
        c.duplicateSelectedRegions();
        QTRY_COMPARE(c.regions()->rowCount(), n + 1);
        QTRY_VERIFY(c.selectedRegionIds().size() == 1 && c.selectedRegionIds().first() != id);
        const jad::RegionRow* copy = c.regions()->find(c.selectedRegionIds().first());
        QVERIFY(copy);
        QVERIFY(std::abs(copy->startBeats - (before.startBeats + before.lengthBeats)) < 0.01);
    }
    void muteRegionSilencesItAndToggles() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const int muted = c.regions()->roleNames().key("muted");
        c.selectRegion(c.regions()->regionIdAt(0), "replace");
        c.toggleMuteSelectedRegions();
        QTRY_VERIFY(c.regions()->data(c.regions()->index(0), muted).toBool());
        c.toggleMuteSelectedRegions();
        QTRY_VERIFY(!c.regions()->data(c.regions()->index(0), muted).toBool());
        c.toggleMuteSelectedRegions();
        QTRY_VERIFY(c.regions()->data(c.regions()->index(0), muted).toBool());
        c.undo();                                                    // a command now: one undo step
        QTRY_VERIFY(!c.regions()->data(c.regions()->index(0), muted).toBool());
    }
    void loopUnloopRegionsRepeatsTheContentAndIsOneUndoStep() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const int loop = c.regions()->roleNames().key("loopBeats");
        c.selectRegion(c.regions()->regionIdAt(0), "replace");
        QCOMPARE(c.regions()->data(c.regions()->index(0), loop).toDouble(), 0.0);
        c.toggleLoopSelectedRegions();
        QTRY_VERIFY(c.regions()->data(c.regions()->index(0), loop).toDouble() > 0.0);
        c.toggleLoopSelectedRegions();
        QTRY_COMPARE(c.regions()->data(c.regions()->index(0), loop).toDouble(), 0.0);
        c.undo();
        QTRY_VERIFY(c.regions()->data(c.regions()->index(0), loop).toDouble() > 0.0);
    }
    void nudgeMovesSelectedRegionsByTheNudgeValue() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const QString id = c.regions()->regionIdAt(0);
        const double start = c.regions()->find(id)->startBeats;
        c.selectRegion(id, "replace");
        c.setNudgeBeats(2.0);
        c.nudgeSelectedRegions(1);
        QTRY_VERIFY(std::abs(c.regions()->find(id)->startBeats - (start + 2.0)) < 0.01);
    }
    void deleteUnusedTracksKeepsTheTracksWithRegions() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const int before = c.tracks()->rowCount();
        c.addTrack("instrument");
        QTRY_COMPARE(c.tracks()->rowCount(), before + 1);
        const int withNew = before + 1;
        const int regions = c.regions()->rowCount();
        c.deleteUnusedTracks();
        QTRY_VERIFY(c.tracks()->rowCount() < withNew);
        QCOMPARE(c.regions()->rowCount(), regions);  // nothing with a region went
    }
    void saveACopyWritesAWholeProjectAndKeepsTheCurrentOne() {
        TempDir dir;
        TempDir other;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const int n = c.regions()->rowCount();
        const auto copy = other.path() / "copy";
        QVERIFY(c.saveProjectAs(url(copy), false));
        jad::ProjectController d(false);
        QVERIFY(d.openProject(url(copy)));
        QTRY_COMPARE(d.regions()->rowCount(), n);
        QCOMPARE(c.regions()->rowCount(), n);
        QVERIFY(!c.saveProjectAs(url(copy), false));  // not empty any more
    }
    void bounceWritesAWavFileOfTheProject() {
        TempDir dir;
        TempDir other;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const auto out = other.path() / "mix.wav";
        QSignalSpy notices(&c, &jad::ProjectController::notice);
        c.bounceProject(url(out));
        QTRY_VERIFY_WITH_TIMEOUT(std::filesystem::exists(out) && std::filesystem::file_size(out) > 1000 && notices.count() >= 2, 20000);
        QVERIFY(notices.last().at(0).toString().startsWith("Bounced"));
    }
    void metronomeToggleSurvivesATempoChange() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QSignalSpy spy(&c, &jad::ProjectController::metronomeChanged);
        c.setMetronome(true);
        QVERIFY(c.metronomeOn());
        QCOMPARE(spy.count(), 1);
        c.setMetronome(true);
        QCOMPARE(spy.count(), 1);  // no change, no signal
        c.setTempo(90);
        QTRY_COMPARE(c.property("bpm").toDouble(), 90.0);
        c.setSignature(3, 4);
        QTRY_COMPARE(c.property("barBeats").toDouble(), 3.0);
        c.setMetronome(false);
        QVERIFY(!c.metronomeOn());
    }
    void clickSettingsKeepTheModeGroupingAndSampleFiles() {
        jad::ProjectController c(false);
        QSignalSpy spy(&c, &jad::ProjectController::clickSettingsChanged);
        QCOMPARE(c.clickMode(), QString("beats"));
        c.setClickMode("nonsense");
        QCOMPARE(c.clickMode(), QString("beats"));
        QCOMPARE(spy.count(), 0);
        c.setClickMode("grouped");
        c.setClickGrouping("3+2+2");
        QCOMPARE(c.clickMode(), QString("grouped"));
        QCOMPARE(c.clickGrouping(), QString("3+2+2"));
        c.setClickSlotFile(1, QUrl::fromLocalFile("C:/x/one.wav"));
        QVERIFY(c.clickSlotFile(1).endsWith("one.wav"));
        c.setClickSlotGain(1, 5.0);
        QCOMPARE(c.clickSlotGain(1), 2.0);  // clamped
        c.clearClickSlot(1);
        QVERIFY(c.clickSlotFile(1).isEmpty());
        QCOMPARE(jad::ProjectController::clickSlotName(1), QString("1"));
        QCOMPARE(jad::ProjectController::clickSlotName(36), QString("la"));
        QSettings s(QDir(QDir::tempPath()).filePath("jad-click-test.ini"), QSettings::IniFormat);
        c.saveClickSettings(s);
        jad::ProjectController d(false);
        d.loadClickSettings(s);
        QCOMPARE(d.clickMode(), QString("grouped"));
        QCOMPARE(d.clickGrouping(), QString("3+2+2"));
    }
    void recordingNeedsAnArmedAudioTrackAndMakesARegionFromTheInput() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        QSignalSpy notices(&c, &jad::ProjectController::notice);
        c.startRecording();
        QVERIFY(!c.recording());
        QVERIFY(notices.count() >= 1 && notices.last().at(0).toString().contains("Arm a track"));
        QString audio;
        for (int i = 0; i < c.tracks()->rowCount(); ++i)
            if (c.tracks()->kindAt(i) == "audio") audio = c.tracks()->trackIdAt(i);
        QVERIFY(!audio.isEmpty());
        c.setTrackToggle("track.recordArm", audio, true);
        const int before = c.regions()->rowCount();
        c.locateBeats(8.0);
        QTRY_VERIFY(std::abs(c.positionBeats() - 8.0) < 0.01);
        c.startRecording();
        QVERIFY(c.recording());
        QTest::qWait(600);  // the engine driver of a project without a device pumps the engine in real time: silence is captured
        c.stop();
        QTRY_COMPARE_WITH_TIMEOUT(c.regions()->rowCount(), before + 1, 15000);
        QVERIFY(!c.recording());
        const jad::RegionRow* take = nullptr;
        for (int i = 0; i < c.regions()->rowCount(); ++i) {
            const jad::RegionRow* r = c.regions()->find(c.regions()->regionIdAt(i));
            if (r && r->trackId == audio && std::abs(r->startBeats - 8.0) < 0.01) take = r;
        }
        QVERIFY(take);                        // at the position where recording started
        QVERIFY(take->lengthBeats > 0.5);     // about 0.6 s at 120 bpm is 1.2 beats
        c.undo();
        QTRY_COMPARE(c.regions()->rowCount(), before);
    }
    void countInChoiceAcceptsBarsAndBeatsOnly() {
        jad::ProjectController c(false);
        QCOMPARE(c.countInChoice(), 1);
        c.setCountInChoice(6);
        QCOMPARE(c.countInChoice(), 6);
        c.setCountInChoice(7);
        QCOMPARE(c.countInChoice(), 6);   // out of range: kept
        c.setCountInChoice(0);
        QCOMPARE(c.countInChoice(), 6);
        c.setCountInChoice(-3);
        QCOMPARE(c.countInChoice(), -3);  // 3/4: three beats
        c.setCountInChoice(-4);
        QCOMPARE(c.countInChoice(), -3);
    }
    void newTracksMakesSeveralInOneUndoStep() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() > 0);
        const int before = c.tracks()->rowCount();
        c.addTracks("audio", 3, "Gtr");
        QTRY_COMPARE(c.tracks()->rowCount(), before + 3);
        auto names = [&] { QStringList out; for (int i = 0; i < c.tracks()->rowCount(); ++i) out << c.tracks()->nameAt(i); return out; };
        QVERIFY(names().contains("Gtr 1") && names().contains("Gtr 2") && names().contains("Gtr 3"));
        c.addTracks("instrument", 1, "Lead");
        QTRY_COMPARE(c.tracks()->rowCount(), before + 4);
        QVERIFY(names().contains("Lead"));
        c.undo();
        QTRY_COMPARE(c.tracks()->rowCount(), before + 3);
        c.undo();
        QTRY_COMPARE(c.tracks()->rowCount(), before);
    }
    void droppedAudioOutsideAnAudioTrackMakesATrackNamedAfterTheFile() {
        TempDir dir;
        TempDir other;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() > 0);
        const int tracks = c.tracks()->rowCount();
        const int regions = c.regions()->rowCount();
        // a 44.1 kHz stereo WAV: it must be converted to the project's rate
        std::vector<float> samples(2 * 4410);
        for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = 0.2f;
        const auto file = other.path() / "Guitar loop.wav";
        lpc::writeWav(file, 44100, 2, samples, lpc::WavFormat::Pcm16);
        c.importAudioFilesAt({url(file)}, QString(), 4.0);   // no track under the drop
        QTRY_COMPARE_WITH_TIMEOUT(c.tracks()->rowCount(), tracks + 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(c.regions()->rowCount(), regions + 1, 15000);
        bool named = false;
        for (int i = 0; i < c.tracks()->rowCount(); ++i) named = named || c.tracks()->nameAt(i) == "Guitar loop";
        QVERIFY(named);
        const jad::RegionRow* made = nullptr;
        for (int i = 0; i < c.regions()->rowCount(); ++i) {
            const jad::RegionRow* r = c.regions()->find(c.regions()->regionIdAt(i));
            if (r && std::abs(r->startBeats - 4.0) < 0.01 && r->audio) made = r;
        }
        QVERIFY(made);
        QVERIFY(std::abs(made->lengthBeats - 0.2) < 0.01);   // 0.1 s at 120 bpm
    }
    void audioSettingsListDevicesAndAreRemembered() {
        jad::ProjectController c(false);
        c.setDeviceListerForTest([](const std::string& out, const std::string&) {
            lpc::AudioDeviceChoices d;
            d.outputs = {"Speakers", "Interface"};
            d.inputs = {"Mic", "Line"};
            d.currentOutput = out.empty() ? "Speakers" : out;
            d.currentInput = "Mic";
            d.rates = out == "Interface" ? std::vector<double>{44100, 48000, 96000} : std::vector<double>{48000};
            d.buffers = {64, 128, 256, 512};
            d.inputChannels = 2;
            d.outputChannels = 2;
            return d;
        });
        QVariantMap m = c.audioDevices("", "");
        QCOMPARE(m.value("outputs").toStringList(), QStringList({"Speakers", "Interface"}));
        QCOMPARE(m.value("currentOutput").toString(), QString("Speakers"));
        QCOMPARE(m.value("rates").toList().size(), 1);
        m = c.audioDevices("Interface", "");
        QCOMPARE(m.value("rates").toList().size(), 3);
        QSignalSpy spy(&c, &jad::ProjectController::audioSettingsChanged);
        c.applyAudioSettings("Interface", "Line", 128);
        QCOMPARE(c.audioOutput(), QString("Interface"));
        QCOMPARE(c.audioInput(), QString("Line"));
        QCOMPARE(c.audioBufferSize(), 128);
        QVERIFY(spy.count() >= 1);
        c.applyAudioSettings("", "", 99999);
        QCOMPARE(c.audioBufferSize(), 4096);   // kept in range
        QSettings s(QDir(QDir::tempPath()).filePath("jad-audio-test.ini"), QSettings::IniFormat);
        c.applyAudioSettings("Interface", "Line", 128);
        c.saveAudioSettings(s);
        jad::ProjectController d(false);
        d.loadAudioSettings(s);
        QCOMPARE(d.audioOutput(), QString("Interface"));
        QCOMPARE(d.audioBufferSize(), 128);
    }
    void trackInputIsACommandAndPunchAndDelayAreKept() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() > 0);
        const QString audio = firstAudioTrackId(c);
        QCOMPARE(c.trackInput(audio), 0);
        c.setTrackInput(audio, 2);
        QTRY_COMPARE(c.trackInput(audio), 2);
        c.undo();
        QTRY_COMPARE(c.trackInput(audio), 0);
        c.setTrackInput(audio, 99);                  // refused: out of range
        QCOMPARE(c.inputChoices().first(), QString("Input 1 + 2 (stereo)"));
        QSignalSpy punch(&c, &jad::ProjectController::punchChanged);
        c.setPunchRange(8, 4);                       // backwards: ignored
        QCOMPARE(punch.count(), 0);
        c.setPunchRange(4, 8);
        c.setPunchEnabled(true);
        QCOMPARE(c.punchStartBeats(), 4.0);
        QCOMPARE(c.punchEndBeats(), 8.0);
        QVERIFY(c.punchEnabled());
        c.setRecordingDelay(100000);
        QCOMPARE(c.recordingDelay(), 48000);         // kept in range
    }
    void builtInEffectsHaveSpecsParametersAndAnEditorState() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() > 0);
        const QVariantList specs = c.effectSpecs();
        QStringList ids;
        for (const QVariant& v : specs) ids << v.toMap().value("id").toString();
        QVERIFY(ids.contains("builtin.eq") && ids.contains("builtin.compressor") && ids.contains("builtin.reverb"));
        const QString audio = firstAudioTrackId(c);
        c.addInsert(audio, "builtin.compressor");
        QTRY_VERIFY(c.trackInserts(audio).size() >= 1);
        const int last = static_cast<int>(c.trackInserts(audio).size()) - 1;
        QCOMPARE(c.trackInserts(audio).at(last).toMap().value("processorId").toString(), QString("builtin.compressor"));
        c.setInsertParam(audio, last, "threshold", -30.0);
        QTRY_COMPARE(c.trackInserts(audio).at(last).toMap().value("params").toMap().value("threshold").toDouble(), -30.0);
        c.setInsertParam(audio, last, "ratio", 99.0);   // out of its range: the Core refuses it
        c.openEffectEditor(audio, last);
        QCOMPARE(c.effectEditorTrack(), audio);
        QCOMPARE(c.effectEditorIndex(), last);
        c.closeEffectEditor();
        QVERIFY(c.effectEditorTrack().isEmpty());
        QVariantMap flat;
        const QVariantList curve = c.eqCurve(flat, 16, 20, 20000);
        QCOMPARE(curve.size(), 16);
        QVERIFY(std::abs(curve.first().toDouble()) < 0.01);                  // a flat EQ
        QVariantMap boost{{"m2Freq", 1000.0}, {"m2Gain", 12.0}};
        double peak = 0;
        for (const QVariant& v : c.eqCurve(boost, 64, 20, 20000)) peak = std::max(peak, v.toDouble());
        QVERIFY(peak > 11.0);
    }
    void notesPlayedWhileRecordingBecomeAMidiRegionOnTheArmedInstrumentTrack() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() > 0);
        QString instrument;
        for (int i = 0; i < c.tracks()->rowCount(); ++i)
            if (c.tracks()->kindAt(i) == "instrument") instrument = c.tracks()->trackIdAt(i);
        QVERIFY(!instrument.isEmpty());
        const int before = c.regions()->rowCount();
        c.setTrackToggle("track.recordArm", instrument, true);
        QTRY_COMPARE(c.liveTargetTrack(), instrument);   // the armed instrument plays what is played
        c.locateBeats(16.0);
        QTRY_VERIFY(std::abs(c.positionBeats() - 16.0) < 0.01);
        c.startRecording();
        QVERIFY(c.recording());
        QTest::qWait(150);
        c.playNote(62, 100, true);
        QTest::qWait(200);
        c.playNote(62, 0, false);
        QTest::qWait(100);
        c.stop();
        QTRY_COMPARE_WITH_TIMEOUT(c.regions()->rowCount(), before + 1, 15000);
        const jad::RegionRow* made = nullptr;
        for (int i = 0; i < c.regions()->rowCount(); ++i) {
            const jad::RegionRow* r = c.regions()->find(c.regions()->regionIdAt(i));
            if (r && r->trackId == instrument && std::abs(r->startBeats - 16.0) < 0.2) made = r;
        }
        QVERIFY(made);
        const QVariantList notes = c.regionNotes(made->id);
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.first().toMap().value("note").toInt(), 62);
        QVERIFY(notes.first().toMap().value("length").toDouble() > 0.2);   // 0.2 s at 120 bpm is 0.4 beats
        c.undo();
        QTRY_COMPARE(c.regions()->rowCount(), before);
    }
    void midiInputsAreChosenAndRemembered() {
        jad::ProjectController c(false);
        QSignalSpy spy(&c, &jad::ProjectController::midiChanged);
        c.setMidiInputs({"Keystation", "Pad"});
        QCOMPARE(c.midiInputsChosen(), QStringList({"Keystation", "Pad"}));
        QVERIFY(spy.count() >= 1);
        QSettings s(QDir(QDir::tempPath()).filePath("jad-midi-test.ini"), QSettings::IniFormat);
        c.saveMidiSettings(s);
        jad::ProjectController d(false);
        d.loadMidiSettings(s);
        QCOMPARE(d.midiInputsChosen(), QStringList({"Keystation", "Pad"}));
    }
    void regionFadesAreCommandsInBeatsWithUndo() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        QString audio;
        for (int i = 0; i < c.regions()->rowCount(); ++i) {
            const jad::RegionRow* r = c.regions()->find(c.regions()->regionIdAt(i));
            if (r && r->audio) audio = r->id;
        }
        QVERIFY(!audio.isEmpty());
        QCOMPARE(c.regions()->find(audio)->fadeInBeats, 0.0);
        c.setRegionFades(audio, 0.5, 0.25);
        QTRY_VERIFY(std::abs(c.regions()->find(audio)->fadeInBeats - 0.5) < 0.01);
        QVERIFY(std::abs(c.regions()->find(audio)->fadeOutBeats - 0.25) < 0.01);
        c.setRegionFades(audio, 9999.0, 0.0);              // longer than the region: kept inside it
        QTRY_VERIFY(c.regions()->find(audio)->fadeInBeats > 0.5);
        QVERIFY(c.regions()->find(audio)->fadeInBeats <= c.regions()->find(audio)->lengthBeats + 1e-6);
        c.undo();
        QTRY_VERIFY(std::abs(c.regions()->find(audio)->fadeInBeats - 0.5) < 0.01);
        c.undo();
        QTRY_COMPARE(c.regions()->find(audio)->fadeInBeats, 0.0);
    }
    void normalizeMakesANewFileWithTheTargetPeakAndOneUndoStep() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        QString audio, media;
        for (int i = 0; i < c.regions()->rowCount(); ++i) {
            const jad::RegionRow* r = c.regions()->find(c.regions()->regionIdAt(i));
            if (r && r->audio) { audio = r->id; media = r->mediaId; }
        }
        QVERIFY(!audio.isEmpty());
        c.selectRegion(audio, "replace");
        c.processSelectedRegions("normalize", -6.0);
        QTRY_VERIFY_WITH_TIMEOUT(c.regions()->find(audio) && c.regions()->find(audio)->mediaId != media, 15000);
        // the new file holds the region's part at -6 dBFS
        const QString relative = c.property("mediaPathForTest").toString();
        Q_UNUSED(relative);
        const auto files = countFiles(dir.path() / "d.lpc" / "audio");
        QVERIFY(files >= 2);
        c.undo();
        QTRY_COMPARE(c.regions()->find(audio)->mediaId, media);
    }
    void stripSilenceCutsARegionAtItsSilencesAndUndoRestoresIt() {
        TempDir dir;
        TempDir other;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() > 0);
        std::vector<float> samples(48000 * 3 * 2, 0.0f);   // 3 s stereo: sound at 0.2-0.7 s and at 1.8-2.4 s
        auto burst = [&](double from, double to) {
            for (int i = static_cast<int>(from * 48000); i < static_cast<int>(to * 48000); ++i) { samples[static_cast<size_t>(i) * 2] = samples[static_cast<size_t>(i) * 2 + 1] = 0.5f * std::sin(0.2f * static_cast<float>(i)); }
        };
        burst(0.2, 0.7);
        burst(1.8, 2.4);
        lpc::writeWav(other.path() / "bursts.wav", 48000, 2, samples, lpc::WavFormat::Float32);
        const QString audioTrack = firstAudioTrackId(c);
        const int before = c.regions()->rowCount();
        c.importAudio(url(other.path() / "bursts.wav"), audioTrack, 100.0);
        QTRY_COMPARE_WITH_TIMEOUT(c.regions()->rowCount(), before + 1, 15000);
        QString id;
        for (int i = 0; i < c.regions()->rowCount(); ++i) {
            const jad::RegionRow* r = c.regions()->find(c.regions()->regionIdAt(i));
            if (r && r->audio && r->startBeats > 99.0) id = r->id;
        }
        QVERIFY(!id.isEmpty());
        c.selectRegion(id, "replace");
        c.stripSilence(-30.0, 200.0);
        QTRY_COMPARE_WITH_TIMEOUT(c.regions()->rowCount(), before + 2, 15000);   // the region became two sounds
        c.undo();
        QTRY_COMPARE(c.regions()->rowCount(), before + 1);
    }
    void trimToSelectionKeepsOnlyTheChosenFramesOfTheRegion() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        QString audio;
        for (int i = 0; i < c.regions()->rowCount(); ++i) {
            const jad::RegionRow* r = c.regions()->find(c.regions()->regionIdAt(i));
            if (r && r->audio) audio = r->id;
        }
        const jad::RegionRow before = *c.regions()->find(audio);
        QVERIFY(before.lengthFrames > 1000);
        const double from = 0.25 * static_cast<double>(before.lengthFrames), to = 0.75 * static_cast<double>(before.lengthFrames);
        c.trimRegionToFrames(audio, from, to);
        QTRY_VERIFY(std::abs(c.regions()->find(audio)->lengthBeats - 0.5 * before.lengthBeats) < 0.05 * before.lengthBeats);
        QVERIFY(std::abs(c.regions()->find(audio)->startBeats - (before.startBeats + 0.25 * before.lengthBeats)) < 0.05 * before.lengthBeats);
        QVERIFY(c.regions()->find(audio)->sourceOffsetFrames > before.sourceOffsetFrames);   // it plays the same sound, from later in the file
        c.undo();
        QTRY_VERIFY(std::abs(c.regions()->find(audio)->lengthBeats - before.lengthBeats) < 0.01);
    }
    void waveformZoomAcceptsOnlyItsSteps() {
        jad::ProjectController c(false);
        QSignalSpy spy(&c, &jad::ProjectController::waveformZoomChanged);
        QCOMPARE(c.waveformZoom(), 1);
        c.setWaveformZoom(4);
        QCOMPARE(c.waveformZoom(), 4);
        c.setWaveformZoom(3);
        QCOMPARE(c.waveformZoom(), 4);
        QCOMPARE(spy.count(), 1);
    }
    void groupedTracksMoveTogetherInOneUndoStep() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        QString keys, tone;
        for (int i = 0; i < c.tracks()->rowCount(); ++i) {
            if (c.tracks()->kindAt(i) == "instrument") keys = c.tracks()->trackIdAt(i);
            if (c.tracks()->kindAt(i) == "audio") tone = c.tracks()->trackIdAt(i);
        }
        auto gainOf = [&](const QString& id) { for (int i = 0; i < c.mixer()->rowCount(); ++i) { const auto m = c.mixer()->data(c.mixer()->index(i), c.mixer()->roleNames().key("info")).toMap(); if (m.value("trackId").toString() == id) return m.value("gainDb").toDouble(); } return 1e9; };
        auto mutedOf = [&](const QString& id) { for (int i = 0; i < c.mixer()->rowCount(); ++i) { const auto m = c.mixer()->data(c.mixer()->index(i), c.mixer()->roleNames().key("info")).toMap(); if (m.value("trackId").toString() == id) return m.value("mute").toBool(); } return false; };
        const double keys0 = gainOf(keys), tone0 = gainOf(tone);
        c.createGroup({keys, tone}, "Band");
        QTRY_COMPARE(c.groups().size(), 1);
        QCOMPARE(c.trackGroup(tone).value("name").toString(), QString("Band"));
        QCOMPARE(c.trackGroup(keys).value("id"), c.trackGroup(tone).value("id"));
        c.setGain(tone, tone0 - 6.0);
        QTRY_VERIFY(std::abs(gainOf(tone) - (tone0 - 6.0)) < 0.01);
        QTRY_VERIFY(std::abs(gainOf(keys) - (keys0 - 6.0)) < 0.01);          // the other member moved by the same amount
        c.setMute(tone, true);
        QTRY_VERIFY(mutedOf(keys));                                           // mute is shared as it is
        c.undo();                                                             // one step took both
        QTRY_VERIFY(!mutedOf(keys));
        c.setGroupsActive(false);
        c.setGain(tone, tone0 - 12.0);
        QTRY_VERIFY(std::abs(gainOf(tone) - (tone0 - 12.0)) < 0.01);
        QVERIFY(std::abs(gainOf(keys) - (keys0 - 6.0)) < 0.01);               // groups off: alone
        c.setGroupsActive(true);
        c.setGroupField(c.trackGroup(tone).value("id").toString(), "volume", false);
        QTRY_VERIFY(!c.trackGroup(tone).value("volume").toBool());
        c.setTrackGroup(tone, "");                                            // out of the group: the group of one track goes with it? no, keys remains
        QTRY_VERIFY(c.trackGroup(tone).isEmpty());
        QVERIFY(!c.trackGroup(keys).isEmpty());
        c.setTrackGroup(keys, "");
        QTRY_COMPARE(c.groups().size(), 0);                                   // an empty group goes
    }
    void touchModeWritesTheFaderDragIntoTheLaneWhilePlayingAndOffIgnoresIt() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = firstAudioTrackId(c);
        QCOMPARE(c.automationMode(audio), QString("read"));
        c.setAutomationMode(audio, "touch");
        QTRY_COMPARE(c.automationMode(audio), QString("touch"));
        c.setAutomationMode(audio, "bogus");                         // refused
        QCOMPARE(c.automationMode(audio), QString("touch"));
        c.play();
        QTRY_VERIFY(c.playing());
        c.beginGesture();
        for (int i = 1; i <= 6; ++i) { c.setGainLive(audio, -2.0 * i); QTest::qWait(60); }
        c.setGain(audio, -12.0);
        c.endGesture();                                              // touch: the take ends when the fader is let go
        QTRY_VERIFY(c.automationPoints(audio, "volume").size() >= 2);
        const QVariantList pts = c.automationPoints(audio, "volume");
        QVERIFY(std::abs(pts.last().toMap().value("value").toDouble() + 12.0) < 0.01);
        QVERIFY(pts.first().toMap().value("beats").toDouble() <= pts.last().toMap().value("beats").toDouble());
        c.stop();
        c.undo();                                                    // the take is one undo step
        QTRY_VERIFY(c.automationPoints(audio, "volume").size() < pts.size());
    }
    void aSummingStackRoutesTheSelectedTracksToANewAux() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        QSignalSpy notices(&c, &jad::ProjectController::notice);
        c.createSummingStack();                                  // nothing selected
        QVERIFY(notices.count() >= 1);
        const QString audio = firstAudioTrackId(c);
        c.selectTrack(audio, "replace");
        const int before = c.tracks()->totalCount();
        c.createSummingStack("Drum Stack");
        QTRY_COMPARE(c.tracks()->totalCount(), before + 1);
        QString bus;
        for (int i = 0; i < c.tracks()->rowCount(); ++i)
            if (c.tracks()->nameAt(i) == "Drum Stack") bus = c.tracks()->trackIdAt(i);
        QVERIFY(!bus.isEmpty());
        const jad::TrackRow* row = nullptr;
        for (int i = 0; i < c.mixer()->rowCount(); ++i) {
            const auto m = c.mixer()->data(c.mixer()->index(i), c.mixer()->roleNames().key("info")).toMap();
            if (m.value("trackId").toString() == audio) QTRY_COMPARE(m.value("outputName").toString(), QString("Drum Stack"));
        }
        Q_UNUSED(row);
        c.undo();                                                // the stack and the routing go in one step
        QTRY_COMPARE(c.tracks()->totalCount(), before);
    }
    void inputLabelsNameTheInputs() {
        jad::ProjectController c(false);
        QCOMPARE(c.inputLabel(3), QString("Input 3"));
        c.setInputLabel(3, "  Vocal mic ");
        QCOMPARE(c.inputLabel(3), QString("Vocal mic"));
        QCOMPARE(c.inputLabel(1), QString("Input 1"));
        c.setInputLabel(2, "Vocal mic");
        QCOMPARE(c.inputChoices().at(2), QString("Vocal mic"));     // listed in the Input menu (stereo, 1, 2...)
        QVERIFY(c.inputChoices().first().contains("Vocal mic"));    // and in the stereo entry
        c.setInputLabel(3, "");
        QCOMPARE(c.inputLabel(3), QString("Input 3"));
        QSettings s(QDir(QDir::tempPath()).filePath("jad-io-test.ini"), QSettings::IniFormat);
        c.setInputLabel(2, "Guitar");
        c.saveIoSettings(s);
        jad::ProjectController d(false);
        d.loadIoSettings(s);
        QCOMPARE(d.inputLabel(2), QString("Guitar"));
    }
    void bounceCanWriteAiffNormalizedAndOnlyTheCycleArea() {
        TempDir dir;
        TempDir other;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const auto aiff = other.path() / "mix.aif";
        c.bounceProjectAs(url(aiff), {{"format", "aiff16"}, {"normalize", true}, {"tail", 0.0}});
        QTRY_VERIFY_WITH_TIMEOUT(std::filesystem::exists(aiff) && std::filesystem::file_size(aiff) > 1000, 20000);
        const lpc::WavData d = lpc::decodeAudioFile(aiff);
        QCOMPARE(d.channels, 2);
        QVERIFY(std::abs(lpc::peakOf(d) - std::pow(10.0f, -0.3f / 20.0f)) < 0.01f);   // normalized to -0.3 dBFS
        QSignalSpy notices(&c, &jad::ProjectController::notice);
        c.bounceProjectAs(url(other.path() / "cycle.wav"), {{"range", "cycle"}});
        QVERIFY(!c.lastError().isEmpty());                                              // no cycle area set
        c.setLoopBeats(0, 2);
        c.setLoopBeats(0, 0);                                                           // the area stays when the cycle is off
        c.clearError();
        c.bounceProjectAs(url(other.path() / "cycle.wav"), {{"range", "cycle"}, {"tail", 0.0}});
        QTRY_VERIFY_WITH_TIMEOUT(std::filesystem::exists(other.path() / "cycle.wav"), 20000);
        const lpc::WavData cycle = lpc::decodeAudioFile(other.path() / "cycle.wav");
        QVERIFY(std::abs(cycle.frames() - 2 * 24000) < 600);                            // 2 beats at 120 bpm = 1 s
    }
    void closeRevertRecentAndRenameTheProject() {
        TempDir dir;
        jad::ProjectController c(false);
        QSignalSpy recent(&c, &jad::ProjectController::recentChanged);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() > 0);
        QVERIFY(recent.count() >= 1);
        QVERIFY(c.recentProjects().contains(c.projectFolder()));
        c.setProjectName("Renamed");
        QTRY_COMPARE(c.projectName(), QString("Renamed"));
        c.undo();
        QTRY_COMPARE(c.projectName(), QString("Demo"));
        c.setProjectName("   ");                              // empty: ignored
        c.addTrack("audio");
        const int tracks = c.tracks()->rowCount();
        QTRY_VERIFY(c.tracks()->totalCount() > 0);
        QTest::qWait(150);
        QVERIFY(c.revertToSaved());                             // the added track was never saved
        QTRY_VERIFY(c.tracks()->rowCount() <= tracks);
        const QString folder = c.projectFolder();
        c.closeProject();
        QVERIFY(!c.hasProject());
        QCOMPARE(c.tracks()->rowCount(), 0);
        QCOMPARE(c.regions()->rowCount(), 0);
        QVERIFY(c.openRecent(folder));
        QTRY_VERIFY(c.tracks()->rowCount() > 0);
        QVERIFY(!c.openRecent(folder + "/nowhere"));
        QSettings s(QDir(QDir::tempPath()).filePath("jad-recent-test.ini"), QSettings::IniFormat);
        c.saveRecent(s);
        jad::ProjectController d(false);
        d.loadRecent(s);
        QVERIFY(d.recentProjects().contains(folder));
    }
    void theUndoHistoryListsStepsAndCanBeSteppedAndDeleted() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() > 0);
        QVERIFY(c.undoHistory().value("undo").toStringList().isEmpty());
        const QString audio = firstAudioTrackId(c);
        c.setGain(audio, -3.0);
        c.setPan(audio, 0.5);
        QTest::qWait(150);
        QTRY_COMPARE(c.undoHistory().value("undo").toStringList().size(), 2);
        QCOMPARE(c.undoHistory().value("undo").toStringList().first(), QString("Channel strip"));
        c.undoSteps(2);
        QTRY_COMPARE(c.undoHistory().value("redo").toStringList().size(), 2);
        c.redoSteps(1);
        QTRY_COMPARE(c.undoHistory().value("undo").toStringList().size(), 1);
        c.clearUndoHistory();
        QVERIFY(c.undoHistory().value("undo").toStringList().isEmpty());
        QVERIFY(c.undoHistory().value("redo").toStringList().isEmpty());
    }
    void sortTracksByNameIsOneUndoStepAndColorsApplyToTheSelection() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        auto names = [&] {
            QStringList n;
            for (int i = 0; i < c.tracks()->rowCount(); ++i) n << c.tracks()->data(c.tracks()->index(i), c.tracks()->roleNames().key("name")).toString();
            return n;
        };
        const QStringList before = names();
        c.sortTracks("name");
        QStringList expected = before;
        std::stable_sort(expected.begin(), expected.end(), [](const QString& a, const QString& b) { return QString::localeAwareCompare(a.toLower(), b.toLower()) < 0; });
        if (expected != before) {
            QTRY_COMPARE(names(), expected);
            c.undo();
            QTRY_COMPARE(names(), before);
        }
        c.selectTrack(firstAudioTrackId(c), "replace");
        c.setSelectedTracksColor("red");
        QTRY_COMPARE(c.tracks()->find(firstAudioTrackId(c))->color, QString("red"));
    }
    void deleteAutomationClearsTheLanesOfTheSelectedTracksInOneStep() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = firstAudioTrackId(c);
        c.setAutomationPoints(audio, "volume", {QVariantMap{{"beats", 0.0}, {"value", -6.0}}, QVariantMap{{"beats", 4.0}, {"value", 0.0}}});
        QTRY_COMPARE(c.automationPoints(audio, "volume").size(), 2);
        c.selectTrack(audio, "replace");
        c.deleteAutomationOfSelected();
        QTRY_COMPARE(c.automationPoints(audio, "volume").size(), 0);
        c.undo();
        QTRY_COMPARE(c.automationPoints(audio, "volume").size(), 2);
        c.createTrackAutomation();
        QVERIFY(c.automationVisible());
    }
    void midiFileExportThenImportMakesNewInstrumentTracksWithTheSameNotes() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const int tracks = c.tracks()->rowCount();
        const QString mid = QString::fromStdWString(dir.path().wstring()) + "/out.mid";
        c.exportMidiFile(QUrl::fromLocalFile(mid));
        QVERIFY(QFileInfo::exists(mid));
        QVERIFY(QFileInfo(mid).size() > 30);
        c.importMidiFile(QUrl::fromLocalFile(mid));
        QTRY_VERIFY(c.tracks()->rowCount() > tracks);
        c.undo();                                                    // one undo step for the whole file
        QTRY_COMPARE(c.tracks()->rowCount(), tracks);
        c.importMidiFile(QUrl::fromLocalFile(mid + ".missing"));
        QVERIFY(!c.lastError().isEmpty());
    }
    void autoInputMonitoringIsAToggleThatIsKept() {
        jad::ProjectController c(false);
        QSignalSpy spy(&c, &jad::ProjectController::recordingChanged);
        const bool before = c.autoInputMonitoring();
        c.setAutoInputMonitoring(!before);
        QCOMPARE(c.autoInputMonitoring(), !before);
        QCOMPARE(spy.count(), 1);
        c.setAutoInputMonitoring(!before);
        QCOMPARE(spy.count(), 1);                                    // no change, no signal
        c.setAutoInputMonitoring(before);                            // put the stored choice back for the other tests
    }
    void projectAudioListsTheMediaAndTheNotesAreKeptWithTheProject() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QVariantList audio = c.projectAudio();
        QVERIFY(!audio.isEmpty());
        QVERIFY(audio.first().toMap().value("used").toInt() >= 1);
        QVERIFY(audio.first().toMap().value("seconds").toDouble() > 0);
        QCOMPARE(c.projectNotes(), QString());
        c.setProjectNotes("Verse 2: lower the guitars\nÀ demain");
        QCOMPARE(c.projectNotes(), QString("Verse 2: lower the guitars\nÀ demain"));
        const bool bar = c.controlBarVisible();
        c.setControlBarVisible(!bar);
        QCOMPARE(c.controlBarVisible(), !bar);
        c.setControlBarVisible(bar);
    }
    void controllerLanesOfAMidiRegionAreEditedPerLaneInOneStep() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3 && c.regions()->rowCount() >= 2);
        QString midiRegion;
        for (int i = 0; i < c.regions()->rowCount(); ++i) {
            const auto idx = c.regions()->index(i);
            if (!c.regions()->data(idx, c.regions()->roleNames().key("isAudio")).toBool()) midiRegion = c.regions()->data(idx, c.regions()->roleNames().key("regionId")).toString();
        }
        QVERIFY(!midiRegion.isEmpty());
        QVERIFY(c.regionControls(midiRegion, "cc64").isEmpty());
        c.setRegionControls(midiRegion, "cc64", {QVariantMap{{"beats", 0.0}, {"value", 127}}, QVariantMap{{"beats", 1.0}, {"value", 0}}});
        QTRY_COMPARE(c.regionControls(midiRegion, "cc64").size(), 2);
        c.setRegionControls(midiRegion, "bend", {QVariantMap{{"beats", 0.5}, {"value", -4096}}});
        QTRY_COMPARE(c.regionControls(midiRegion, "bend").size(), 1);
        QCOMPARE(c.regionControls(midiRegion, "bend").first().toMap().value("value").toInt(), -4096);
        QCOMPARE(c.regionControls(midiRegion, "cc64").size(), 2);          // the other lane stays
        c.setRegionControls(midiRegion, "cc64", {});
        QTRY_COMPARE(c.regionControls(midiRegion, "cc64").size(), 0);
        c.undo();
        QTRY_COMPARE(c.regionControls(midiRegion, "cc64").size(), 2);
        QVERIFY(c.regionControls(midiRegion, "nonsense").isEmpty());
    }
    void transposeVelocityAndQuantizeAreSetFromTheInspector() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3 && c.regions()->rowCount() >= 2);
        QString midiRegion;
        for (int i = 0; i < c.regions()->rowCount(); ++i) {
            const auto idx = c.regions()->index(i);
            if (!c.regions()->data(idx, c.regions()->roleNames().key("isAudio")).toBool()) midiRegion = c.regions()->data(idx, c.regions()->roleNames().key("regionId")).toString();
        }
        QVERIFY(!midiRegion.isEmpty());
        c.selectRegion(midiRegion, "replace");
        c.setSelectedRegionsMidi("transpose", 5);
        QTRY_COMPARE(c.regions()->find(midiRegion)->transpose, 5);
        c.setSelectedRegionsMidi("velocity", -20);
        QTRY_COMPARE(c.regions()->find(midiRegion)->velocityOffset, -20);
        c.setSelectedRegionsMidi("quantize", 0.5);
        QTRY_COMPARE(c.regions()->find(midiRegion)->quantizeBeats, 0.5);
        c.undo();
        QTRY_COMPARE(c.regions()->find(midiRegion)->quantizeBeats, 0.0);
        const QString track = c.regions()->find(midiRegion)->trackId;
        c.setTrackMidi(track, "transpose", -3);
        QTRY_COMPARE(c.tracks()->find(track)->transpose, -3);
        c.setTrackMidi(track, "keyLow", 60);
        QTRY_COMPARE(c.tracks()->find(track)->keyLow, 60);
        c.setTrackMidi(track, "keyHigh", 40);                        // below the low end: the low end follows
        QTRY_COMPARE(c.tracks()->find(track)->keyHigh, 40);
        QCOMPARE(c.tracks()->find(track)->keyLow, 40);
    }
    void takesOfOnePassageShareAGroupAndOnePlays() {
        TempDir dir;
        const auto proj = dir.path() / std::filesystem::path(u8"takes.lpc");
        std::filesystem::create_directories(proj);
        lpc::Project project = lpc::makeDemoProject(proj);
        lpc::Uuid trackId;
        for (lpc::Track& t : project.tracks) {
            if (t.kind != lpc::TrackKind::Audio || t.regions.empty()) continue;
            trackId = t.id;
            lpc::Region second = t.regions[0];
            second.id = lpc::Uuid::random();
            t.regions[0].takeGroup = second.takeGroup = "take-group-1";
            second.muted = true;
            t.regions.push_back(second);
            break;
        }
        QVERIFY(!trackId.isNull());
        lpc::saveProject(project, proj);
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(proj)));
        QTRY_VERIFY(c.regions()->rowCount() >= 3);
        QString first, second;
        for (int i = 0; i < c.regions()->rowCount(); ++i) {
            const jad::RegionRow& r = c.regions()->rows()[static_cast<std::size_t>(i)];
            if (r.takeGroup == "take-group-1") (first.isEmpty() ? first : second) = r.id;
        }
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        QCOMPARE(c.regionTakes(first).size(), 2);
        QCOMPARE(c.regions()->find(first)->muted, false);
        QCOMPARE(c.regions()->find(second)->muted, true);
        QCOMPARE(c.regions()->data(c.regions()->index(0), c.regions()->roleNames().key("takes")).isValid(), true);
        c.setActiveTake(second);                                      // the other take plays now
        QTRY_VERIFY(!c.regions()->find(second)->muted);
        QVERIFY(c.regions()->find(first)->muted);
        c.undo();
        QTRY_VERIFY(c.regions()->find(second)->muted);
        const int before = c.regions()->rowCount();
        c.deleteOtherTakes(first);
        QTRY_COMPARE(c.regions()->rowCount(), before - 1);
        QVERIFY(c.regions()->find(first)->takeGroup.isEmpty());
        QVERIFY(c.regionTakes(first).isEmpty());
    }
    void repeatMultipleLengthChangeAndTheTrackSearchList() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const int n = c.regions()->rowCount();
        const QString id = c.regions()->regionIdAt(0);
        const jad::RegionRow before = *c.regions()->find(id);
        c.selectRegion(id, "replace");
        c.repeatSelectedRegions(3);
        QTRY_COMPARE(c.regions()->rowCount(), n + 3);
        c.undo();                                                    // all the copies are one undo step
        QTRY_COMPARE(c.regions()->rowCount(), n);
        c.selectRegion(id, "replace");
        c.setSelectedRegionsLength(1.5);
        QTRY_VERIFY(std::abs(c.regions()->find(id)->lengthBeats - 1.5) < 1e-6);
        QVERIFY(std::abs(c.regions()->find(id)->startBeats - before.startBeats) < 1e-6);
        const QVariantList tracks = c.trackList();
        QVERIFY(tracks.size() >= 3);
        QVERIFY(!tracks.first().toMap().value("name").toString().isEmpty());
    }
    void bounceInPlaceRendersAnInstrumentTrackToANewAudioTrack() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3 && c.regions()->rowCount() >= 2);
        const int tracks = c.tracks()->rowCount(), regions = c.regions()->rowCount();
        QString instrument;
        for (int i = 0; i < c.tracks()->rowCount(); ++i)
            if (c.tracks()->kindAt(i) == "instrument") instrument = c.tracks()->trackIdAt(i);
        QVERIFY(!instrument.isEmpty());
        c.bounceInPlace();                                           // nothing selected: a notice, no work
        QCOMPARE(c.tracks()->rowCount(), tracks);
        c.selectTrack(instrument, "replace");
        c.bounceInPlace();
        QTRY_COMPARE_WITH_TIMEOUT(c.tracks()->rowCount(), tracks + 1, 20000);
        QTRY_COMPARE_WITH_TIMEOUT(c.regions()->rowCount(), regions + 1, 20000);
        bool found = false;
        for (int i = 0; i < c.tracks()->rowCount(); ++i)
            if (c.tracks()->kindAt(i) == "audio" && c.tracks()->nameAt(i).endsWith(" Bounce")) found = true;
        QVERIFY(found);
        QVERIFY(std::filesystem::exists(dir.path() / "d.lpc" / "audio"));
    }
    void partsOfTheBarsCanBeTurnedOffAndReset() {
        jad::ProjectController c(false);
        c.resetBarItems();
        QVERIFY(c.barItem("cb.lcd"));
        const int rev = c.barItemsRevision();
        QSignalSpy spy(&c, &jad::ProjectController::barsChanged);
        c.setBarItem("cb.lcd", false);
        QVERIFY(!c.barItem("cb.lcd"));
        QVERIFY(c.barItemsRevision() > rev);
        QCOMPARE(spy.count(), 1);
        c.setBarItem("cb.lcd", false);                               // no change, no signal
        QCOMPARE(spy.count(), 1);
        c.resetBarItems();
        QVERIFY(c.barItem("cb.lcd"));
    }
    void theBrowserListsFoldersFirstAndAudioFilesOnly() {
        TempDir dir;
        const auto root = dir.path();
        std::filesystem::create_directories(root / "Drums");
        std::filesystem::create_directories(root / ".hidden");
        { std::ofstream(root / "a.wav") << "x"; std::ofstream(root / "b.txt") << "x"; std::ofstream(root / "c.MP3") << "x"; }
        jad::ProjectController c(false);
        const QVariantMap listing = c.browseFolder(QString::fromStdU16String(root.u16string()));
        const QVariantList entries = listing.value("entries").toList();
        QCOMPARE(entries.size(), 3);                                  // Drums, a.wav, c.MP3: no hidden folder, no text file
        QVERIFY(entries[0].toMap().value("dir").toBool());
        QCOMPARE(entries[0].toMap().value("name").toString(), QString("Drums"));
        QVERIFY(entries[1].toMap().value("audio").toBool());
        QVERIFY(!listing.value("parent").toString().isEmpty());
        QVERIFY(!c.standardLocations().isEmpty());
        QVERIFY(c.browseFolder("/this/does/not/exist").value("entries").isValid());   // a missing folder shows the home folder instead
    }
    void noOverlapDragTrimsSplitsAndRemovesWhatLiesUnderTheMovedRegion() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.newProjectInTempForTest());
        c.addTrack("instrument");
        QTRY_COMPARE(c.tracks()->rowCount(), 1);
        const QString track = c.tracks()->trackIdAt(0);
        c.createRegion(track, 0, 16);
        QTRY_COMPARE(c.regions()->rowCount(), 1);
        const QString big = c.regions()->regionIdAt(0);
        c.createRegion(track, 20, 4);
        QTRY_COMPARE(c.regions()->rowCount(), 2);
        QString small;
        for (int i = 0; i < 2; ++i) if (c.regions()->regionIdAt(i) != big) small = c.regions()->regionIdAt(i);
        QCOMPARE(c.dragMode(), QString("overlap"));
        c.moveRegion(small, 4);                                      // Overlap: nothing is touched
        QTRY_COMPARE(c.regions()->find(small)->startBeats, 4.0);
        QCOMPARE(c.regions()->rowCount(), 2);
        QCOMPARE(c.regions()->find(big)->lengthBeats, 16.0);
        c.undo();
        QTRY_COMPARE(c.regions()->find(small)->startBeats, 20.0);
        c.setDragMode("noOverlap");
        c.moveRegion(small, 4);                                      // inside the big one: it is cut in two around the moved region
        QTRY_COMPARE(c.regions()->rowCount(), 3);
        QCOMPARE(c.regions()->find(big)->lengthBeats, 4.0);          // [0, 4)
        double rightStart = -1;
        for (const jad::RegionRow& r : c.regions()->rows()) if (r.id != big && r.id != small) rightStart = r.startBeats;
        QCOMPARE(rightStart, 8.0);                                   // [8, 16)
        c.undo();                                                    // all of it is one undo step
        QTRY_COMPARE(c.regions()->rowCount(), 2);
        QCOMPARE(c.regions()->find(big)->lengthBeats, 16.0);
        c.moveRegion(big, 18);                                       // the big one lands on the small one (20..24): the small one is trimmed to its end
        QTRY_COMPARE(c.regions()->find(big)->startBeats, 18.0);
        QVERIFY(c.regions()->find(small) == nullptr || c.regions()->find(small)->startBeats >= 34.0 || c.regions()->find(small)->lengthBeats < 4.0);
        c.setDragMode("overlap");
    }
    void hiddenTracksLeaveTheTracksAreaUntilShownAndTheirRegionsGoWithThem() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3 && c.regions()->rowCount() >= 2);
        const int tracks = c.tracks()->rowCount(), regions = c.regions()->rowCount();
        const QString audio = firstAudioTrackId(c);
        c.selectTrack(audio, "replace");
        c.hideSelectedTracks();
        QTRY_COMPARE(c.tracks()->rowCount(), tracks - 1);
        QVERIFY(c.regions()->rowCount() < regions);                 // its region is not drawn on another row
        c.setShowHiddenTracks(true);                                 // Toggle Hide View
        QTRY_COMPARE(c.tracks()->rowCount(), tracks);
        QTRY_COMPARE(c.regions()->rowCount(), regions);
        QVERIFY(c.tracks()->data(c.tracks()->index(0), c.tracks()->roleNames().key("hidden")).isValid());
        c.setShowHiddenTracks(false);
        QTRY_COMPARE(c.tracks()->rowCount(), tracks - 1);
        c.unhideAllTracks();
        QTRY_COMPARE(c.tracks()->rowCount(), tracks);
        QTRY_COMPARE(c.regions()->rowCount(), regions);
        c.selectTrack(audio, "replace");
        c.hideUnselectedTracks();                                    // only the selected one stays
        QTRY_COMPARE(c.tracks()->rowCount(), 1);
        c.undo();
        QTRY_COMPARE(c.tracks()->rowCount(), tracks);                // one undo step
    }
    void moveToPlayheadPutsTheFirstSelectedRegionAtThePlayhead() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const QString id = c.regions()->regionIdAt(0);
        c.selectRegion(id, "replace");
        c.locateBeats(30.0);
        QTRY_VERIFY(std::abs(c.positionBeats() - 30.0) < 0.01);
        c.moveSelectedToPlayhead();
        QTRY_VERIFY(std::abs(c.regions()->find(id)->startBeats - 30.0) < 0.01);
    }
    void splitAtLocatorsCutsTheRegionsThatCrossThem() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const jad::RegionRow first = *c.regions()->find(c.regions()->regionIdAt(0));
        const int n = c.regions()->rowCount();
        c.selectRegion(first.id, "replace");
        c.setLoopRange(first.startBeats + 1, first.startBeats + 2);
        c.splitAtLocators();
        QTRY_COMPARE(c.regions()->rowCount(), n + 2);  // cut at both locators: three pieces
    }
    void selectInsideLocatorsAndSimilarRegions() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const jad::RegionRow first = *c.regions()->find(c.regions()->regionIdAt(0));
        c.setLoopRange(first.startBeats - 0.5, first.startBeats + first.lengthBeats + 0.5);
        c.selectInsideLocators();
        QVERIFY(c.selectedRegionIds().contains(first.id));
        c.selectRegion(first.id, "replace");
        c.selectSimilarRegions();
        QVERIFY(c.selectedRegionIds().contains(first.id));
        c.selectEqualRegions();
        QVERIFY(c.selectedRegionIds().contains(first.id));
    }
    void deleteAndMoveClosesTheGap() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const QString id = c.regions()->regionIdAt(0);
        const jad::RegionRow gone = *c.regions()->find(id);
        // the first region on its track that starts after it, if any
        const jad::RegionRow* later = nullptr;
        for (int i = 0; i < c.regions()->rowCount(); ++i) {
            const jad::RegionRow* r = c.regions()->find(c.regions()->regionIdAt(i));
            if (r && r->id != id && r->trackId == gone.trackId && r->startBeats >= gone.startBeats + gone.lengthBeats - 1e-6) later = r;
        }
        const int n = c.regions()->rowCount();
        c.selectRegion(id, "replace");
        c.deleteSelectedAndMove();
        QTRY_COMPARE(c.regions()->rowCount(), n - 1);
        if (later) {
            const QString laterId = later->id;
            const double was = later->startBeats;
            QTRY_VERIFY(std::abs(c.regions()->find(laterId)->startBeats - (was - gone.lengthBeats)) < 0.01);
        }
        c.undo();
        QTRY_COMPARE(c.regions()->rowCount(), n);
    }
    void muteAllSwitchesEveryStripInTheSameState() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.mixer()->rowCount() > 1);
        c.muteAll(true);
        const int mute = c.mixer()->roleNames().key("mute");
        QTRY_VERIFY([&] {
            for (int i = 0; i < c.mixer()->rowCount(); ++i)
                if (!c.mixer()->data(c.mixer()->index(i), mute).toBool()) return false;
            return true;
        }());
    }
    void settingTheNotesOfAMidiRegionIsOneUndoStep() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        QString midi;
        for (int i = 0; i < c.regions()->rowCount(); ++i) {
            const QString id = c.regions()->regionIdAt(i);
            if (!c.regions()->find(id)->audio) midi = id;
        }
        QVERIFY(!midi.isEmpty());
        const QVariantList before = c.regionNotes(midi);
        QVariantList next;
        next.append(QVariantMap{{"start", 0.5}, {"length", 1.5}, {"note", 64}, {"velocity", 90}});
        c.setRegionNotes(midi, next);
        QTRY_COMPARE(c.regionNotes(midi).size(), 1);
        const QVariantMap n = c.regionNotes(midi).first().toMap();
        QCOMPARE(n.value("note").toInt(), 64);
        QCOMPARE(n.value("velocity").toInt(), 90);
        QVERIFY(std::abs(n.value("start").toDouble() - 0.5) < 1e-6);
        QVERIFY(std::abs(n.value("length").toDouble() - 1.5) < 1e-6);
        c.undo();
        QTRY_COMPARE(c.regionNotes(midi).size(), before.size());
        const QVariantMap info = c.regionInfo(midi);
        QVERIFY(info.value("found").toBool());
        QVERIFY(!info.value("audio").toBool());
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
    void applyingAPatchIsOneUndoStep() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        c.selectTrack(trackIdOfKind(c, "audio"), "replace");
        c.applyPatch("audio.bright-vocal");
        QTRY_COMPARE(c.inspector()->track().value("patchId").toString(), QStringLiteral("audio.bright-vocal"));
        QCOMPARE(c.inspector()->track().value("gainDb").toDouble(), -2.0);
        QCOMPARE(c.inspector()->track().value("inserts").toList().size(), 1);
        QCOMPARE(c.inspector()->track().value("patchName").toString(), QStringLiteral("Bright Vocal"));
        QCOMPARE(c.library()->currentPatchId(), QStringLiteral("audio.bright-vocal"));
        c.undo();
        QTRY_COMPARE(c.inspector()->track().value("patchId").toString(), QString());
        QCOMPARE(c.inspector()->track().value("inserts").toList().size(), 0);
    }
    void aPatchOfAnotherKindOrNoTrackGivesANoticeAndChangesNothing() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        QSignalSpy notices(&c, &jad::ProjectController::notice);
        c.applyPatch("audio.bright-vocal");  // nothing selected
        QCOMPARE(notices.count(), 1);
        c.selectTrack(trackIdOfKind(c, "instrument"), "replace");
        c.applyPatch("audio.bright-vocal");  // an audio patch on an instrument track
        QCOMPARE(notices.count(), 2);
        c.applyPatch("gone.patch");
        QCOMPARE(notices.count(), 3);
        QVERIFY(c.inspector()->track().value("patchId").toString().isEmpty());
    }
    void revertReappliesThePatchOfTheTrack() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio");
        c.selectTrack(audio, "replace");
        c.applyPatch("audio.warm-guitar");
        QTRY_COMPARE(c.inspector()->track().value("gainDb").toDouble(), -4.0);
        c.setGain(audio, 3.0);
        QTRY_COMPARE(c.inspector()->track().value("gainDb").toDouble(), 3.0);
        c.revertPatch();
        QTRY_COMPARE(c.inspector()->track().value("gainDb").toDouble(), -4.0);
    }
    void aSmartControlMovesEveryTargetAndIsOneUndoStep() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio");
        c.selectTrack(audio, "replace");
        c.applyPatch("audio.bright-vocal");
        QTRY_COMPARE(c.inspector()->smartControls().size(), 3);
        c.setSmartControl(audio, "boost", 1.0);
        QTRY_COMPARE(c.inspector()->track().value("gainDb").toDouble(), -5.0);
        QCOMPARE(c.inspector()->track().value("inserts").toList().first().toMap().value("gainDb").toDouble(), 12.0);
        double boost = 0;
        for (const QVariant& s : c.inspector()->smartControls())
            if (s.toMap().value("id").toString() == "boost") boost = s.toMap().value("value").toDouble();
        QCOMPARE(boost, 1.0);  // the knob reads its value back from the track
        c.undo();
        QTRY_COMPARE(c.inspector()->track().value("gainDb").toDouble(), -2.0);
        c.setSmartControl(audio, "nope", 1.0);              // unknown control: ignored
        c.setSmartControl(audio, "boost", std::nan(""));     // NaN: ignored
        c.setSmartControl("not-a-track", "boost", 1.0);      // stale track: ignored
        QTest::qWait(100);
        QCOMPARE(c.inspector()->track().value("gainDb").toDouble(), -2.0);
    }
    void pluginInsertsGoThroughCommandsAndKeepTheirLabel() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio");
        c.selectTrack(audio, "replace");
        c.addPlugin(audio, "vst3:00112233445566778899aabbccddeeff", "Verb");
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 1);
        const QVariantMap insert = c.inspector()->track().value("inserts").toList().first().toMap();
        QCOMPARE(insert.value("plugin").toBool(), true);
        QCOMPARE(insert.value("label").toString(), QString("Verb"));
        c.setInsertState(audio, 0, "AAAA");  // undoable like any command
        c.undo();
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 1);
        c.removeInsert(audio, 0);
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 0);
    }
    void insertsSendsOutputAndRegionGainGoThroughCommands() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio");
        const QString bus = trackIdOfKind(c, "bus");
        c.selectTrack(audio, "replace");
        c.addInsert(audio, "builtin.gain");
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 1);
        c.setInsertParam(audio, 0, "gainDb", 4.5);
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().first().toMap().value("gainDb").toDouble(), 4.5);
        c.removeInsert(audio, 0);
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 0);
        c.addSend(audio, bus);
        QTRY_COMPARE(c.inspector()->track().value("sends").toList().size(), 1);
        const QString sendId = c.inspector()->track().value("sends").toList().first().toMap().value("id").toString();
        c.setSendLevel(sendId, -20.0);
        QTRY_COMPARE(c.inspector()->track().value("sends").toList().first().toMap().value("levelDb").toDouble(), -20.0);
        c.setSendPreFader(sendId, true);
        QTRY_VERIFY(c.inspector()->track().value("sends").toList().first().toMap().value("preFader").toBool());
        c.removeSend(sendId);
        QTRY_COMPARE(c.inspector()->track().value("sends").toList().size(), 0);
        c.setOutput(audio, bus);
        QTRY_COMPARE(c.inspector()->output().value("trackId").toString(), bus);
        c.setOutput(audio, QString());
        QTRY_VERIFY(c.inspector()->output().value("master").toBool());
        c.createRegion(trackIdOfKind(c, "instrument"), 0.0, 4.0);
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const QString region = c.regions()->regionIdAt(c.regions()->rowCount() - 1);
        c.selectRegion(region, "replace");
        c.setRegionGain(region, -6.0);
        QTRY_COMPARE(c.inspector()->region().value("gainDb").toDouble(), -6.0);
    }
    void aHiddenBusLeavesTheTracksAreaButNotTheMixer() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const int shown = c.tracks()->rowCount(), all = c.mixer()->rowCount();
        const QString bus = trackIdOfKind(c, "bus");
        c.setShowInTracks(bus, false);
        QTRY_COMPARE(c.tracks()->rowCount(), shown - 1);
        QCOMPARE(c.mixer()->rowCount(), all);
        QCOMPARE(c.tracks()->totalCount(), shown);
        QVERIFY(!trackField(c, bus, "trackId").isValid());  // not a row of the Tracks area
        c.selectTrack(bus, "replace");                        // but still selectable: the controller finds it
        QCOMPARE(c.selectedTrackIds(), QStringList{bus});
        c.undo();
        QTRY_COMPARE(c.tracks()->rowCount(), shown);
    }
    void regionRowsFollowTheShownTracksOnly() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        c.setShowInTracks(trackIdOfKind(c, "bus"), false);
        QTRY_VERIFY(c.tracks()->totalCount() > c.tracks()->rowCount());
        const auto roles = c.regions()->roleNames();
        for (int i = 0; i < c.regions()->rowCount(); ++i) {
            const QString track = c.regions()->data(c.regions()->index(i), roles.key("trackId")).toString();
            const int row = c.regions()->data(c.regions()->index(i), roles.key("trackIndex")).toInt();
            QCOMPARE(c.tracks()->trackIdAt(row), track);  // the timeline row and the header row are the same track
        }
    }
    void theTrackMenuStateFollowsTheSelectedTrack() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        c.selectTrack(trackIdOfKind(c, "audio"), "replace");
        QVERIFY(c.selectedCanHide());   // any track but the master
        QVERIFY(c.selectedShowInTracks());
        const QString bus = trackIdOfKind(c, "bus");
        c.selectTrack(bus, "replace");
        QVERIFY(c.selectedCanHide());
        c.setShowInTracks(bus, false);
        QTRY_VERIFY(!c.selectedShowInTracks());   // still selected, now hidden
        QVERIFY(c.selectedCanHide());
    }
    void newBusForASendIsOneUndoStep() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio");
        const int shown = c.tracks()->rowCount(), all = c.mixer()->rowCount();
        c.selectTrack(audio, "replace");
        QTRY_VERIFY(c.inspector()->track().value("sends").toList().isEmpty());
        QSignalSpy sent(&c, &jad::ProjectController::commandSent);
        c.newBusFor(audio, "send");
        QTRY_COMPARE(c.mixer()->rowCount(), all + 1);
        QCOMPARE(sent.count(), 1);
        QCOMPARE(c.tracks()->rowCount(), shown);                                           // hidden from the Tracks area
        QTRY_VERIFY(c.selectedTrackIds() != QStringList{audio});                           // the new bus is selected
        c.selectTrack(audio, "replace");
        QTRY_COMPARE(c.inspector()->track().value("sends").toList().size(), 1);             // and the track is routed to it
        c.undo();
        QTRY_COMPARE(c.mixer()->rowCount(), all);
        QTRY_COMPARE(c.inspector()->track().value("sends").toList().size(), 0);             // one undo removes both
    }
    void newBusForTheOutputKeepsOneUndoStepAndPicksAFreeName() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio");
        const int all = c.mixer()->rowCount();
        c.selectTrack(audio, "replace");
        c.newBusFor(audio, "output");
        QTRY_COMPARE(c.mixer()->rowCount(), all + 1);
        c.selectTrack(audio, "replace");
        QTRY_VERIFY(!c.inspector()->output().value("master").toBool());
        c.newBusFor(audio, "output");
        QTRY_COMPARE(c.mixer()->rowCount(), all + 2);
        QStringList names;
        const auto roles = c.mixer()->roleNames();
        for (int i = 0; i < c.mixer()->rowCount(); ++i) names << c.mixer()->data(c.mixer()->index(i), roles.key("name")).toString();
        QVERIFY(names.contains("Bus 1"));
        QVERIFY(names.contains("Bus 2"));
        c.undo();
        QTRY_COMPARE(c.mixer()->rowCount(), all + 1);
        c.selectTrack(audio, "replace");
        QTRY_VERIFY(!c.inspector()->output().value("master").toBool());                    // the first bus is still the output
        c.newBusFor(audio, "nonsense");                                                    // unknown role: nothing happens
        QCOMPARE(c.mixer()->rowCount(), all + 1);
    }
    void startPluginsDoesNothingWithoutAudio() {
        jad::ProjectController c(false);
        c.startPlugins();                       // audio output is off: no plug-in hosting, no scanner
        QVERIFY(!c.plugins()->supported());
        c.startPlugins();                       // and asking again is harmless
        QVERIFY(!c.plugins()->supported());
    }
    void moveInsertIsOneUndoStepInsideATrackAndAcrossTracks() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio"), instrument = trackIdOfKind(c, "instrument");
        c.selectTrack(audio, "replace");
        c.addInsert(audio, "builtin.gain");
        c.addInsert(audio, "builtin.gain");
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 2);
        c.setInsertParam(audio, 0, "gainDb", 5.0);
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().at(0).toMap().value("gainDb").toDouble(), 5.0);
        QSignalSpy sent(&c, &jad::ProjectController::commandSent);
        c.moveInsert(audio, 0, 1);
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().at(1).toMap().value("gainDb").toDouble(), 5.0);
        QCOMPARE(sent.count(), 1);
        c.undo();
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().at(0).toMap().value("gainDb").toDouble(), 5.0);
        c.moveInsert(audio, 0, 0, instrument);  // to another track
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 1);
        c.selectTrack(instrument, "replace");
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 1);
        QCOMPARE(c.inspector()->track().value("inserts").toList().at(0).toMap().value("gainDb").toDouble(), 5.0);
        c.undo();
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 0);
        c.moveInsert(audio, 0, 7);  // out of range: rejected, the error is shown, nothing changes
        QTRY_VERIFY(c.lastError().startsWith("bad_index"));
    }
    void insertBypassIsOneUndoStepAndShownInTheRow() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio");
        c.selectTrack(audio, "replace");
        c.addInsert(audio, "builtin.gain");
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 1);
        QVERIFY(!c.inspector()->track().value("inserts").toList().at(0).toMap().value("bypass").toBool());
        c.setInsertBypass(audio, 0, true);
        QTRY_VERIFY(c.inspector()->track().value("inserts").toList().at(0).toMap().value("bypass").toBool());
        c.undo();
        QTRY_VERIFY(!c.inspector()->track().value("inserts").toList().at(0).toMap().value("bypass").toBool());
    }
    void showBusPinsTheRightStripUntilTheSelectionChanges() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio"), bus = trackIdOfKind(c, "bus"), instrument = trackIdOfKind(c, "instrument");
        c.selectTrack(audio, "replace");
        QTRY_VERIFY(!c.inspector()->output().isEmpty());
        QVERIFY(!c.inspector()->pinned());
        c.showBus(bus);
        QTRY_VERIFY(c.inspector()->pinned());
        QCOMPARE(c.inspector()->output().value("trackId").toString(), bus);
        c.selectTrack(instrument, "replace");                      // another track: the pin goes
        QTRY_VERIFY(!c.inspector()->pinned());
        c.showBus(bus);
        QTRY_VERIFY(c.inspector()->pinned());
        c.showBus(c.inspector()->track().value("outputId").toString());  // the output of the shown track: back to normal
        QTRY_VERIFY(!c.inspector()->pinned());
        c.showBus(bus);
        QTRY_VERIFY(c.inspector()->pinned());
        c.selectTrack(bus, "replace");                             // the pinned bus becomes the shown track
        c.selectTrack(audio, "replace");
        QTRY_VERIFY(!c.inspector()->pinned());
    }
    void aPinnedBusThatIsDeletedUnpins() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio");
        c.addTrack("bus");
        QTRY_VERIFY(c.mixer()->rowCount() >= 5);
        const auto roles = c.mixer()->roleNames();
        QString fresh;
        for (int i = 0; i < c.mixer()->rowCount(); ++i)
            if (c.mixer()->data(c.mixer()->index(i), roles.key("name")).toString().startsWith("Bus ")) fresh = c.mixer()->data(c.mixer()->index(i), roles.key("trackId")).toString();
        QVERIFY(!fresh.isEmpty());
        c.selectTrack(audio, "replace");
        c.showBus(fresh);
        QTRY_VERIFY(c.inspector()->pinned());
        c.undo();                                                  // the bus goes away
        QTRY_VERIFY(!c.inspector()->pinned());
    }
    void targetsForLeavesOutWhatTheCoreRefuses() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio"), bus = trackIdOfKind(c, "bus");
        const auto ids = [&](const QString& track) {
            QStringList out;
            for (const QVariant& v : c.targetsFor(track)) out << v.toMap().value("id").toString();
            return out;
        };
        QVERIFY(ids(audio).contains(bus));
        QVERIFY(!ids(bus).contains(bus));              // not itself
        c.addTrack("bus");
        QTRY_VERIFY(ids(audio).size() == 2);
        QString other;
        for (const QString& id : ids(audio))
            if (id != bus) other = id;
        const int revision = c.routingRevision();
        c.setOutput(other, bus);                       // other -> bus
        QTRY_VERIFY(c.routingRevision() != revision);
        QVERIFY(!ids(bus).contains(other));            // bus would loop through its own output
        QVERIFY(ids(other).contains(bus));             // the other direction is fine
        QVERIFY(c.targetsFor("no-such-track").isEmpty());
    }
    void revertSaysSoWhenThePatchLeftTheCatalogue() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio");
        c.selectTrack(audio, "replace");
        c.submit(QStringLiteral(R"({"type":"set_patch_id","trackId":"%1","patchId":"gone.patch"})").arg(audio));
        QTRY_COMPARE(c.inspector()->track().value("patchId").toString(), QStringLiteral("gone.patch"));
        QSignalSpy notices(&c, &jad::ProjectController::notice);
        c.revertPatch();
        QCOMPARE(notices.count(), 1);
        QVERIFY(notices.at(0).at(0).toString().contains("'gone.patch'"));
        QVERIFY(notices.at(0).at(0).toString().contains("no longer in the catalogue"));
        QCOMPARE(c.inspector()->track().value("patchId").toString(), QStringLiteral("gone.patch"));  // nothing changed
    }
    void theInspectorGoesNeutralWhenItsTrackIsDeleted() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const int before = c.tracks()->rowCount();
        c.addTrack("audio");
        QTRY_COMPARE(c.tracks()->rowCount(), before + 1);
        const QString last = c.tracks()->trackIdAt(c.tracks()->rowCount() - 1);
        c.selectTrack(last, "replace");
        QTRY_VERIFY(c.inspector()->hasTrack());
        c.deleteSelectedTracks();
        QTRY_VERIFY(!c.inspector()->hasTrack());
        QVERIFY(c.library()->categories().isEmpty());
    }
    void panelStateHasDefaultsAndClampsSizes() {
        jad::ProjectController c(false);
        QVERIFY(c.inspectorVisible());
        QVERIFY(!c.libraryVisible());
        QVERIFY(!c.smartControlsVisible());
        QSignalSpy spy(&c, &jad::ProjectController::panelsChanged);
        c.setLibraryVisible(true);
        c.setLeftColumnWidth(5000);
        QCOMPARE(c.leftColumnWidth(), 320.0);
        c.setLeftColumnWidth(10);
        QCOMPARE(c.leftColumnWidth(), 200.0);
        c.setLeftColumnWidth(std::nan(""));
        QCOMPARE(c.leftColumnWidth(), 200.0);
        c.setSmartControlsHeight(1000);
        QCOMPARE(c.smartControlsHeight(), 320.0);
        c.setSmartControlsHeight(0);
        QCOMPARE(c.smartControlsHeight(), 120.0);
        QVERIFY(spy.count() >= 4);
    }
    void panelLayoutSurvivesARestart() {
        QTemporaryDir dir;
        const QString ini = dir.filePath("jad.ini");
        {
            jad::ProjectController c(false);
            c.setLibraryVisible(true);
            c.setInspectorVisible(false);
            c.setSmartControlsVisible(true);
            c.setLeftColumnWidth(300);
            c.setSmartControlsHeight(250);
            QSettings s(ini, QSettings::IniFormat);
            c.savePanelState(s);
        }
        jad::ProjectController d(false);
        QSettings s(ini, QSettings::IniFormat);
        d.loadPanelState(s);
        QVERIFY(d.libraryVisible());
        QVERIFY(!d.inspectorVisible());
        QVERIFY(d.smartControlsVisible());
        QCOMPARE(d.leftColumnWidth(), 300.0);
        QCOMPARE(d.smartControlsHeight(), 250.0);
        // nothing stored: the defaults stay
        QTemporaryDir empty;
        QSettings none(empty.filePath("x.ini"), QSettings::IniFormat);
        jad::ProjectController e(false);
        e.loadPanelState(none);
        QVERIFY(e.inspectorVisible());
        QVERIFY(!e.libraryVisible());
        QCOMPARE(e.leftColumnWidth(), 240.0);
    }
    void announceStubEmitsANotice() {
        jad::ProjectController c(false);
        QSignalSpy spy(&c, &jad::ProjectController::notice);
        c.announceStub("Quantize");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QStringLiteral("Quantize: not implemented yet"));
    }
    void theCatalogueLoadsFromTheResources() {
        jad::ProjectController c(false);
        QVERIFY(c.library()->problems().isEmpty());
        QVERIFY(c.library()->patchCount() >= 8);
    }
};

QTEST_MAIN(BridgeTest)
#include "tst_bridge.moc"
