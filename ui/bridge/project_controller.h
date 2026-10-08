#pragma once
#include <QList>
#include <QObject>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <deque>
#include <functional>
#include <filesystem>
#include <memory>
#include <thread>

#include <nlohmann/json_fwd.hpp>

#include "bridge/mixer_model.h"
#include "bridge/region_model.h"
#include "bridge/snapshot.h"
#include "bridge/track_list_model.h"
#include "bridge/waveform_cache.h"

namespace lpc {
class MediaStore;
class ProjectHost;
class IAudioDevice;
class IAudioCallback;
namespace audio {
class AudioEngine;
}
}  // namespace lpc

namespace jad {

struct JuceInit;
class EngineDriver;

// The only door between QML and the Core. Owns the engine, media store, project host and the audio device.
// QML never changes data directly: it sends JSON commands (submit and the helpers built on it) and shows the
// models, which are rebuilt from a snapshot after every accepted change.
class ProjectController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY projectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(double bpm READ bpm NOTIFY projectChanged)
    Q_PROPERTY(int beatsPerBar READ beatsPerBar NOTIFY projectChanged)
    Q_PROPERTY(double barBeats READ barBeats NOTIFY projectChanged)  // quarter-note beats in a bar: numerator * 4 / denominator
    Q_PROPERTY(QString signatureText READ signatureText NOTIFY projectChanged)
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QString snap READ snap WRITE setSnap NOTIFY snapChanged)
    Q_PROPERTY(double snapBeats READ snapBeats NOTIFY snapChanged)
    Q_PROPERTY(bool followPlayhead READ followPlayhead WRITE setFollowPlayhead NOTIFY followPlayheadChanged)
    Q_PROPERTY(QStringList selectedTrackIds READ selectedTrackIds NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedRegionIds READ selectedRegionIds NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedRecordArm READ selectedRecordArm NOTIFY trackTogglesChanged)  // of the first selected track
    Q_PROPERTY(bool selectedInputMonitor READ selectedInputMonitor NOTIFY trackTogglesChanged)
    Q_PROPERTY(int trackHeightIndex READ trackHeightIndex WRITE setTrackHeightIndex NOTIFY trackHeightChanged)
    Q_PROPERTY(double masterGainDb READ masterGainDb NOTIFY projectChanged)
    Q_PROPERTY(bool mixerVisible READ mixerVisible WRITE setMixerVisible NOTIFY mixerVisibleChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double positionSeconds READ positionSeconds NOTIFY positionChanged)
    Q_PROPERTY(double positionBeats READ positionBeats NOTIFY positionChanged)
    Q_PROPERTY(bool loopEnabled READ loopEnabled NOTIFY loopChanged)
    Q_PROPERTY(bool degraded READ degraded NOTIFY degradedChanged)
    Q_PROPERTY(QString deviceError READ deviceError NOTIFY deviceErrorChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool audioEnabled READ audioEnabled WRITE setAudioEnabled NOTIFY audioEnabledChanged)
    Q_PROPERTY(double masterPeak READ masterPeak NOTIFY peakChanged)
    Q_PROPERTY(jad::TrackListModel* tracks READ tracks CONSTANT)
    Q_PROPERTY(jad::RegionModel* regions READ regions CONSTANT)
    Q_PROPERTY(jad::MixerModel* mixer READ mixer CONSTANT)

