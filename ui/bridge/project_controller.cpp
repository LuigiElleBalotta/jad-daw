#include "bridge/project_controller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QMetaObject>
#include <QPointer>
#include <QtConcurrent>

#include <QUuid>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <future>
#include <limits>
#include <nlohmann/json.hpp>

#include "lpc/audio/engine.h"
#include "lpc/commands.h"
#include "lpc/device.h"
#include "lpc/media_store.h"
#include "lpc/patch_library.h"
#include "lpc/model_json.h"
#include "lpc/project_host.h"
#include "lpc/project_io.h"
#include "lpc/validation.h"
#include "lpc/wav.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>

#include "lpc/plugin_catalogue.h"
#include "lpc/processor_ids.h"

#ifdef JAD_HAVE_JUCE
#include <juce_events/juce_events.h>

#include "juce_device.h"
#include "juce_plugin_host.h"
#include "plugin_scanner.h"
#endif

namespace jad {

#ifdef JAD_HAVE_JUCE
struct JuceInit {
    juce::ScopedJuceInitialiser_GUI init;
};
#else
struct JuceInit {};
#endif

namespace {

struct EngineCallback final : lpc::IAudioCallback {
    explicit EngineCallback(lpc::audio::AudioEngine& e) : engine(e) {}
    void process(float* l, float* r, int n) noexcept override { engine.processBlock(l, r, n); }
    lpc::audio::AudioEngine& engine;
};

constexpr double kMaxBeats = static_cast<double>(lpc::kMaxPosition) / lpc::kPPQ;

std::shared_ptr<const lpc::PatchLibrary> loadPatchCatalogue() {
    const QString resource = QStringLiteral(":/qt/qml/Jad/patches/patches.json");
    const std::string name = resource.toStdString();
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly)) return std::make_shared<lpc::PatchLibrary>(lpc::PatchLibrary::unreadable(name, "cannot open the patch catalogue"));
    try {
        const QByteArray bytes = file.readAll();
        return std::make_shared<lpc::PatchLibrary>(lpc::PatchLibrary::fromJson(nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size()), name));
    } catch (const std::exception& e) {
        return std::make_shared<lpc::PatchLibrary>(lpc::PatchLibrary::unreadable(name, std::string("not valid JSON: ") + e.what()));
    }
}

std::optional<lpc::TrackKind> kindFromName(const QString& k) {
    if (k == "audio") return lpc::TrackKind::Audio;
    if (k == "instrument") return lpc::TrackKind::Instrument;
    if (k == "aux") return lpc::TrackKind::Aux;
    if (k == "bus") return lpc::TrackKind::Bus;
    return std::nullopt;
}
std::optional<lpc::Uuid> uuidOf(const QString& id) { return lpc::Uuid::parse(id.toStdString()); }

}  // namespace

// Calls processBlock at about real time pace when there is no audio device, so that the message queue is
// always drained (edits keep reaching the engine) even though nothing is heard.
class EngineDriver {
public:
    EngineDriver(lpc::audio::AudioEngine& engine, int sampleRate) : engine_(engine), sampleRate_(sampleRate), thread_([this] { run(); }) {}
    ~EngineDriver() {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
    }

private:
    void run() {
        constexpr int kBlock = 256;
        std::vector<float> l(kBlock), r(kBlock);
        const auto period = std::chrono::microseconds(static_cast<long long>(1e6 * kBlock / sampleRate_));
        while (!stop_.load()) {
            engine_.processBlock(l.data(), r.data(), kBlock);
            std::this_thread::sleep_for(period);
        }
    }
    lpc::audio::AudioEngine& engine_;
    int sampleRate_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

ProjectController::ProjectController(QObject* parent) : ProjectController(true, parent) {}

ProjectController::ProjectController(bool openAudioDevice, QObject* parent) : QObject(parent), openAudioDevice_(openAudioDevice) {
    connect(this, &ProjectController::selectionChanged, this, &ProjectController::trackFlagsChanged);
    connect(this, &ProjectController::projectChanged, this, &ProjectController::trackFlagsChanged);
    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, &ProjectController::tick);
    connect(this, &ProjectController::selectionChanged, this, &ProjectController::trackTogglesChanged);
    patches_ = loadPatchCatalogue();
    for (const lpc::PatchProblem& p : patches_->problems()) qWarning().noquote() << "patch catalogue:" << QString::fromStdString(p.where) << QString::fromStdString(p.message);
    library_.setLibrary(patches_.get());
    connect(this, &ProjectController::selectionChanged, this, &ProjectController::refreshPanels);
}

ProjectController::~ProjectController() { teardown(); }

bool ProjectController::degraded() const { return forcedDegraded_ || (host_ && host_->degraded()); }

std::filesystem::path ProjectController::toPath(const QUrl& url) {
    const QString local = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const QByteArray utf8 = local.toUtf8();
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.constData()), static_cast<std::size_t>(utf8.size())));
}

void ProjectController::setError(const QString& message) {
    if (message == lastError_) return;
    lastError_ = message;
    emit lastErrorChanged();
}

void ProjectController::clearError() { setError({}); }

void ProjectController::teardown() {
    timer_.stop();
    inspectorBusId_.clear();
    pinOwner_.clear();
#ifdef JAD_HAVE_JUCE
    if (pluginHost_) pluginHost_->closeAllEditors();  // the editors must go before the instances they show
#endif
    host_.reset();    // joins the project thread first: nothing posts to the engine any more
    driver_.reset();
    if (device_) device_->close();
    device_.reset();
    callback_.reset();
    media_.reset();
    engine_.reset();
#ifdef JAD_HAVE_JUCE
    if (pluginHost_) pluginHost_->releaseAll();  // the engine is gone: nothing uses the instances any more
#endif
}

bool ProjectController::openProject(const QUrl& folder) {
    const std::filesystem::path path = toPath(folder);
    lpc::Project project;
    try {
        project = lpc::loadProject(path);
    } catch (const std::exception& e) {
        const QString message = QString("Cannot open project: ") + QString::fromUtf8(e.what());
        setError(message);
        emit projectOpenFailed(message);
        return false;  // the current project stays open
    }

    teardown();
    ++generation_;
    importQueue_.clear();  // pending imports were meant for the previous project
    dir_ = path;
    shownRevision_ = 0;
    peaksCache_.clear();
    peaksPending_.clear();
    sampleRate_ = project.sampleRate;
    engine_ = std::make_unique<lpc::audio::AudioEngine>(static_cast<double>(project.sampleRate));
    media_ = std::make_unique<lpc::MediaStore>(path, /*streaming=*/true);

    deviceError_.clear();
    bool haveDevice = false;
#ifdef JAD_HAVE_JUCE
    if (openAudioDevice_) {
        startPlugins();
        device_ = lpc::makeJuceAudioDevice();
        callback_ = std::make_unique<EngineCallback>(*engine_);
        std::string error;
        if (device_->open(static_cast<double>(project.sampleRate), 256, *callback_, error)) {
            haveDevice = true;
        } else {
            deviceError_ = QString::fromStdString(error);
            device_.reset();
        }
    } else {
        deviceError_ = "audio output disabled";
    }
#else
    deviceError_ = "built without an audio backend";
#endif
    if (!haveDevice) driver_ = std::make_unique<EngineDriver>(*engine_, project.sampleRate);
    emit deviceErrorChanged();

#ifdef JAD_HAVE_JUCE
    lpc::IPluginHost* pluginHost = pluginHost_.get();
#else
    lpc::IPluginHost* pluginHost = nullptr;
#endif
    host_ = std::make_unique<lpc::ProjectHost>(std::move(project), *engine_, *media_, pluginHost);
    const std::uint64_t generation = generation_;
    host_->setChangeListener([this, generation](std::uint64_t rev) {
        QMetaObject::invokeMethod(this, [this, rev, generation] { if (generation == generation_) refresh(rev); }, Qt::QueuedConnection);
    }).get();
    refresh(host_->revision());
    timer_.start();
    emit projectChanged();
    return true;
}

