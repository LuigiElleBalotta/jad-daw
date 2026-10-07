#include "bridge/project_controller.h"

#include <QMetaObject>
#include <QPointer>
#include <QtConcurrent>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <nlohmann/json.hpp>

#include "lpc/audio/engine.h"
#include "lpc/commands.h"
#include "lpc/device.h"
#include "lpc/media_store.h"
#include "lpc/project_host.h"
#include "lpc/project_io.h"
#include "lpc/validation.h"

#ifdef JAD_HAVE_JUCE
#include <juce_events/juce_events.h>

#include "juce_device.h"
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
    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, &ProjectController::tick);
}

ProjectController::~ProjectController() { teardown(); }

bool ProjectController::degraded() const { return host_ && host_->degraded(); }

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
    host_.reset();    // joins the project thread first: nothing posts to the engine any more
    driver_.reset();
    if (device_) device_->close();
    device_.reset();
    callback_.reset();
    media_.reset();
    engine_.reset();
}

bool ProjectController::openProject(const QUrl& folder) {
    const std::filesystem::path path = toPath(folder);
    lpc::Project project;
    try {
        project = lpc::loadProject(path);
    } catch (const std::exception& e) {
        setError(QString("Cannot open project: ") + QString::fromUtf8(e.what()));
        return false;  // the current project stays open
    }

    teardown();
    dir_ = path;
    shownRevision_ = 0;
    sampleRate_ = project.sampleRate;
    engine_ = std::make_unique<lpc::audio::AudioEngine>(static_cast<double>(project.sampleRate));
    media_ = std::make_unique<lpc::MediaStore>(path, /*streaming=*/true);

    deviceError_.clear();
    bool haveDevice = false;
#ifdef JAD_HAVE_JUCE
    if (openAudioDevice_) {
        if (!juce_) juce_ = std::make_unique<JuceInit>();
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

    host_ = std::make_unique<lpc::ProjectHost>(std::move(project), *engine_, *media_);
    host_->setChangeListener([this](std::uint64_t rev) {
        QMetaObject::invokeMethod(this, [this, rev] { refresh(rev); }, Qt::QueuedConnection);
    }).get();
    refresh(host_->revision());
    timer_.start();
    emit projectChanged();
    return true;
}

bool ProjectController::newProject(const QUrl& folder) {
    const std::filesystem::path path = toPath(folder);
    try {
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
    try {
        const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
        lpc::saveProject(project, dir_);
        return true;
    } catch (const std::exception& e) {
        setError(QString("Cannot save project: ") + QString::fromUtf8(e.what()));
        return false;
    }
}

void ProjectController::refresh(std::uint64_t revision) {
    if (!host_) return;
    lpc::MediaStore* media = media_.get();
    auto future = std::make_shared<std::future<Snapshot>>(host_->read([revision, media](const lpc::Project& p) {
        return makeSnapshot(p, revision, [media](const lpc::MediaItem& item) { return media->open(item) != nullptr; });
    }));
    QPointer<ProjectController> self(this);
    (void)QtConcurrent::run([self, future] {
        Snapshot snapshot;
        try {
            snapshot = future->get();
        } catch (const std::exception&) {
            return;  // the host went away
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, s = std::move(snapshot)]() mutable {
            if (self) self->applySnapshot(std::move(s));
        }, Qt::QueuedConnection);
    });
}

void ProjectController::applySnapshot(Snapshot s) {
    if (s.revision < shownRevision_) return;  // an older read finished after a newer one
    shownRevision_ = s.revision;
    std::vector<TrackRow> withoutMaster;
    for (const TrackRow& t : s.tracks)
        if (!t.master) withoutMaster.push_back(t);
    tracks_.reset(withoutMaster);
    mixer_.reset(s.tracks);
    regions_.reset(s.regions);
    tempoMap_ = s.tempoMap;
    sampleRate_ = s.sampleRate;
    name_ = s.name;
    bpm_ = s.bpm;
    beatsPerBar_ = s.beatsPerBar;
    emit projectChanged();
}

void ProjectController::sendCommand(const nlohmann::json& command) {
    if (!host_) {
        setError("No project is open");
        return;
    }
    lpc::CommandPtr cmd;
    try {
        cmd = lpc::commandFromJson(command);
    } catch (const std::exception& e) {
        setError(QString("Invalid command: ") + QString::fromUtf8(e.what()));
        return;
    }
    emit commandSent(QString::fromStdString(command.value("type", std::string())));
    auto future = std::make_shared<std::future<std::optional<lpc::CommandError>>>(host_->submit(std::move(cmd)));
    QPointer<ProjectController> self(this);
    (void)QtConcurrent::run([self, future] {
        std::optional<lpc::CommandError> error;
        try {
            error = future->get();
        } catch (const std::exception&) {
            return;
        }
        if (!error || !self) return;
        const QString message = QString::fromStdString(error->code + ": " + error->message);
        QMetaObject::invokeMethod(self.data(), [self, message] {
            if (self) self->setError(message);
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

void ProjectController::play() {
    if (!host_) return;
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

}  // namespace jad
