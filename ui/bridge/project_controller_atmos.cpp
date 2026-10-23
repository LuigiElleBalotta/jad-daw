// Spatial Audio with Dolby Atmos: object tracks are tracks with a 3D Object Panner; the master is exported as an ADM BWF file (File > Export > Project as Spatial Audio)
// and a project can be made from one (File > Import > Create Project from ADM BWF). Project settings: spatialAudio ("dolbyAtmos" when on), atmosMonitor ("binaural" or "stereo").
#include <QDir>
#include <QPointer>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "bridge/project_controller.h"
#include "lpc/adm_bwf.h"
#include "lpc/atmos.h"
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
        const bool stereo = projectSetting("atmosMonitor", "binaural") == QLatin1String("stereo");
        sendCommand({{"type", "add_insert"}, {"trackId", trackId.toStdString()}, {"index", -1},
                     {"insert", {{"processorId", lpc::kProcObjectPanner}, {"params", {{"mode", stereo ? 0.0 : 1.0}}}, {"state", ""}}}});
    } else if (!object && at >= 0) {
        removeInsert(trackId, at);
    }
}

// Listening to the objects over headphones (binaural) or as a plain stereo pan: the panners follow.
void ProjectController::setAtmosMonitor(const QString& mode) {
    if (mode != QLatin1String("binaural") && mode != QLatin1String("stereo")) return;
    nlohmann::json commands = nlohmann::json::array();
    commands.push_back({{"type", "set_project_setting"}, {"key", "atmosMonitor"}, {"value", mode.toStdString()}});
    for (const TrackRow& t : allRows_)
        for (std::size_t i = 0; i < t.inserts.size(); ++i)
            if (t.inserts[i].processorId == QLatin1String(lpc::kProcObjectPanner))
                commands.push_back({{"type", "set_insert_param"}, {"trackId", t.id.toStdString()}, {"index", static_cast<int>(i)}, {"param", "mode"}, {"value", mode == QLatin1String("binaural") ? 1.0 : 0.0}});
    sendCommand({{"type", "transaction"}, {"commands", commands}});
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