public:
    explicit ProjectController(QObject* parent = nullptr);
    explicit ProjectController(bool openAudioDevice, QObject* parent = nullptr);
    ~ProjectController() override;

    bool hasProject() const { return host_ != nullptr; }
    bool selectedRecordArm() const { return selectedToggle(QStringLiteral("track.recordArm")); }
    bool selectedInputMonitor() const { return selectedToggle(QStringLiteral("track.inputMonitor")); }
    QString projectName() const { return name_; }
    double bpm() const { return bpm_; }
    int beatsPerBar() const { return beatsPerBar_; }
    double barBeats() const { return beatsPerBar_ * 4.0 / beatUnit_; }
    QString signatureText() const { return QStringLiteral("%1/%2").arg(beatsPerBar_).arg(beatUnit_); }
    double masterGainDb() const { return masterGain_; }
    bool mixerVisible() const { return mixerVisible_; }
    QString tool() const { return tool_; }
    void setTool(const QString& tool);  // pointer, pencil, eraser, scissors or glue; anything else is ignored
    QString snap() const { return snap_; }
    void setSnap(const QString& snap);  // off, bar, half, quarter, eighth or sixteenth; anything else is ignored
    double snapBeats() const;           // the grid in beats (quarter notes); 0 when snapping is off
    bool followPlayhead() const { return followPlayhead_; }
    void setFollowPlayhead(bool on) {
        if (on == followPlayhead_) return;
        followPlayhead_ = on;
        emit followPlayheadChanged();
    }
    QStringList selectedTrackIds() const { return selectedTracks_; }
    QStringList selectedRegionIds() const { return selectedRegions_; }
    int trackHeightIndex() const { return trackHeightIndex_; }
    void setTrackHeightIndex(int index) {
        const int clamped = index < 0 ? 0 : (index > 3 ? 3 : index);
        if (clamped == trackHeightIndex_) return;
        trackHeightIndex_ = clamped;
        emit trackHeightChanged();
    }
    void setMixerVisible(bool visible) {
        if (visible == mixerVisible_) return;
        mixerVisible_ = visible;
        emit mixerVisibleChanged();
    }
    bool playing() const { return playing_; }
    double positionSeconds() const { return positionSeconds_; }
    double positionBeats() const { return positionBeats_; }
    bool loopEnabled() const { return loop_; }
    bool degraded() const;
    QString deviceError() const { return deviceError_; }
    QString lastError() const { return lastError_; }
    double masterPeak() const { return peak_; }
    bool audioEnabled() const { return openAudioDevice_; }
    void setAudioEnabled(bool enabled) {
        if (enabled == openAudioDevice_) return;
        openAudioDevice_ = enabled;
        emit audioEnabledChanged();
    }
    TrackListModel* tracks() { return &tracks_; }
    RegionModel* regions() { return &regions_; }
    MixerModel* mixer() { return &mixer_; }

    Q_INVOKABLE bool openProject(const QUrl& folder);
    Q_INVOKABLE bool newProject(const QUrl& folder);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE void submit(const QString& commandJson);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void locateBeats(double beats);
    Q_INVOKABLE void locateSeconds(double seconds);
    Q_INVOKABLE void setLoopBeats(double startBeats, double endBeats);
    Q_INVOKABLE void clearError();
    // The LCD: tempo (clamped to 20..999, NaN ignored) and time signature at tick 0, master volume, bar steps.
    Q_INVOKABLE void setTempo(double bpm);
    Q_INVOKABLE void setSignature(int numerator, int denominator);
    Q_INVOKABLE void setMasterGain(double db);
    Q_INVOKABLE void barBack();
    Q_INVOKABLE void barForward();
    // Selection lives here so that menus, shortcuts and the views agree. `mode` is "replace", "extend" or "toggle";
    // ids that do not exist are ignored. Selecting regions does not touch the track selection and vice versa.
    Q_INVOKABLE void selectTrack(const QString& id, const QString& mode);
    Q_INVOKABLE void selectRegion(const QString& id, const QString& mode);
    Q_INVOKABLE void selectRegions(const QStringList& ids, const QString& mode);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectAll();  // every region
    // Track management: one command each (deleting several tracks is one transaction, one undo step).
    Q_INVOKABLE void addTrack(const QString& kind);  // "audio", "instrument" or "bus"
    Q_INVOKABLE void deleteSelectedTracks();
    Q_INVOKABLE void renameTrack(const QString& trackId, const QString& name);
    Q_INVOKABLE void setTrackColor(const QString& trackId, const QString& color);
    Q_INVOKABLE void toggleMuteSelected();
    // Region tools. Positions are in beats, snapped to the grid and clamped before a command is built; the Core's
    // refusals (a split outside the region, a join of regions that do not touch) show up in lastError.
    Q_INVOKABLE void createRegion(const QString& trackId, double startBeats, double lengthBeats);  // empty MIDI region
    Q_INVOKABLE void splitRegion(const QString& regionId, double atBeats);
    Q_INVOKABLE void joinRegions(const QStringList& regionIds);
    Q_INVOKABLE void resizeRegion(const QString& regionId, double startBeats, double lengthBeats);  // snapping is done by the caller
    Q_INVOKABLE void splitSelectedAtPlayhead();  // every selected region that strictly contains the playhead, one undo step
    Q_INVOKABLE void joinSelected();             // the selected regions, which must touch one another
    // Selects the regions that overlap the rectangle (beats and timeline rows, both inclusive).
    Q_INVOKABLE void selectRegionsIn(double fromBeats, double toBeats, int fromRow, int toRow, const QString& mode);
    Q_INVOKABLE void joinWithNext(const QString& regionId);  // the region that starts where this one ends, same track
    Q_INVOKABLE void toggleSoloSelected();
    Q_INVOKABLE void setSelectedColor(const QString& color);  // every selected track, one undo step
    // The R and I stubs keep their state per track here, so it survives track changes. Other action ids are ignored.
    Q_INVOKABLE void setTrackToggle(const QString& actionId, const QString& trackId, bool on);
    // Strip edits: one set_strip command each; values are clamped, NaN is ignored.
    Q_INVOKABLE void setGain(const QString& trackId, double db);
    Q_INVOKABLE void setPan(const QString& trackId, double pan);
    Q_INVOKABLE void setMute(const QString& trackId, bool on);
    Q_INVOKABLE void setSolo(const QString& trackId, bool on);
    Q_INVOKABLE void toggleMute(const QString& trackId);
    Q_INVOKABLE void toggleSolo(const QString& trackId);
    Q_INVOKABLE void moveRegion(const QString& regionId, double startBeats);
    Q_INVOKABLE void deleteRegions(const QStringList& regionIds);
    // Copies a 1-2 channel WAV of the project sample rate into <project>/audio and adds it as media plus a region
    // on `trackId` at `startBeats`, as one undo step. Errors go to lastError.
    Q_INVOKABLE void importAudio(const QUrl& file, const QString& trackId, double startBeats);
    // Several files: imported one after the other, back to back from `startBeats`. A bad file is reported and skipped.
    Q_INVOKABLE void importAudioFiles(const QList<QUrl>& files, const QString& trackId, double startBeats);
    // Peaks of an audio file of the project, `buckets` values in [0,1]. Empty until computed (on a worker);
    // waveformReady(mediaId) fires when the call can be repeated to get them.
    Q_INVOKABLE QVariantList waveformPeaks(const QString& mediaId, int buckets);

    // Same code path the change listener uses; lets tests feed snapshots in any order.
    void applySnapshotForTest(Snapshot snapshot) { applySnapshot(std::move(snapshot), generation_); }
    void applySnapshotForTest(Snapshot snapshot, std::uint64_t generation) { applySnapshot(std::move(snapshot), generation); }
    std::uint64_t generationForTest() const { return generation_; }
    void forceDegradedForTest(bool on) { forcedDegraded_ = on; }