bool ProjectController::newProject(const QUrl& folder) {
    const std::filesystem::path path = toPath(folder);
    try {
        if (std::filesystem::exists(path / "project.json")) {
            setError("Cannot create project: this folder already contains a project");
            return false;
        }
        std::filesystem::create_directories(path);
        lpc::saveProject(lpc::Project{}, path);
    } catch (const std::exception& e) {
        setError(QString("Cannot create project: ") + QString::fromUtf8(e.what()));
        return false;
    }
    return openProject(folder);
}

bool ProjectController::saveProject() {
    if (!host_) return false;
    commitPluginStates();                                        // what an editor changed is part of the project
    host_->read([](const lpc::Project&) { return 0; }).get();  // the commands above have run
    try {
        const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
        lpc::saveProject(project, dir_);
        return true;
    } catch (const std::exception& e) {
        setError(QString("Cannot save project: ") + QString::fromUtf8(e.what()));
        return false;
    }
}

void ProjectController::refresh(std::uint64_t /*lowerBound*/) {
    if (!host_) return;
    lpc::MediaStore* media = media_.get();
    const lpc::ProjectHost* host = host_.get();
    // The read runs on the project thread, which also bumps the revision right after each change: inside the read,
    // revision() is exactly the revision of the state being copied (the listener's `revision` is only a lower bound).
    auto future = std::make_shared<std::future<Snapshot>>(host_->read([host, media, patches = patches_](const lpc::Project& p) {
        return makeSnapshot(p, host->revision(), [media](const lpc::MediaItem& item) { return media->open(item) != nullptr; }, patches.get());
    }));
    QPointer<ProjectController> self(this);
    const std::uint64_t generation = generation_;
    (void)QtConcurrent::run([self, future, generation] {
        Snapshot snapshot;
        try {
            snapshot = future->get();
        } catch (const std::exception&) {
            return;  // the host went away
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, generation, s = std::move(snapshot)]() mutable {
            if (self) self->applySnapshot(std::move(s), generation);
        }, Qt::QueuedConnection);
    });
}

void ProjectController::applySnapshot(Snapshot s, std::uint64_t generation) {
    if (generation != generation_) return;  // read for a project that has been replaced
    if (s.revision < shownRevision_) return;  // an older read finished after a newer one
    shownRevision_ = s.revision;
    for (TrackRow& t : s.tracks) {
        t.recordArm = trackToggles_.value(QStringLiteral("track.recordArm")).contains(t.id);
        t.inputMonitor = trackToggles_.value(QStringLiteral("track.inputMonitor")).contains(t.id);
        t.soloSafe = trackToggles_.value(QStringLiteral("track.soloSafe")).contains(t.id);
    }
    std::vector<TrackRow> withoutMaster;
    for (const TrackRow& t : s.tracks)
        if (!t.master) withoutMaster.push_back(t);
    tracks_.reset(withoutMaster);
    ++routingRevision_;
    if (!selectWhenListed_.isEmpty() && tracks_.find(selectWhenListed_)) {
        selectedTracks_ = {selectWhenListed_};
        selectWhenListed_.clear();
        emit selectionChanged();
    }
    mixer_.reset(s.tracks);
    for (RegionRow& r : s.regions) r.muted = mutedRegions_.contains(r.id);
    regions_.reset(s.regions);
    if (!pendingRegionSelection_.isEmpty() && regions_.find(pendingRegionSelection_.first())) {
        selectRegions(pendingRegionSelection_, "replace");
        pendingRegionSelection_.clear();
    }
    allRows_ = s.tracks;
    regionRows_ = s.regions;
    tempoMap_ = s.tempoMap;
    mediaPaths_ = s.mediaPaths;
    sampleRate_ = s.sampleRate;
    name_ = s.name;
    bpm_ = s.bpm;
    beatsPerBar_ = s.beatsPerBar;
    beatUnit_ = s.beatUnit;
    masterId_.clear();
    masterGain_ = 0.0;
    for (const TrackRow& t : s.tracks)
        if (t.master) {
            masterId_ = t.id;
            masterGain_ = t.gainDb;
        }
    emit routingRevisionChanged();
    emit projectChanged();
    pruneSelection();
    refreshPanels();
}

void ProjectController::sendCommand(const nlohmann::json& command, std::function<void(bool)> done) {
    if (!host_) {
        setError("No project is open");
        if (done) done(false);
        return;
    }
    lpc::CommandPtr cmd;
    try {
        cmd = lpc::commandFromJson(command);
    } catch (const std::exception& e) {
        setError(QString("Invalid command: ") + QString::fromUtf8(e.what()));
        if (done) done(false);
        return;
    }
    emit commandSent(QString::fromStdString(command.value("type", std::string())));
    auto future = std::make_shared<std::future<std::optional<lpc::CommandError>>>(host_->submit(std::move(cmd)));
    QPointer<ProjectController> self(this);
    (void)QtConcurrent::run([self, future, done = std::move(done)] {
        std::optional<lpc::CommandError> error;
        try {
            error = future->get();
        } catch (const std::exception&) {
            return;
        }
        if (!self) return;
        const QString message = error ? QString::fromStdString(error->code + ": " + error->message) : QString();
        QMetaObject::invokeMethod(self.data(), [self, message, accepted = !error.has_value(), done] {
            if (!self) return;
            if (!accepted) self->setError(message);
            if (done) done(accepted);
        }, Qt::QueuedConnection);
    });
}

void ProjectController::submit(const QString& commandJson) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(commandJson.toStdString());
    } catch (const std::exception& e) {
        setError(QString("Invalid command: ") + QString::fromUtf8(e.what()));
        return;
    }
    sendCommand(j);
}

void ProjectController::setStripField(const QString& trackId, const char* field, nlohmann::json value) {
    if (!host_) return;
    sendCommand({{"type", "set_strip"}, {"trackId", trackId.toStdString()}, {field, std::move(value)}});
}

void ProjectController::setGain(const QString& trackId, double db) {
    if (!std::isfinite(db)) return;
    setStripField(trackId, "gainDb", std::clamp(db, -96.0, 24.0));
}

void ProjectController::setPan(const QString& trackId, double pan) {
    if (!std::isfinite(pan)) return;
    setStripField(trackId, "pan", std::clamp(pan, -1.0, 1.0));
}

namespace {

void applyMode(QStringList& list, const QStringList& ids, const QString& mode) {
    if (mode == "replace") {
        list = ids;
    } else if (mode == "extend") {
        for (const QString& id : ids)
            if (!list.contains(id)) list.append(id);
    } else if (mode == "toggle") {
        for (const QString& id : ids) {
            if (!list.removeOne(id)) list.append(id);
        }
    }
}

}  // namespace

void ProjectController::selectTrack(const QString& id, const QString& mode) {
    if (!tracks_.find(id)) return;
    QStringList next = selectedTracks_;
    applyMode(next, {id}, mode);
    if (next == selectedTracks_) return;
    selectedTracks_ = next;
    emit selectionChanged();
}

void ProjectController::selectRegions(const QStringList& ids, const QString& mode) {
    QStringList known;
    for (const QString& id : ids)
        if (regions_.find(id) && !known.contains(id)) known.append(id);
    if (known.isEmpty() && mode != "replace") return;
    QStringList next = selectedRegions_;
    applyMode(next, known, mode);
    if (next == selectedRegions_) return;
    selectedRegions_ = next;
    emit selectionChanged();
}

void ProjectController::selectRegion(const QString& id, const QString& mode) { selectRegions({id}, mode); }

void ProjectController::clearSelection() {
    if (selectedTracks_.isEmpty() && selectedRegions_.isEmpty()) return;
    selectedTracks_.clear();
    selectedRegions_.clear();
    emit selectionChanged();
}

