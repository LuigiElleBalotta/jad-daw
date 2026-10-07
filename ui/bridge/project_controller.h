#pragma once
#include <QObject>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <atomic>
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
    QString projectName() const { return name_; }
    double bpm() const { return bpm_; }
    int beatsPerBar() const { return beatsPerBar_; }
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
    Q_INVOKABLE void setLoopBeats(double startBeats, double endBeats);
    Q_INVOKABLE void clearError();
    // Peaks of an audio file of the project, `buckets` values in [0,1]. Empty until computed (on a worker);
    // waveformReady(mediaId) fires when the call can be repeated to get them.
    Q_INVOKABLE QVariantList waveformPeaks(const QString& mediaId, int buckets);

    // Same code path the change listener uses; lets tests feed snapshots in any order.
    void applySnapshotForTest(Snapshot snapshot) { applySnapshot(std::move(snapshot)); }

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
    void audioEnabledChanged();
    void waveformReady(const QString& mediaId);

private:
    void sendCommand(const nlohmann::json& command);
    void refresh(std::uint64_t revision);
    void applySnapshot(Snapshot snapshot);
    void tick();
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

    QHash<QString, QString> mediaPaths_;
    QHash<QString, QVariantList> peaksCache_;  // key: mediaId + "#" + buckets
    QSet<QString> peaksPending_;

    std::uint64_t shownRevision_ = 0;
    lpc::TempoMap tempoMap_;
    int sampleRate_ = 48000;
    QString name_;
    double bpm_ = 120.0;
    int beatsPerBar_ = 4;
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