signals:
    void projectChanged();
    void playingChanged();
    void positionChanged();
    void loopChanged();
    void degradedChanged();
    void deviceErrorChanged();
    void lastErrorChanged();
    void peakChanged();
    void commandSent(const QString& type);
    // A tool had nothing to do (no MIDI track, nothing selected, nothing to join): a toast, not an error.
    void notice(const QString& message);
    void audioEnabledChanged();
    void mixerVisibleChanged();
    void selectionChanged();
    void trackTogglesChanged();
    void toolChanged();
    void snapChanged();
    void followPlayheadChanged();
    void trackHeightChanged();
    // The loader refused a folder; the message is the loader's. The previous project stays open.
    void projectOpenFailed(const QString& message);
    void waveformReady(const QString& mediaId);

private:
    // `done(accepted)` runs on the Qt thread once the project thread has answered, or at once when nothing could be sent.
    void setStripField(const QString& trackId, const char* field, nlohmann::json value);
    struct PendingImport {
        QUrl file;
        QString trackId;
        double startBeats;  // NaN: right after the previous clip
    };
    void startNextImport();
    // `done(ok, endBeats)` runs on the Qt thread when the file is in the project or has failed.
    void runImport(const QUrl& file, const QString& trackId, double startBeats, std::function<void(bool, double)> done);
    void sendCommand(const nlohmann::json& command, std::function<void(bool)> done = {});
    void refresh(std::uint64_t revision);
    void applySnapshot(Snapshot snapshot, std::uint64_t generation);
    void tick();
    void pruneSelection();
    bool selectedToggle(const QString& actionId) const;
    void toggleSelectedFlag(const char* field, bool TrackRow::*flag);
    std::int64_t regionPosition(const RegionRow& row, double beats) const;  // beats -> the region's own unit
    void setError(const QString& message);
    void teardown();
    static std::filesystem::path toPath(const QUrl& url);

    bool openAudioDevice_;
    TrackListModel tracks_;
    RegionModel regions_;
    MixerModel mixer_;

    std::unique_ptr<JuceInit> juce_;
    std::unique_ptr<lpc::audio::AudioEngine> engine_;
    std::unique_ptr<lpc::MediaStore> media_;
    std::unique_ptr<lpc::IAudioCallback> callback_;
    std::unique_ptr<lpc::IAudioDevice> device_;
    std::unique_ptr<EngineDriver> driver_;  // keeps the engine draining when there is no device
    std::unique_ptr<lpc::ProjectHost> host_;
    std::filesystem::path dir_;
    QTimer timer_;

    std::deque<PendingImport> importQueue_;
    bool importRunning_ = false;
    double lastImportEnd_ = 0.0;
    QHash<QString, QString> mediaPaths_;
    QHash<QString, QVariantList> peaksCache_;  // key: mediaId + "#" + buckets
    QSet<QString> peaksPending_;

    // Bumped on every open: reads and notifications started for a previous project must not touch the new one.
    std::uint64_t generation_ = 0;
    bool forcedDegraded_ = false;
    std::uint64_t shownRevision_ = 0;
    lpc::TempoMap tempoMap_;
    int sampleRate_ = 48000;
    QString name_;
    double bpm_ = 120.0;
    int beatsPerBar_ = 4;
    int beatUnit_ = 4;
    double masterGain_ = 0.0;
    QString masterId_;
    bool mixerVisible_ = true;
    QString tool_ = QStringLiteral("pointer");
    QString snap_ = QStringLiteral("quarter");
    bool followPlayhead_ = true;
    QStringList selectedTracks_;
    QHash<QString, QSet<QString>> trackToggles_;  // action id -> the tracks it is on for
    QStringList selectedRegions_;
    int trackHeightIndex_ = 1;
    bool playing_ = false;
    bool loop_ = false;
    bool degraded_ = false;
    double positionSeconds_ = 0.0;
    double positionBeats_ = 0.0;
    double peak_ = 0.0;
    QString deviceError_;
    QString lastError_;
};

}  // namespace jad