void ProjectController::selectAll() {
    QStringList all;
    for (int i = 0; i < regions_.rowCount(); ++i) all.append(regions_.regionIdAt(i));
    selectRegions(all, "replace");
}

void ProjectController::pruneSelection() {
    const auto prune = [](QStringList& list, const auto& exists) {
        const int before = list.size();
        list.erase(std::remove_if(list.begin(), list.end(), [&](const QString& id) { return !exists(id); }), list.end());
        return list.size() != before;
    };
    const bool a = prune(selectedTracks_, [this](const QString& id) { return tracks_.find(id) != nullptr; });
    const bool b = prune(selectedRegions_, [this](const QString& id) { return regions_.find(id) != nullptr; });
    if (a || b) emit selectionChanged();
}

void ProjectController::addTrack(const QString& kind) {
    if (kind != "audio" && kind != "instrument" && kind != "bus") return;
    const QString label = kind == "audio" ? "Audio" : (kind == "instrument" ? "Instrument" : "Bus");
    const std::string name = (label + " " + QString::number(tracks_.totalCount() + 1)).toStdString();
    const std::string nullId = "00000000-0000-0000-0000-000000000000";
    nlohmann::json instrument = nullptr;
    if (kind == "instrument") instrument = {{"processorId", "builtin.sine"}, {"params", nlohmann::json::object()}, {"state", ""}};
    nlohmann::json track = {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()},
                            {"kind", kind.toStdString()},
                            {"name", name},
                            {"color", ""},
                            {"strip", {{"gainDb", 0}, {"pan", 0}, {"mute", false}, {"solo", false}, {"inserts", nlohmann::json::array()},
                                       {"sends", nlohmann::json::array()}, {"output", nullId}}},
                            {"regions", nlohmann::json::array()},
                            {"automation", nlohmann::json::array()},
                            {"instrument", instrument}};
    sendCommand({{"type", "add_track"}, {"index", -1}, {"track", track}});
}

