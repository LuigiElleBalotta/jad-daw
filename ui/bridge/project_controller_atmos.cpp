// Spatial Audio with Dolby Atmos: object tracks are tracks with a 3D Object Panner; the master is exported as an ADM BWF file (File > Export > Project as Spatial Audio)
// and a project can be made from one (File > Import > Create Project from ADM BWF). Project settings: spatialAudio ("dolbyAtmos" when on), atmosMonitor ("binaural" or "stereo").
#include <QDir>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "bridge/project_controller.h"
#include "lpc/adm_bwf.h"
#include "lpc/atmos.h"
#include "lpc/atmos_render.h"
#include "lpc/wav.h"
#include "lpc/commands.h"
#include "lpc/offline_render.h"
#include "lpc/processor_ids.h"
#include "lpc/project_host.h"
#include "lpc/project_io.h"
#ifdef JAD_HAVE_JUCE
#include "juce_device.h"
#include "juce_plugin_host.h"
#endif

namespace jad {

QVariantList ProjectController::atmosTracks() const {
    QVariantList out;
    for (const TrackRow& t : allRows_) {
        if (t.master || (t.kind != QLatin1String("audio") && t.kind != QLatin1String("instrument") && t.kind != QLatin1String("midi"))) continue;
        bool object = false;
        for (const InsertRow& i : t.inserts) object = object || i.processorId == QLatin1String(lpc::kProcObjectPanner);
        out.append(QVariantMap{{"id", t.id}, {"name", t.name}, {"kind", t.kind}, {"object", object}});
    }
    return out;
}

void ProjectController::setAtmosMode(bool on) { setProjectSetting("spatialAudio", on ? "dolbyAtmos" : ""); }

nlohmann::json ProjectController::atmosPannerParams() const {
    const int format = std::clamp(projectSetting("atmosFormat", projectSetting("atmosMonitor", "binaural") == QLatin1String("stereo") ? "0" : "1").toInt(), 0, lpc::kMonitorFormats - 1);
    const int layout = lpc::trimLayoutOf(static_cast<lpc::MonitorFormat>(format));
    lpc::AtmosTrim trim = layout >= 0 ? lpc::automaticTrim(layout) : lpc::AtmosTrim{};
    if (layout >= 0 && projectSetting(QStringLiteral("atmosTrimMode.%1").arg(layout), "auto") == QLatin1String("manual")) {
        const auto get = [&](const char* key, double fallback) { return projectSetting(QStringLiteral("atmosTrim.%1.%2").arg(layout).arg(key), QString::number(fallback)).toDouble(); };
        trim.surroundDb = get("surround", trim.surroundDb);
        trim.heightDb = get("height", trim.heightDb);
        trim.overhead = get("overhead", 0.0) / 100.0;
        trim.plane = get("plane", 0.0) / 100.0;
    }
    return {{"mode", format}, {"downmix51", std::clamp(projectSetting("atmosDownmix51", "0").toInt(), 0, 3)}, {"downmix20", std::clamp(projectSetting("atmosDownmix20", "0").toInt(), 0, 2)},
            {"surroundTrim", std::clamp(trim.surroundDb, -24.0, 0.0)}, {"heightTrim", std::clamp(trim.heightDb, -24.0, 0.0)},
            {"overhead", std::clamp(trim.overhead * 100.0, -100.0, 100.0)}, {"plane", std::clamp(trim.plane * 100.0, -100.0, 100.0)}};
}

// Every panner takes the parameters the settings ask for: the Monitoring Format as its mode, the downmix algorithms and the trims of the layout.
void ProjectController::applyAtmosToPanners(nlohmann::json extra) {
    nlohmann::json commands = nlohmann::json::array();
    if (extra.is_array())
        for (auto& c : extra) commands.push_back(std::move(c));
    const nlohmann::json want = atmosPannerParams();
    for (const TrackRow& t : allRows_)
        for (std::size_t i = 0; i < t.inserts.size(); ++i) {
            if (t.inserts[i].processorId != QLatin1String(lpc::kProcObjectPanner)) continue;
            for (auto it = want.begin(); it != want.end(); ++it) {
                const QString key = QString::fromStdString(it.key());
                if (std::abs(t.inserts[i].params.value(key, -1e9).toDouble() - it.value().get<double>()) < 1e-9) continue;
                commands.push_back({{"type", "set_insert_param"}, {"trackId", t.id.toStdString()}, {"index", static_cast<int>(i)}, {"param", it.key()}, {"value", it.value()}});
            }
        }
    if (commands.empty()) return;
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::setAtmosObject(const QString& trackId, bool object) {
    if (!host_) return;
    int at = -1;
    for (const TrackRow& t : allRows_)
        if (t.id == trackId)
            for (std::size_t i = 0; i < t.inserts.size(); ++i)
                if (t.inserts[i].processorId == QLatin1String(lpc::kProcObjectPanner)) at = static_cast<int>(i);
    if (object && at < 0) {
        int objects = 0;
        for (const QVariant& v : atmosTracks()) objects += v.toMap().value("object").toBool() ? 1 : 0;
        if (objects >= lpc::kMaxAtmosObjects) {
            emit notice("A Dolby Atmos master has at most 118 object tracks");
            return;
        }
        sendCommand({{"type", "add_insert"}, {"trackId", trackId.toStdString()}, {"index", -1}, {"insert", {{"processorId", lpc::kProcObjectPanner}, {"params", atmosPannerParams()}, {"state", ""}}}});
    } else if (!object && at >= 0) {
        removeInsert(trackId, at);
    }
}

// Listening to the objects over headphones (binaural) or as a plain stereo pan: the panners follow.
void ProjectController::setAtmosMonitor(const QString& mode) {
    if (mode != QLatin1String("binaural") && mode != QLatin1String("stereo")) return;
    setAtmosFormat(mode == QLatin1String("binaural") ? 1 : 0);
}

void ProjectController::setAtmosFormat(int format) {
    if (format < 0 || format >= lpc::kMonitorFormats) return;
    nlohmann::json extra = nlohmann::json::array();
    extra.push_back({{"type", "set_project_setting"}, {"key", "atmosFormat"}, {"value", std::to_string(format)}});
    extra.push_back({{"type", "set_project_setting"}, {"key", "atmosMonitor"}, {"value", format == 1 ? "binaural" : "stereo"}});
    // the panners must see the new setting: the commands that change them are built from it
    const QString before = projectSetting("atmosFormat", "");
    (void)before;
    projectSettings_["atmosFormat"] = QString::number(format);
    applyAtmosToPanners(extra);
}

QVariantMap ProjectController::atmosSettings() const {
    QVariantList trims;
    for (int layout = 0; layout < 5; ++layout) {
        const lpc::AtmosTrim automatic = lpc::automaticTrim(layout);
        const bool manual = projectSetting(QStringLiteral("atmosTrimMode.%1").arg(layout), "auto") == QLatin1String("manual");
        const auto get = [&](const char* key, double fallback) { return projectSetting(QStringLiteral("atmosTrim.%1.%2").arg(layout).arg(key), QString::number(fallback)).toDouble(); };
        trims.append(QVariantMap{{"manual", manual}, {"surround", manual ? get("surround", automatic.surroundDb) : automatic.surroundDb}, {"height", manual ? get("height", automatic.heightDb) : automatic.heightDb},
                                 {"overhead", manual ? get("overhead", 0.0) : 0.0}, {"plane", manual ? get("plane", 0.0) : 0.0}});
    }
    const nlohmann::json p = atmosPannerParams();
    return {{"format", p.value("mode", 1)}, {"downmix51", p.value("downmix51", 0)}, {"downmix20", p.value("downmix20", 0)}, {"trims", trims}};
}

void ProjectController::setAtmosDownmix(const QString& which, int value) {
    const QString key = which == QLatin1String("to20") ? QStringLiteral("atmosDownmix20") : QStringLiteral("atmosDownmix51");
    projectSettings_[key] = QString::number(value);
    applyAtmosToPanners(nlohmann::json::array({{{"type", "set_project_setting"}, {"key", key.toStdString()}, {"value", std::to_string(value)}}}));
}

void ProjectController::setAtmosTrim(int layout, const QString& key, double value) {
    if (layout < 0 || layout > 4 || !std::isfinite(value)) return;
    if (key != QLatin1String("surround") && key != QLatin1String("height") && key != QLatin1String("overhead") && key != QLatin1String("plane")) return;
    nlohmann::json extra = nlohmann::json::array();
    // changing a trim makes the layout Manual, starting from what Automatic had
    if (projectSetting(QStringLiteral("atmosTrimMode.%1").arg(layout), "auto") != QLatin1String("manual")) {
        const lpc::AtmosTrim a = lpc::automaticTrim(layout);
        extra.push_back({{"type", "set_project_setting"}, {"key", QStringLiteral("atmosTrimMode.%1").arg(layout).toStdString()}, {"value", "manual"}});
        extra.push_back({{"type", "set_project_setting"}, {"key", QStringLiteral("atmosTrim.%1.surround").arg(layout).toStdString()}, {"value", std::to_string(a.surroundDb)}});
        extra.push_back({{"type", "set_project_setting"}, {"key", QStringLiteral("atmosTrim.%1.height").arg(layout).toStdString()}, {"value", std::to_string(a.heightDb)}});
        projectSettings_[QStringLiteral("atmosTrimMode.%1").arg(layout)] = "manual";
        projectSettings_[QStringLiteral("atmosTrim.%1.surround").arg(layout)] = QString::number(a.surroundDb);
        projectSettings_[QStringLiteral("atmosTrim.%1.height").arg(layout)] = QString::number(a.heightDb);
    }
    const double v = key == QLatin1String("surround") || key == QLatin1String("height") ? std::clamp(value, -24.0, 0.0) : std::clamp(value, -100.0, 100.0);
    const QString full = QStringLiteral("atmosTrim.%1.%2").arg(layout).arg(key);
    extra.push_back({{"type", "set_project_setting"}, {"key", full.toStdString()}, {"value", std::to_string(v)}});
    projectSettings_[full] = QString::number(v);
    applyAtmosToPanners(extra);
}

void ProjectController::setAtmosTrimManual(int layout, bool manual) {
    if (layout < 0 || layout > 4) return;
    const QString key = QStringLiteral("atmosTrimMode.%1").arg(layout);
    projectSettings_[key] = manual ? "manual" : "auto";
    applyAtmosToPanners(nlohmann::json::array({{{"type", "set_project_setting"}, {"key", key.toStdString()}, {"value", manual ? "manual" : "auto"}}}));
}

// File > Bounce for a Spatial Audio project: the mix as a file of the channels of the Monitoring Format.
void ProjectController::bounceAtmos(const QUrl& file) {
    if (!host_) return;
    int format = std::clamp(projectSetting("atmosFormat", projectSetting("atmosMonitor", "binaural") == QLatin1String("stereo") ? "0" : "1").toInt(), 0, lpc::kMonitorFormats - 1);
    std::filesystem::path out = file.toLocalFile().toStdWString();
    if (out.extension().empty()) out += ".wav";
    // .ec3 / .ac3: Dolby Digital Plus / Dolby Digital 5.1, encoded by FFmpeg (the encoder is FFmpeg's, not ours: it has no Atmos objects, only the 5.1 bed of the mix)
    const QString ext = QString::fromStdWString(out.extension().wstring()).toLower();
    const bool dolby = ext == QLatin1String(".ec3") || ext == QLatin1String(".eac3") || ext == QLatin1String(".ac3");
    QString ffmpeg;
    if (dolby) {
        ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty()) {
            setError("Dolby Digital files are encoded by FFmpeg: install FFmpeg and put it in the PATH");
            return;
        }
        format = static_cast<int>(lpc::MonitorFormat::S51);
    }
    if (format < 2) {
        emit notice("The binaural and stereo pan formats are bounced with File > Bounce");
        return;
    }
    if (!saveProject()) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    if (lpc::atmosObjects(project).empty()) {
        setError("Make some tracks objects first (Mix > Dolby Atmos)");
        return;
    }
    const nlohmann::json want = atmosPannerParams();
    lpc::AtmosDownmix dm;
    dm.to51 = want.value("downmix51", 0);
    dm.to20 = want.value("downmix20", 0);
    dm.trim = {want.value("surroundTrim", 0.0), want.value("heightTrim", 0.0), want.value("overhead", 0.0) / 100.0, want.value("plane", 0.0) / 100.0};
    lpc::AtmosExportOptions render;
    if (bouncing_->exchange(true)) {
        setError("A bounce is already running");
        return;
    }
    if (playing_) stop();
#ifdef JAD_HAVE_JUCE
    render.plugins = pluginHost_.get();
#endif
    emit notice("Bouncing the Spatial Audio mix…");
    const std::filesystem::path root = dir_;
    QPointer<ProjectController> self(this);
    const std::shared_ptr<std::atomic<bool>> flag = bouncing_;
    (void)QtConcurrent::run([self, flag, project, root, out, render, format, dm, dolby, ffmpeg, plus = ext != QLatin1String(".ac3")] {
        QString message;
        bool failed = false;
        try {
            lpc::MediaStore media(root, /*streaming=*/false);
            const lpc::AdmFile adm = lpc::renderAdm(project, media, render);
            flag->store(false);
            const std::vector<float> mix = lpc::renderSpeakers(adm, static_cast<lpc::MonitorFormat>(format), dm);
            if (dolby) {
                QTemporaryDir tmp;
                if (!tmp.isValid()) throw std::runtime_error("Cannot make a temporary folder");
                const std::filesystem::path wav = std::filesystem::path(tmp.path().toStdWString()) / "mix.wav";
                lpc::writeWav(wav, adm.sampleRate, 6, mix, lpc::WavFormat::Pcm24);
                QProcess encoder;
                encoder.start(ffmpeg, {"-nostdin", "-y", "-v", "error", "-i", QString::fromStdWString(wav.wstring()), "-af", "channelmap=channel_layout=5.1", "-c:a", plus ? "eac3" : "ac3", "-b:a", plus ? "768k" : "640k",
                                       QString::fromStdWString(out.wstring())});
                if (!encoder.waitForFinished(600000) || encoder.exitCode() != 0) throw std::runtime_error("FFmpeg failed: " + encoder.readAllStandardError().toStdString());
                message = QString(plus ? "Encoded Dolby Digital Plus 5.1" : "Encoded Dolby Digital 5.1");
            } else {
            lpc::writeWav(out, adm.sampleRate, lpc::monitorChannels(static_cast<lpc::MonitorFormat>(format)), mix, lpc::WavFormat::Pcm24);
            message = QString("Bounced %1 (%2 channels)").arg(lpc::monitorName(static_cast<lpc::MonitorFormat>(format))).arg(lpc::monitorChannels(static_cast<lpc::MonitorFormat>(format)));
            }
        } catch (const std::exception& e) {
            flag->store(false);
            failed = true;
            message = QString::fromUtf8(e.what());
        }
        if (self) QMetaObject::invokeMethod(self.data(), [self, message, failed] { if (self) { if (failed) self->setError(message); else emit self->notice(message); } }, Qt::QueuedConnection);
    });
}

// The position of an object at the playhead, for the panner's window: (x, y, z, size) from the parameters and the automation.
QVariantMap ProjectController::objectPosition(const QString& trackId) const {
    if (!host_) return {};
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    if (!id) return {};
    const lpc::Ticks tick = static_cast<lpc::Ticks>(std::llround(positionBeats_ * lpc::kPPQ));
    const lpc::AdmPosition pos = host_->read([id = *id, tick](const lpc::Project& p) {
                                          const lpc::Track* t = p.findTrack(id);
                                          return t ? lpc::objectPositionAt(p, *t, tick) : lpc::AdmPosition{};
                                      })
                                     .get();
    return {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}, {"size", pos.size}};
}