void ProjectController::deleteSelectedTracks() {
    if (selectedTracks_.isEmpty()) return;
    if (selectedTracks_.size() == 1) {
        sendCommand({{"type", "remove_track"}, {"trackId", selectedTracks_.first().toStdString()}});
        return;
    }
    nlohmann::json commands = nlohmann::json::array();
    for (const QString& id : std::as_const(selectedTracks_)) commands.push_back({{"type", "remove_track"}, {"trackId", id.toStdString()}});
    sendCommand({{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::renameTrack(const QString& trackId, const QString& name) {
    sendCommand({{"type", "set_track_props"}, {"trackId", trackId.toStdString()}, {"name", name.toStdString()}});
}

void ProjectController::setTrackColor(const QString& trackId, const QString& color) {
    sendCommand({{"type", "set_track_props"}, {"trackId", trackId.toStdString()}, {"color", color.toStdString()}});
}

void ProjectController::toggleMuteSelected() { toggleSelectedFlag("mute", &TrackRow::mute); }

void ProjectController::toggleSoloSelected() { toggleSelectedFlag("solo", &TrackRow::solo); }

// Mute or solo of every selected track as one undo step: all go on when any of them is off, else all go off.
void ProjectController::toggleSelectedFlag(const char* field, bool TrackRow::*flag) {
    if (!host_) return;
    std::vector<const TrackRow*> rows;
    for (const QString& id : std::as_const(selectedTracks_))
        if (const TrackRow* t = tracks_.find(id)) rows.push_back(t);
    if (rows.empty()) return;
    const bool on = std::any_of(rows.begin(), rows.end(), [flag](const TrackRow* t) { return !(t->*flag); });
    nlohmann::json commands = nlohmann::json::array();
    for (const TrackRow* t : rows) commands.push_back({{"type", "set_strip"}, {"trackId", t->id.toStdString()}, {field, on}});
    sendCommand({{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::setSelectedColor(const QString& color) {
    if (!host_ || selectedTracks_.isEmpty()) return;
    nlohmann::json commands = nlohmann::json::array();
    for (const QString& id : std::as_const(selectedTracks_))
        commands.push_back({{"type", "set_track_props"}, {"trackId", id.toStdString()}, {"color", color.toStdString()}});
    sendCommand({{"type", "transaction"}, {"commands", commands}});
}

bool ProjectController::newProjectInTempForTest() {
    const QUrl folder = QUrl::fromLocalFile(QDir::tempPath() + "/jad-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    return newProject(folder);
}

void ProjectController::simulatePlaybackForTest(bool playing, double positionBeats) {
    timer_.stop();
    if (playing != playing_) {
        playing_ = playing;
        emit playingChanged();
    }
    positionBeats_ = positionBeats;
    emit positionChanged();
}

void ProjectController::setTrackToggle(const QString& actionId, const QString& trackId, bool on) {
    const bool safe = actionId == QStringLiteral("track.soloSafe");
    if (actionId != QStringLiteral("track.recordArm") && actionId != QStringLiteral("track.inputMonitor") && !safe) return;
    if (!tracks_.find(trackId)) return;
    QSet<QString>& ids = trackToggles_[actionId];
    if (on) ids.insert(trackId);
    else ids.remove(trackId);
    const bool arm = actionId == QStringLiteral("track.recordArm");
    if (!safe) tracks_.setToggle(trackId, arm ? TrackListModel::RecordArm : TrackListModel::InputMonitor, on);
    mixer_.setToggle(trackId, safe ? MixerModel::SoloSafe : (arm ? MixerModel::RecordArm : MixerModel::InputMonitor), on);
    emit trackTogglesChanged();
}

bool ProjectController::selectedToggle(const QString& actionId) const {
    if (selectedTracks_.isEmpty()) return false;
    return trackToggles_.value(actionId).contains(selectedTracks_.first());
}

void ProjectController::setTool(const QString& tool) {
    static const QStringList known{"pointer", "pencil", "eraser", "scissors", "glue", "zoom", "mute"};
    if (!known.contains(tool) || tool == tool_) return;
    tool_ = tool;
    emit toolChanged();
}

void ProjectController::setSnap(const QString& snap) {
    static const QStringList known{"off", "bar", "half", "quarter", "eighth", "sixteenth"};
    if (!known.contains(snap) || snap == snap_) return;
    snap_ = snap;
    emit snapChanged();
}

double ProjectController::snapBeats() const {
    if (snap_ == "off") return 0.0;
    if (snap_ == "bar") return barBeats();
    if (snap_ == "half") return 2.0;
    if (snap_ == "eighth") return 0.5;
    if (snap_ == "sixteenth") return 0.25;
    return 1.0;
}

// beats -> ticks, or microseconds for a region in absolute time
std::int64_t ProjectController::regionPosition(const RegionRow& row, double beats) const {
    std::int64_t position = static_cast<std::int64_t>(std::llround(std::clamp(beats, 0.0, kMaxBeats) * lpc::kPPQ));
    if (row.absolute) {
        const double frames = tempoMap_.ticksToSamples(position, sampleRate_);
        position = std::min<std::int64_t>(std::llround(frames * 1e6 / sampleRate_), lpc::kMaxPosition);
    }
    return position;
}

void ProjectController::createRegion(const QString& trackId, double startBeats, double lengthBeats) {
    if (!std::isfinite(startBeats) || !std::isfinite(lengthBeats)) return;
    const TrackRow* track = tracks_.find(trackId);
    if (!track) {
        setError("No such track");
        return;
    }
    if (track->kind != "instrument" && track->kind != "midi") {
        emit notice("This track cannot hold MIDI regions");
        return;
    }
    const double start = std::clamp(startBeats, 0.0, kMaxBeats);
    const double length = std::clamp(lengthBeats, 1.0 / 16.0, kMaxBeats);
    const std::string nullId = "00000000-0000-0000-0000-000000000000";
    nlohmann::json region = {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()},
                             {"timeBase", "musical"},
                             {"start", static_cast<std::int64_t>(std::llround(start * lpc::kPPQ))},
                             {"length", static_cast<std::int64_t>(std::llround(length * lpc::kPPQ))},
                             {"mediaId", nullId},
                             {"sourceOffsetFrames", 0},
                             {"gainDb", 0},
                             {"notes", nlohmann::json::array()}};
    sendCommand({{"type", "add_region"}, {"trackId", trackId.toStdString()}, {"index", -1}, {"region", region}});
}

void ProjectController::splitRegion(const QString& regionId, double atBeats) {
    if (!host_ || !std::isfinite(atBeats)) return;
    const RegionRow* row = regions_.find(regionId);
    if (!row) return;
    const double grid = snapBeats();
    const double at = grid > 0.0 ? std::round(atBeats / grid) * grid : atBeats;
    sendCommand({{"type", "split_region"},
                 {"regionId", regionId.toStdString()},
                 {"at", regionPosition(*row, at)},
                 {"newRegionId", QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()}});
}

nlohmann::json ProjectController::resizeCommand(const RegionRow& row, double startBeats, double lengthBeats) const {
    const double minLength = std::min(std::max(snapBeats(), 1.0 / 16.0), row.lengthBeats);  // a short region stays short
    const double start = std::clamp(startBeats, 0.0, kMaxBeats);
    const double end = std::clamp(start + std::max(lengthBeats, minLength), 0.0, kMaxBeats);
    const std::int64_t from = regionPosition(row, start);
    std::int64_t length = regionPosition(row, end) - from;
    if (length <= 0) length = 1;  // the Core refuses what is still wrong (a start at the very end of the range)
    return {{"type", "resize_region"}, {"regionId", row.id.toStdString()}, {"start", from}, {"length", length}};
}

void ProjectController::resizeRegion(const QString& regionId, double startBeats, double lengthBeats) {
    if (!host_ || !std::isfinite(startBeats) || !std::isfinite(lengthBeats)) return;
    const RegionRow* row = regions_.find(regionId);
    if (!row) return;
    sendCommand(resizeCommand(*row, startBeats, lengthBeats));
}

// An edge of `regionId` was dragged: when it is one of several selected regions, every selected region gets the same change
// (Logic: the same delta for all), as one "Length Change" step.
void ProjectController::resizeSelectedRegions(const QString& regionId, double startBeats, double lengthBeats) {
    if (!host_ || !std::isfinite(startBeats) || !std::isfinite(lengthBeats)) return;
    const RegionRow* row = regions_.find(regionId);
    if (!row) return;
    if (selectedRegions_.size() < 2 || !selectedRegions_.contains(regionId)) {
        resizeRegion(regionId, startBeats, lengthBeats);
        return;
    }
    const double dStart = startBeats - row->startBeats;
    const double dEnd = (startBeats + lengthBeats) - (row->startBeats + row->lengthBeats);
    nlohmann::json commands = nlohmann::json::array();
    for (const QString& id : std::as_const(selectedRegions_)) {
        const RegionRow* r = regions_.find(id);
        if (!r) continue;
        const double s = std::max(0.0, r->startBeats + dStart);
        const double e = std::max(s, r->startBeats + r->lengthBeats + dEnd);
        commands.push_back(resizeCommand(*r, s, e - s));
    }
    if (!commands.empty()) sendCommand({{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::splitSelectedAtPlayhead() {
    nlohmann::json commands = nlohmann::json::array();
    for (const QString& id : std::as_const(selectedRegions_)) {
        const RegionRow* row = regions_.find(id);
        if (!row) continue;
        if (!(row->startBeats < positionBeats_ && positionBeats_ < row->startBeats + row->lengthBeats)) continue;
        commands.push_back({{"type", "split_region"},
                            {"regionId", id.toStdString()},
                            {"at", regionPosition(*row, positionBeats_)},
                            {"newRegionId", QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()}});
    }
    if (commands.empty()) {
        emit notice("No selected region at the playhead");
        return;
    }
    if (commands.size() == 1) {
        sendCommand(commands.front());
        return;
    }
    sendCommand({{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::joinSelected() {
    if (selectedRegions_.size() < 2) {
        emit notice("Select at least two regions to join");
        return;
    }
    joinRegions(selectedRegions_);
}

void ProjectController::selectRegionsIn(double fromBeats, double toBeats, int fromRow, int toRow, const QString& mode) {
    if (!std::isfinite(fromBeats) || !std::isfinite(toBeats)) return;
    const double a = std::min(fromBeats, toBeats), b = std::max(fromBeats, toBeats);
    const int r0 = std::min(fromRow, toRow), r1 = std::max(fromRow, toRow);
    QStringList ids;
    for (const RegionRow& row : regions_.rows())
        if (row.trackIndex >= r0 && row.trackIndex <= r1 && row.startBeats < b && row.startBeats + row.lengthBeats > a) ids.append(row.id);
    selectRegions(ids, mode);
}

void ProjectController::joinRegions(const QStringList& regionIds) {
    if (!host_ || regionIds.isEmpty()) return;
    nlohmann::json ids = nlohmann::json::array();
    for (const QString& id : regionIds) ids.push_back(id.toStdString());
    sendCommand({{"type", "join_regions"}, {"regionIds", ids}});
}

void ProjectController::joinWithNext(const QString& regionId) {
    const RegionRow* row = regions_.find(regionId);
    if (!row) return;
    const double end = row->startBeats + row->lengthBeats;
    for (const RegionRow& other : regions_.rows()) {
        if (other.trackId == row->trackId && other.id != row->id && std::abs(other.startBeats - end) < 1e-6) {
            joinRegions({row->id, other.id});
            return;
        }
    }
    emit notice("Nothing to join after this region");
}

void ProjectController::setTempo(double bpm) {
    if (!host_ || !std::isfinite(bpm)) return;
    sendCommand({{"type", "set_tempo"}, {"tick", 0}, {"bpm", std::clamp(bpm, 20.0, 999.0)}});
}

void ProjectController::setSignature(int numerator, int denominator) {
    const bool denominatorOk = denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16 || denominator == 32;
    if (numerator < 1 || numerator > 32 || !denominatorOk) {
        setError("Invalid time signature");
        return;
    }
    if (!host_) return;
    sendCommand({{"type", "set_signature"}, {"tick", 0}, {"numerator", numerator}, {"denominator", denominator}});
}

void ProjectController::setMasterGain(double db) {
    if (masterId_.isEmpty()) return;
    setGain(masterId_, db);
}

void ProjectController::barBack() {
    const double bar = std::floor(positionBeats_ / barBeats()) * barBeats();
    locateBeats(std::max(0.0, positionBeats_ - bar < 1.0 / 16.0 ? bar - barBeats() : bar));
}

void ProjectController::barForward() { locateBeats((std::floor(positionBeats_ / barBeats()) + 1.0) * barBeats()); }

void ProjectController::setMute(const QString& trackId, bool on) { setStripField(trackId, "mute", on); }

void ProjectController::setSolo(const QString& trackId, bool on) { setStripField(trackId, "solo", on); }

// Command-click on M or S: every strip that is in the same state as the clicked one switches to its new state, the master
// included for mute (one undo step).
void ProjectController::setAllStrips(const char* field, bool on) {
    nlohmann::json commands = nlohmann::json::array();
    const bool solo = std::string(field) == "solo";
    for (const TrackRow& t : allRows_) {
        if (solo && (t.master || trackToggles_.value(QStringLiteral("track.soloSafe")).contains(t.id))) continue;
        if ((solo ? t.solo : t.mute) == !on) commands.push_back({{"type", "set_strip"}, {"trackId", t.id.toStdString()}, {field, on}});
    }
    if (!commands.empty()) sendCommand({{"type", "transaction"}, {"commands", commands}});
}
void ProjectController::muteAll(bool on) { setAllStrips("mute", on); }
void ProjectController::soloAll(bool on) { setAllStrips("solo", on); }

// Option-click on S: only this track is soloed (one undo step).
void ProjectController::soloExclusive(const QString& trackId) {
    nlohmann::json commands = nlohmann::json::array();
    for (const TrackRow& t : allRows_) {
        const bool want = t.id == trackId;
        if (!t.master && t.solo != want) commands.push_back({{"type", "set_strip"}, {"trackId", t.id.toStdString()}, {"solo", want}});
    }
    if (!commands.empty()) sendCommand({{"type", "transaction"}, {"commands", commands}});
}

// Option-click on a lit S: every solo off (one undo step).
void ProjectController::clearSolo() {
    nlohmann::json commands = nlohmann::json::array();
    for (const TrackRow& t : allRows_)
        if (t.solo) commands.push_back({{"type", "set_strip"}, {"trackId", t.id.toStdString()}, {"solo", false}});
    if (!commands.empty()) sendCommand({{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::toggleMute(const QString& trackId) {
    if (const TrackRow* t = tracks_.find(trackId)) setMute(trackId, !t->mute);
}

void ProjectController::toggleSolo(const QString& trackId) {
    if (const TrackRow* t = tracks_.find(trackId)) setSolo(trackId, !t->solo);
}

nlohmann::json ProjectController::moveCommand(const RegionRow& row, double startBeats) const {
    const double clamped = std::clamp(startBeats, 0.0, kMaxBeats);
    std::int64_t start = static_cast<std::int64_t>(std::llround(clamped * lpc::kPPQ));
    if (row.absolute) {
        const double frames = tempoMap_.ticksToSamples(start, sampleRate_);
        start = std::min<std::int64_t>(std::llround(frames * 1e6 / sampleRate_), lpc::kMaxPosition);
    }
    return {{"type", "move_region"}, {"regionId", row.id.toStdString()}, {"start", start}};
}

void ProjectController::moveRegion(const QString& regionId, double startBeats) {
    if (!host_ || !std::isfinite(startBeats)) return;
    const RegionRow* row = regions_.find(regionId);
    if (!row) return;
    sendCommand(moveCommand(*row, startBeats));
}

// A selected region was dragged: all the selected regions move by the same amount, as one "Drag" step.
void ProjectController::moveSelectedRegions(const QString& regionId, double startBeats) {
    if (!host_ || !std::isfinite(startBeats)) return;
    const RegionRow* row = regions_.find(regionId);
    if (!row) return;
    if (selectedRegions_.size() < 2 || !selectedRegions_.contains(regionId)) {
        moveRegion(regionId, startBeats);
        return;
    }
    double delta = startBeats - row->startBeats;
    for (const QString& id : std::as_const(selectedRegions_))  // the group stops at the start of the project
        if (const RegionRow* r = regions_.find(id)) delta = std::max(delta, -r->startBeats);
    nlohmann::json commands = nlohmann::json::array();
    for (const QString& id : std::as_const(selectedRegions_))
        if (const RegionRow* r = regions_.find(id)) commands.push_back(moveCommand(*r, r->startBeats + delta));
    if (!commands.empty()) sendCommand({{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::deleteRegions(const QStringList& regionIds) {
    if (!host_ || regionIds.isEmpty()) return;
    auto remove = [](const QString& id) { return nlohmann::json{{"type", "remove_region"}, {"regionId", id.toStdString()}}; };
    if (regionIds.size() == 1) {
        sendCommand(remove(regionIds.first()));
        return;
    }
    nlohmann::json commands = nlohmann::json::array();
    for (const QString& id : regionIds) commands.push_back(remove(id));
    sendCommand({{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::importAudio(const QUrl& fileUrl, const QString& trackId, double startBeats) {
    importAudioFiles({fileUrl}, trackId, startBeats);
}

void ProjectController::importAudioFiles(const QList<QUrl>& files, const QString& trackId, double startBeats) {
    if (!host_ || files.isEmpty() || !std::isfinite(startBeats)) return;
    const TrackRow* track = tracks_.find(trackId);
    if (!track || track->kind != "audio") {
        setError("Audio can only be dropped on an audio track");
        return;
    }
    for (int i = 0; i < files.size(); ++i)
        importQueue_.push_back({files[i], trackId, i == 0 ? startBeats : std::numeric_limits<double>::quiet_NaN()});
    if (!importRunning_) startNextImport();
}

void ProjectController::startNextImport() {
    if (importQueue_.empty()) {
        importRunning_ = false;
        return;
    }
    importRunning_ = true;
    const PendingImport next = importQueue_.front();
    importQueue_.pop_front();
    const double start = std::isnan(next.startBeats) ? lastImportEnd_ : next.startBeats;
    QPointer<ProjectController> self(this);
    runImport(next.file, next.trackId, start, [self, start](bool ok, double endBeats) {
        if (!self) return;
        self->lastImportEnd_ = ok ? endBeats : start;  // a failed file leaves the spot free for the next one
        self->startNextImport();
    });
}

void ProjectController::runImport(const QUrl& fileUrl, const QString& trackId, double startBeats, std::function<void(bool, double)> done) {
    if (!host_) {
        done(false, startBeats);
        return;
    }
    const std::filesystem::path source = toPath(fileUrl);
    const std::filesystem::path projectDir = dir_;
    const int projectRate = sampleRate_;
    const double beats = std::clamp(startBeats, 0.0, kMaxBeats);
    const lpc::Ticks startTick = static_cast<lpc::Ticks>(std::llround(beats * lpc::kPPQ));
    const double startFrames = tempoMap_.ticksToSamples(startTick, projectRate);
    const std::int64_t startMicros = std::min<std::int64_t>(std::llround(startFrames * 1e6 / projectRate), lpc::kMaxPosition);

    QPointer<ProjectController> self(this);
    (void)QtConcurrent::run([self, source, projectDir, projectRate, trackId, startMicros, beats, done] {
        auto fail = [&](const QString& message) {
            if (!self) return;
            QMetaObject::invokeMethod(self.data(), [self, message, done, beats] {
                if (!self) return;
                self->setError(message);
                done(false, beats);
            }, Qt::QueuedConnection);
        };
        const QString sourceName = QString::fromStdU16String(source.filename().u16string());
        std::int64_t frames = 0;
        int channels = 0;
        try {
            lpc::WavFile wav(source);
            if (wav.sampleRate() != projectRate) {
                fail(QString("Cannot import %1: its sample rate is %2 Hz but the project uses %3 Hz").arg(sourceName).arg(wav.sampleRate()).arg(projectRate));
                return;
            }
            if (wav.channels() < 1 || wav.channels() > 2) {
                fail(QString("Cannot import %1: only mono and stereo files are supported").arg(sourceName));
                return;
            }
            frames = wav.frames();
            channels = wav.channels();
        } catch (const std::exception& e) {
            fail(QString("Cannot import %1: %2").arg(sourceName, QString::fromUtf8(e.what())));
            return;
        }

        // copy into <project>/audio under a name that is not taken yet
        std::filesystem::path target;
        bool created = false;
        try {
            const std::filesystem::path audioDir = projectDir / "audio";
            std::filesystem::create_directories(audioDir);
            const std::filesystem::path stem = source.stem(), ext = source.extension();
            // copy_file without overwrite fails when the name is taken (also by a concurrent import): try the next name
            for (int n = 1; !created; ++n) {
                target = n == 1 ? audioDir / source.filename()
                                : audioDir / (stem.u16string() + u" (" + QString::number(n).toStdU16String() + u")" + ext.u16string());
                if (std::filesystem::exists(target)) continue;
                std::error_code ec;
                created = std::filesystem::copy_file(source, target, std::filesystem::copy_options::none, ec);
                if (ec && ec != std::errc::file_exists) throw std::filesystem::filesystem_error("copy failed", source, target, ec);
            }
        } catch (const std::exception& e) {
            std::error_code ignore;
            if (created) std::filesystem::remove(target, ignore);  // only a file this worker made
            fail(QString("Cannot import %1: %2").arg(sourceName, QString::fromUtf8(e.what())));
            return;
        }

        QString hash;
        {
            QFile f(QString::fromStdU16String(target.u16string()));
            QCryptographicHash h(QCryptographicHash::Sha256);
            if (f.open(QIODevice::ReadOnly) && h.addData(&f)) hash = QString::fromLatin1(h.result().toHex());
        }

        if (!self) {
            std::error_code ignore;
            std::filesystem::remove(target, ignore);
            return;
        }
        QMetaObject::invokeMethod(self.data(), [self, target, projectDir, trackId, startMicros, frames, channels, projectRate, hash, done, beats] {
            std::error_code ignore;
            if (!self || self->dir_ != projectDir || !self->host_) {  // closed or replaced meanwhile
                std::filesystem::remove(target, ignore);
                if (self) self->importQueue_.clear();
                done(false, beats);
                return;
            }
            lpc::MediaItem item;
            item.id = lpc::Uuid::random();
            const std::u8string rel = (std::filesystem::path("audio") / target.filename()).generic_u8string();
            item.path.assign(rel.begin(), rel.end());
            item.hash = hash.toStdString();
            item.sampleRate = projectRate;
            item.channels = channels;
            item.frames = frames;
            lpc::Region region;
            region.id = lpc::Uuid::random();
            region.timeBase = lpc::TimeBase::Absolute;
            region.start = startMicros;
            region.length = std::max<std::int64_t>(1, std::llround(static_cast<double>(frames) * 1e6 / projectRate));
            region.mediaId = item.id;
            nlohmann::json commands = nlohmann::json::array();
            commands.push_back({{"type", "add_media"}, {"item", item}, {"index", -1}});
            commands.push_back({{"type", "add_region"}, {"trackId", trackId.toStdString()}, {"region", region}, {"index", -1}});
            const double endFrames = static_cast<double>(startMicros + region.length) * projectRate / 1e6;
            const double endBeats = static_cast<double>(self->tempoMap_.samplesToTicks(endFrames, projectRate)) / lpc::kPPQ;
            self->sendCommand({{"type", "transaction"}, {"commands", commands}}, [target, done, endBeats, beats](bool accepted) {
                if (!accepted) {
                    std::error_code ec;
                    std::filesystem::remove(target, ec);  // rejected: leave no orphan file
                }
                done(accepted, accepted ? endBeats : beats);
            });
        }, Qt::QueuedConnection);
    });
}

void ProjectController::undo() {
    if (!host_) return;
    auto future = std::make_shared<std::future<std::optional<lpc::CommandError>>>(host_->undo());
    QPointer<ProjectController> self(this);
    (void)QtConcurrent::run([self, future] {
        const auto error = future->get();
        if (!error || !self) return;
        const QString message = QString::fromStdString(error->code + ": " + error->message);
        QMetaObject::invokeMethod(self.data(), [self, message] { if (self) self->setError(message); }, Qt::QueuedConnection);
    });
}

void ProjectController::redo() {
    if (!host_) return;
    auto future = std::make_shared<std::future<std::optional<lpc::CommandError>>>(host_->redo());
    QPointer<ProjectController> self(this);
    (void)QtConcurrent::run([self, future] {
        const auto error = future->get();
        if (!error || !self) return;
        const QString message = QString::fromStdString(error->code + ": " + error->message);
        QMetaObject::invokeMethod(self.data(), [self, message] { if (self) self->setError(message); }, Qt::QueuedConnection);
    });
}

QVariantList ProjectController::waveformPeaks(const QString& mediaId, int buckets) {
    if (buckets <= 0 || buckets > 4096) return {};
    const QString key = mediaId + '#' + QString::number(buckets);
    if (const auto it = peaksCache_.constFind(key); it != peaksCache_.constEnd()) return it.value();
    const auto path = mediaPaths_.constFind(mediaId);
    if (path == mediaPaths_.constEnd() || peaksPending_.contains(key)) return {};
    peaksPending_.insert(key);
    const QByteArray rel = path->toUtf8();
    const std::filesystem::path file = dir_ / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(rel.constData()), static_cast<std::size_t>(rel.size())));
    QPointer<ProjectController> self(this);
    const std::filesystem::path cacheDir = dir_ / "cache";
    (void)QtConcurrent::run([self, key, mediaId, buckets, file, cacheDir] {
        const std::vector<float> peaks = WaveformCache(cacheDir).peaks(file, buckets);
        if (!self) return;
        QVariantList list;
        list.reserve(static_cast<qsizetype>(peaks.size()));
        for (float v : peaks) list.push_back(static_cast<double>(v));
        QMetaObject::invokeMethod(self.data(), [self, key, mediaId, list] {
            if (!self || !self->peaksPending_.remove(key)) return;  // the project changed meanwhile
            self->peaksCache_.insert(key, list);
            emit self->waveformReady(mediaId);
        }, Qt::QueuedConnection);
    });
    return {};
}

void ProjectController::play() {
    if (!host_) return;
    if (degraded()) {
        setError("Audio engine not running: playback is unavailable until it recovers");
        return;
    }
    if (!device_) {
        setError(QString("No audio output: ") + (deviceError_.isEmpty() ? QString("no device") : deviceError_));
        return;
    }
    host_->play();
}

void ProjectController::stop() {
    if (host_) host_->stop();
}

void ProjectController::locateBeats(double beats) {
    if (!host_ || !std::isfinite(beats)) return;
    const double clamped = std::clamp(beats, 0.0, kMaxBeats);
    const lpc::Ticks ticks = static_cast<lpc::Ticks>(std::llround(clamped * lpc::kPPQ));
    const std::int64_t frames = static_cast<std::int64_t>(std::llround(tempoMap_.ticksToSamples(ticks, sampleRate_)));
    host_->locate(frames);
}

void ProjectController::locateSeconds(double seconds) {
    if (!host_ || !std::isfinite(seconds)) return;
    host_->locate(static_cast<std::int64_t>(std::llround(std::clamp(seconds, 0.0, 86400.0) * sampleRate_)));
}

void ProjectController::setLoopBeats(double startBeats, double endBeats) {
    if (!host_ || !std::isfinite(startBeats) || !std::isfinite(endBeats)) return;
    const auto toFrames = [this](double beats) {
        const lpc::Ticks ticks = static_cast<lpc::Ticks>(std::llround(std::clamp(beats, 0.0, kMaxBeats) * lpc::kPPQ));
        return static_cast<std::int64_t>(std::llround(tempoMap_.ticksToSamples(ticks, sampleRate_)));
    };
    const std::int64_t a = toFrames(startBeats), b = toFrames(endBeats);
    host_->setLoop(a, b);
    loop_ = b > a;
    emit loopChanged();
}

void ProjectController::tick() {
    if (!engine_) return;
    const bool playing = engine_->playing();
    if (playing != playing_) {
        playing_ = playing;
        emit playingChanged();
    }
    const std::int64_t frames = engine_->positionFrames();
    const double seconds = static_cast<double>(frames) / sampleRate_;
    if (seconds != positionSeconds_) {
        positionSeconds_ = seconds;
        positionBeats_ = static_cast<double>(tempoMap_.samplesToTicks(static_cast<double>(frames), sampleRate_)) / lpc::kPPQ;
        emit positionChanged();
    }
    const double peak = engine_->masterPeak();
    if (peak != peak_) {
        peak_ = peak;
        emit peakChanged();
    }
    const bool deg = degraded();
    if (deg != degraded_) {
        degraded_ = deg;
        emit degradedChanged();
    }
}

void ProjectController::loadPanelState(QSettings& s) {
    setInspectorVisible(s.value("panels/inspector", inspectorVisible_).toBool());
    setLibraryVisible(s.value("panels/library", libraryVisible_).toBool());
    setSmartControlsVisible(s.value("panels/smartControls", smartControlsVisible_).toBool());
    setLeftColumnWidth(s.value("panels/leftColumnWidth", leftColumnWidth_).toDouble());
    setSmartControlsHeight(s.value("panels/smartControlsHeight", smartControlsHeight_).toDouble());
}

void ProjectController::savePanelState(QSettings& s) const {
    s.setValue("panels/inspector", inspectorVisible_);
    s.setValue("panels/library", libraryVisible_);
    s.setValue("panels/smartControls", smartControlsVisible_);
    s.setValue("panels/leftColumnWidth", leftColumnWidth_);
    s.setValue("panels/smartControlsHeight", smartControlsHeight_);
}

void ProjectController::setLeftColumnWidth(double width) {
    if (!std::isfinite(width)) return;
    const double clamped = std::clamp(width, 200.0, 320.0);
    if (clamped == leftColumnWidth_) return;
    leftColumnWidth_ = clamped;
    emit panelsChanged();
}

void ProjectController::setSmartControlsHeight(double height) {
    if (!std::isfinite(height)) return;
    const double clamped = std::clamp(height, 120.0, 320.0);
    if (clamped == smartControlsHeight_) return;
    smartControlsHeight_ = clamped;
    emit panelsChanged();
}

const TrackRow* ProjectController::rowOf(const QString& trackId) const {
    for (const TrackRow& r : allRows_)
        if (r.id == trackId) return &r;
    return nullptr;
}

void ProjectController::refreshPanels() {
    inspector_.update(allRows_, regionRows_, selectedTracks_, selectedRegions_, inspectorBusId_);
    if (!inspectorBusId_.isEmpty() && (inspector_.trackId() != pinOwner_ || !inspector_.pinned())) {  // another track, or the bus is gone
        inspectorBusId_.clear();
        inspector_.update(allRows_, regionRows_, selectedTracks_, selectedRegions_);
    }
    library_.setTrack(inspector_.shownKind(), inspector_.shownPatchId());
}

void ProjectController::announceStub(const QString& label) { emit notice(tr("%1: not implemented yet").arg(label)); }

void ProjectController::applyPatch(const QString& patchId) {
    const QString trackId = inspector_.trackId();
    const TrackRow* row = rowOf(trackId);
    const auto id = uuidOf(trackId);
    const auto kind = row ? kindFromName(row->kind) : std::nullopt;
    if (!row || !id || !kind) {
        emit notice(tr("Select a track to choose a patch"));
        return;
    }
    lpc::CommandPtr cmd = patches_->applyCommand(*id, *kind, patchId.toStdString());
    if (!cmd) {
        emit notice(tr("This patch does not fit the selected track"));
        return;
    }
    sendCommand(cmd->toJson());
}

void ProjectController::revertPatch() {
    const TrackRow* row = rowOf(inspector_.trackId());
    if (!row || row->patchId.isEmpty()) {
        emit notice(tr("This track has no patch to revert"));
        return;
    }
    if (!patches_->find(row->patchId.toStdString())) {
        emit notice(tr("Patch '%1' is no longer in the catalogue").arg(row->patchId));
        return;
    }
    applyPatch(row->patchId);
}

void ProjectController::setSmartControl(const QString& trackId, const QString& controlId, double value) {
    const TrackRow* row = rowOf(trackId);
    const auto id = uuidOf(trackId);
    if (!row || !id || !std::isfinite(value)) return;
    const lpc::Patch* patch = patches_->find(row->patchId.toStdString());
    if (!patch) return;
    for (const lpc::SmartControl& c : patch->smartControls) {
        if (QString::fromStdString(c.id) != controlId) continue;
        if (lpc::CommandPtr cmd = lpc::PatchLibrary::smartControlCommand(*id, c, value)) sendCommand(cmd->toJson());
        return;
    }
}

void ProjectController::addInsert(const QString& trackId, const QString& processorId) {
    sendCommand({{"type", "add_insert"}, {"trackId", trackId.toStdString()}, {"index", -1},
                 {"insert", {{"processorId", processorId.toStdString()}, {"params", nlohmann::json::object()}, {"state", ""}}}});
}

void ProjectController::startPlugins() {
#ifdef JAD_HAVE_JUCE
    if (!openAudioDevice_) return;
    if (!juce_) juce_ = std::make_unique<JuceInit>();
    setUpPlugins();
#endif
}

void ProjectController::setUpPlugins() {
#ifdef JAD_HAVE_JUCE
    if (pluginHost_) return;
    pluginHost_ = std::make_shared<lpc::JucePluginHost>();
    plugins_.setSupported(true);

    lpc::PluginScanner::Options o;
    o.scannerExe = std::filesystem::path(QCoreApplication::applicationDirPath().toStdU16String()) / "lpc-plugin-scanner.exe";
    o.cacheFile = lpc::appConfigDir() / "plugins.json";
    o.folders = lpc::PluginScanner::defaultFolders();
    scanner_ = std::make_unique<lpc::PluginScanner>(o);
    QPointer<ProjectController> self(this);
    scanner_->setChangedListener([self] {  // scan thread
        QMetaObject::invokeMethod(self.data(), [self] { if (self) self->refreshPluginRows(); }, Qt::QueuedConnection);
    });
    connect(&plugins_, &PluginsModel::rescanRequested, this, [this](int mode) {
        if (scanner_) scanner_->start(static_cast<lpc::ScanMode>(mode));
    });
    pluginHost_->setEditorClosedListener([this](const lpc::InsertSlot& slot) { commitPluginState(slot); });
    refreshPluginRows();                            // the cache is available at once
    scanner_->start(lpc::ScanMode::NewAndChanged);  // then new and changed files, in the background
#endif
}

void ProjectController::refreshPluginRows() {
#ifdef JAD_HAVE_JUCE
    if (!scanner_ || !pluginHost_) return;
    const lpc::PluginCatalogue c = scanner_->snapshot();
    pluginHost_->setCatalogue(c.descriptors());
    std::vector<PluginRow> rows;
    for (const lpc::ScanEntry& e : c.entries()) {
        if (e.status == lpc::ScanStatus::Ok) {
            for (const lpc::PluginDescriptor& d : e.descriptors)
                rows.push_back({QString::fromStdString(d.id), QString::fromStdString(d.name), QString::fromStdString(d.vendor), "ok",
                                QString::fromStdString(e.path), QString()});
        } else {
            const QString file = QFileInfo(QString::fromStdString(e.path)).completeBaseName();
            rows.push_back({QString(), file, QString(), "failed", QString::fromStdString(e.path), QString::fromStdString(e.reason)});
        }
    }
    plugins_.setRows(std::move(rows));
    plugins_.setScan(scanner_->running(), scanner_->done(), scanner_->total());
#endif
}

void ProjectController::addPlugin(const QString& trackId, const QString& pluginId, const QString& label) {
    sendCommand({{"type", "add_insert"}, {"trackId", trackId.toStdString()}, {"index", -1},
                 {"insert", {{"processorId", pluginId.toStdString()}, {"params", nlohmann::json::object()}, {"state", ""},
                             {"label", label.left(128).toStdString()}}}});
}

void ProjectController::setInsertState(const QString& trackId, int index, const QString& state) {
    sendCommand({{"type", "set_insert_state"}, {"trackId", trackId.toStdString()}, {"index", index}, {"state", state.toStdString()}});
}

void ProjectController::openPluginEditor(const QString& trackId, int index) {
#ifdef JAD_HAVE_JUCE
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    if (pluginHost_ && id && !pluginHost_->openEditor(lpc::InsertSlot{*id, index}))
        setError(tr("This plug-in has no editor, or it is not loaded yet"));
#else
    Q_UNUSED(trackId)
    Q_UNUSED(index)
#endif
}

void ProjectController::commitPluginState(const lpc::InsertSlot& slot) {
#ifdef JAD_HAVE_JUCE
    if (!pluginHost_ || !host_) return;
    const std::optional<std::string> captured = pluginHost_->captureState(slot);  // also tells the host this is now the model's state
    if (!captured) return;
    const std::string& state = *captured;
    const lpc::Uuid track = slot.track;
    const int index = slot.index;
    const bool changed = host_->read([track, index, &state](const lpc::Project& p) {
        const lpc::Track* t = p.findTrack(track);
        return t && index >= 0 && index < static_cast<int>(t->strip.inserts.size()) &&
               t->strip.inserts[static_cast<std::size_t>(index)].state != state;
    }).get();
    if (changed) setInsertState(QString::fromStdString(track.toString()), index, QString::fromStdString(state));
#else
    Q_UNUSED(slot)
#endif
}

void ProjectController::commitPluginStates() {
#ifdef JAD_HAVE_JUCE
    if (!host_ || !pluginHost_) return;
    const std::vector<lpc::InsertSlot> pluginSlots = host_->read([](const lpc::Project& p) {
        std::vector<lpc::InsertSlot> out;
        for (const lpc::Track& t : p.tracks)
            for (std::size_t i = 0; i < t.strip.inserts.size(); ++i)
                if (lpc::isVst3Id(t.strip.inserts[i].processorId)) out.push_back({t.id, static_cast<int>(i)});
        return out;
    }).get();
    for (const lpc::InsertSlot& s : pluginSlots) commitPluginState(s);
#endif
}

void ProjectController::removeInsert(const QString& trackId, int index) {
    sendCommand({{"type", "remove_insert"}, {"trackId", trackId.toStdString()}, {"index", index}});
}

void ProjectController::setInsertParam(const QString& trackId, int index, const QString& param, double value) {
    if (!std::isfinite(value)) return;
    const double v = param == "gainDb" ? std::clamp(value, -96.0, 24.0) : value;
    sendCommand({{"type", "set_insert_param"}, {"trackId", trackId.toStdString()}, {"index", index}, {"param", param.toStdString()}, {"value", v}});
}

void ProjectController::addSend(const QString& trackId, const QString& targetId) {
    sendCommand({{"type", "add_send"}, {"trackId", trackId.toStdString()}, {"index", -1},
                 {"send", {{"id", lpc::Uuid::random().toString()}, {"targetTrackId", targetId.toStdString()}, {"levelDb", -12.0}, {"preFader", false}}}});
}

void ProjectController::removeSend(const QString& sendId) { sendCommand({{"type", "remove_send"}, {"sendId", sendId.toStdString()}}); }

void ProjectController::setSendLevel(const QString& sendId, double db) {
    if (!std::isfinite(db)) return;
    sendCommand({{"type", "set_send"}, {"sendId", sendId.toStdString()}, {"levelDb", std::clamp(db, -96.0, 12.0)}});
}

void ProjectController::setSendPreFader(const QString& sendId, bool on) {
    sendCommand({{"type", "set_send"}, {"sendId", sendId.toStdString()}, {"preFader", on}});
}

void ProjectController::setOutput(const QString& trackId, const QString& outputId) {
    sendCommand({{"type", "set_output"}, {"trackId", trackId.toStdString()}, {"output", outputId.isEmpty() ? lpc::Uuid{}.toString() : outputId.toStdString()}});
}

void ProjectController::newBusFor(const QString& trackId, const QString& role) {
    if (role != "send" && role != "output") return;
    if (!tracks_.find(trackId)) return;
    int n = 1;
    const auto used = [this](const QString& name) {
        for (const TrackRow& t : allRows_)
            if (t.name == name) return true;
        return false;
    };
    while (used(QStringLiteral("Bus %1").arg(n))) ++n;
    const std::string busId = lpc::Uuid::random().toString();
    const std::string nullId = lpc::Uuid{}.toString();
    nlohmann::json bus = {{"id", busId},
                          {"kind", "bus"},
                          {"name", QStringLiteral("Bus %1").arg(n).toStdString()},
                          {"color", ""},
                          {"strip", {{"gainDb", 0}, {"pan", 0}, {"mute", false}, {"solo", false}, {"inserts", nlohmann::json::array()},
                                     {"sends", nlohmann::json::array()}, {"output", nullId}}},
                          {"regions", nlohmann::json::array()},
                          {"automation", nlohmann::json::array()},
                          {"instrument", nullptr},
                          {"showInTracks", false}};
    nlohmann::json route;
    if (role == "send")
        route = {{"type", "add_send"}, {"trackId", trackId.toStdString()}, {"index", -1},
                 {"send", {{"id", lpc::Uuid::random().toString()}, {"targetTrackId", busId}, {"levelDb", -12.0}, {"preFader", false}}}};
    else
        route = {{"type", "set_output"}, {"trackId", trackId.toStdString()}, {"output", busId}};
    sendCommand({{"type", "transaction"}, {"commands", nlohmann::json::array({{{"type", "add_track"}, {"index", -1}, {"track", bus}}, route})}},
                [this, busId](bool accepted) {
                    if (accepted) selectWhenListed_ = QString::fromStdString(busId);
                });
}

void ProjectController::setShowInTracks(const QString& trackId, bool on) {
    sendCommand({{"type", "set_track_props"}, {"trackId", trackId.toStdString()}, {"showInTracks", on}});
}

void ProjectController::moveInsert(const QString& trackId, int from, int to, const QString& toTrackId) {
    nlohmann::json cmd = {{"type", "move_insert"}, {"trackId", trackId.toStdString()}, {"from", from}, {"to", to}};
    if (!toTrackId.isEmpty() && toTrackId != trackId) cmd["toTrackId"] = toTrackId.toStdString();
    sendCommand(cmd);
}

void ProjectController::setInsertBypass(const QString& trackId, int index, bool on) {
    sendCommand({{"type", "set_insert_bypass"}, {"trackId", trackId.toStdString()}, {"index", index}, {"bypass", on}});
}

void ProjectController::showBus(const QString& busId) {
    const TrackRow* shown = rowOf(inspector_.trackId());
    const bool exists = !busId.isEmpty() && std::any_of(allRows_.begin(), allRows_.end(), [&](const TrackRow& t) { return t.id == busId; });
    if (!exists || !shown || shown->outputId == busId) inspectorBusId_.clear();
    else {
        inspectorBusId_ = busId;
        pinOwner_ = shown->id;
    }
    refreshPanels();
}

QVariantList ProjectController::targetsFor(const QString& trackId) const {
    QVariantList out;
    const auto find = [this](const QString& id) -> const TrackRow* {
        for (const TrackRow& t : allRows_)
            if (t.id == id) return &t;
        return nullptr;
    };
    if (!find(trackId)) return out;
    // the Core's rule: a target B is refused when B reaches the track through outputs and sends
    const auto reaches = [&](const QString& from, const QString& goal) {
        QStringList stack{from}, seen;
        while (!stack.isEmpty()) {
            const QString cur = stack.takeLast();
            if (cur == goal) return true;
            if (seen.contains(cur)) continue;
            seen.append(cur);
            if (const TrackRow* t = find(cur)) {
                if (!t->master && !t->outputId.isEmpty()) stack.append(t->outputId);
                for (const SendRow& s : t->sends) stack.append(s.targetId);
            }
        }
        return false;
    };
    for (const TrackRow& b : allRows_) {
        if ((b.kind != "bus" && b.kind != "aux") || b.id == trackId || reaches(b.id, trackId)) continue;
        out.append(QVariantMap{{"id", b.id}, {"name", b.name}});
    }
    return out;
}

void ProjectController::setRegionGain(const QString& regionId, double db) {
    if (!std::isfinite(db)) return;
    sendCommand({{"type", "set_region_gain"}, {"regionId", regionId.toStdString()}, {"gainDb", std::clamp(db, -96.0, 24.0)}});
}

}  // namespace jad