// File > Export > Project as Spatial Audio: options {range: "project"|"cycle", bitDepth: 16|24}.
void ProjectController::exportAdmBwf(const QUrl& file, const QVariantMap& options) {
    if (!host_) return;
    std::filesystem::path out = file.toLocalFile().toStdWString();
    if (out.extension().empty()) out += ".wav";
    if (!saveProject()) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    if (lpc::atmosObjects(project).empty() && !lpc::dolbyAtmosOn(project)) {
        setError("Turn on Dolby Atmos in Mix > Dolby Atmos and make some tracks objects first");
        return;
    }
    lpc::AtmosExportOptions render;
    render.bitDepth = options.value("bitDepth", 24).toInt();
    if (options.value("range", "project").toString() == QLatin1String("cycle")) {
        if (!(loopEndBeats_ > loopStartBeats_)) {
            setError("Set the cycle area first (drag in the top strip of the ruler)");
            return;
        }
        const auto toFrames = [this](double beats) { return static_cast<std::int64_t>(std::llround(tempoMap_.ticksToSamples(static_cast<lpc::Ticks>(std::llround(beats * lpc::kPPQ)), sampleRate_))); };
        render.startFrame = toFrames(loopStartBeats_);
        render.frames = toFrames(loopEndBeats_) - render.startFrame;
    }
    if (bouncing_->exchange(true)) {
        setError("A bounce is already running");
        return;
    }
    if (playing_) stop();
#ifdef JAD_HAVE_JUCE
    render.plugins = pluginHost_.get();
#endif
    emit notice("Exporting the Dolby Atmos master…");
    const std::filesystem::path root = dir_;
    QPointer<ProjectController> self(this);
    const std::shared_ptr<std::atomic<bool>> flag = bouncing_;
    (void)QtConcurrent::run([self, flag, project, root, out, render] {
        QString message;
        bool failed = false;
        try {
            lpc::MediaStore media(root, /*streaming=*/false);
            const lpc::AdmFile adm = lpc::renderAdm(project, media, render);
            flag->store(false);
            lpc::writeAdmBwf(out, adm);
            message = QString("Dolby Atmos master written: %1 object track%2").arg(adm.objects.size()).arg(adm.objects.size() == 1 ? "" : "s");
        } catch (const std::exception& e) {
            flag->store(false);
            failed = true;
            message = QString::fromUtf8(e.what());
        }
        if (self) QMetaObject::invokeMethod(self.data(), [self, message, failed] { if (self) { if (failed) self->setError(message); else emit self->notice(message); } }, Qt::QueuedConnection);
    });
}

// File > Import > Create Project from ADM BWF: the project is made in `folder` and opened.
void ProjectController::createProjectFromAdm(const QUrl& file, const QUrl& folder) {
    const std::filesystem::path in = file.toLocalFile().toStdWString();
    const std::filesystem::path path = toPath(folder);
    if (std::filesystem::exists(path / "project.json")) {
        setError("Cannot create project: this folder already contains a project");
        return;
    }
    emit notice("Reading the ADM BWF file…");
    QPointer<ProjectController> self(this);
    (void)QtConcurrent::run([self, in, path, folder] {
        QString error;
        try {
            const lpc::AdmFile adm = lpc::readAdmBwf(in);
            std::filesystem::create_directories(path);
            lpc::saveProject(lpc::projectFromAdm(adm, path), path);
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        }
        if (self) QMetaObject::invokeMethod(self.data(), [self, error, folder] {
            if (!self) return;
            if (!error.isEmpty()) self->setError("Cannot create the project from the ADM BWF file: " + error);
            else self->openProject(folder);
        }, Qt::QueuedConnection);
    });
}

}  // namespace jad
