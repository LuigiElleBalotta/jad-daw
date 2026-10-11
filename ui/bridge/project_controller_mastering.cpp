// Mastering Assistant in the controller: it is an insert ("builtin.mastering") on the stereo output. Adding it, or pressing Reanalyze, renders the
// project offline without that insert (only the cycle when one is set), measures the loudness and the balance of the mix, and writes what it
// found into the insert's parameters in one undo step; the insert then does the correction in real time.
#include <QPointer>
#include <QTimer>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <map>
#include <nlohmann/json.hpp>

#include "bridge/project_controller.h"
#include "lpc/mastering.h"
#include "lpc/media_store.h"
#include "lpc/model_json.h"
#include "lpc/offline_render.h"
#include "lpc/processor_ids.h"
#include "lpc/project_host.h"

namespace jad {

namespace {

// The loudness of the project as it sounds with the insert's parameters set to `params` (a render with the insert in place).
lpc::LoudnessStats outputStats(const lpc::Project& withInsert, const std::filesystem::path& root, const lpc::RenderOptions& render, const std::string& trackId, int index,
                               const std::map<std::string, double>& params) {
    lpc::Project p = withInsert;
    for (lpc::Track& t : p.tracks)
        if (t.id.toString() == trackId) t.strip.inserts[static_cast<std::size_t>(index)].params = params;
    lpc::MediaStore media(root, /*streaming=*/false);
    const lpc::RenderResult r = lpc::renderOffline(p, media, render);
    lpc::WavData out;
    out.sampleRate = r.sampleRate;
    out.channels = 2;
    out.samples = r.interleaved;
    return lpc::measureLoudness(out);
}

QVariantMap outputMap(const lpc::LoudnessStats& s) {
    return QVariantMap{{"outputLufs", s.integrated}, {"outputRange", s.range}, {"outputTruePeakDb", s.truePeakDb}, {"outputMomentaryMax", s.momentaryMax}};
}

}  // namespace

void ProjectController::addMasteringAssistant() {
    if (!host_ || masterId_.isEmpty()) return;
    for (const QVariant& v : trackInserts(masterId_))
        if (v.toMap().value("processorId").toString() == lpc::kProcMastering) {
            emit notice("Mastering Assistant is already on the output");
            return;
        }
    const QString master = masterId_;
    QPointer<ProjectController> self(this);
    sendCommand({{"type", "add_insert"}, {"trackId", master.toStdString()}, {"index", -1},
                 {"insert", {{"processorId", lpc::kProcMastering}, {"params", nlohmann::json::object()}, {"state", ""}}}},
                [self, master](bool accepted) {
                    if (!accepted || !self) return;
                    self->masteringAfterAdd(master, 0);
                });
}

// The insert is in the project once the snapshot with it has arrived; look for it until it shows up.
void ProjectController::masteringAfterAdd(const QString& master, int attempt) {
    const QVariantList inserts = trackInserts(master);
    for (int i = 0; i < inserts.size(); ++i)
        if (inserts[i].toMap().value("processorId").toString() == lpc::kProcMastering) {
            masteringAnalyse(master, i);
            return;
        }
    if (attempt >= 40) return;
    QPointer<ProjectController> self(this);
    QTimer::singleShot(100, this, [self, master, attempt] {
        if (self) self->masteringAfterAdd(master, attempt + 1);
    });
}

void ProjectController::masteringAnalyse(const QString& trackId, int index) {
    if (!host_) return;
    if (masteringBusy_) {
        emit notice("Mastering Assistant is still analysing");
        return;
    }
    lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    lpc::Track* track = nullptr;
    for (lpc::Track& t : project.tracks)
        if (t.id.toString() == trackId.toStdString()) track = &t;
    if (!track || index < 0 || index >= static_cast<int>(track->strip.inserts.size()) || track->strip.inserts[static_cast<std::size_t>(index)].processorId != lpc::kProcMastering) return;
    const lpc::Project withInsert = project;
    track->strip.inserts.erase(track->strip.inserts.begin() + index);  // what it listens to is the mix before it
    if (lpc::projectEndFrame(project) == 0) {
        emit notice("Mastering Assistant: the project has no sound to analyse yet");
        return;
    }
    lpc::RenderOptions render;
    render.tailSeconds = 0.3;
    if (loopEndBeats_ > loopStartBeats_) {  // a cycle: only that part is analysed
        const auto toFrames = [this](double beats) {
            return static_cast<std::int64_t>(std::llround(tempoMap_.ticksToSamples(static_cast<lpc::Ticks>(std::llround(beats * lpc::kPPQ)), sampleRate_)));
        };
        render.startFrame = toFrames(loopStartBeats_);
        render.frames = toFrames(loopEndBeats_) - render.startFrame;
    }
    masteringBusy_ = true;
    emit masteringBusyChanged();
    emit notice(loopEndBeats_ > loopStartBeats_ ? "Analysing the cycle…" : "Analysing the project…");
    const std::filesystem::path root = dir_;
    QPointer<ProjectController> self(this);
    const std::uint64_t generation = generation_;
    (void)QtConcurrent::run([self, project, withInsert, root, render, trackId, index, generation] {
        lpc::MasteringAnalysis a;
        double trim = 0.0;
        lpc::LoudnessStats output;
        QString error;
        try {
            lpc::MediaStore media(root, /*streaming=*/false);
            const lpc::RenderResult r = lpc::renderOffline(project, media, render);
            lpc::WavData mix;
            mix.sampleRate = r.sampleRate;
            mix.channels = 2;
            mix.samples = r.interleaved;
            a = lpc::analyseMastering(mix);
            if (a.before.integrated > -69.0) {
                // what the insert does to the loudness with these settings: Loudness Compensation is the trim that brings it back to the level of the mix
                std::map<std::string, double> base;
                for (const lpc::Track& t : withInsert.tracks)
                    if (t.id.toString() == trackId.toStdString()) base = t.strip.inserts[static_cast<std::size_t>(index)].params;
                base["gain"] = std::round(a.gainDb * 100.0) / 100.0;
                for (int b = 0; b < lpc::kMasteringBands; ++b) base["eq" + std::to_string(b + 1)] = std::round(a.eqDb[static_cast<std::size_t>(b)] * 100.0) / 100.0;
                base["bypass"] = 0.0;
                std::map<std::string, double> unity = base;
                unity["compensate"] = 1.0;
                unity["compTrim"] = 0.0;
                const lpc::LoudnessStats atUnity = outputStats(withInsert, root, render, trackId.toStdString(), index, unity);
                const double lost = a.before.integrated - atUnity.integrated;  // what the insert takes off the loudness at unity gain (EQ, compressor, soft clip)
                trim = std::round(std::clamp(lost, -12.0, 12.0) * 100.0) / 100.0;
                base["compTrim"] = trim;
                // the gain that reaches the target: the first estimate adds what is lost, then it is corrected from what is measured (a limiter that is
                // working hard gives back less than it is asked for, so it stops when more gain does not make it louder)
                double gain = std::clamp(a.gainDb + lost, -24.0, 36.0);
                base["gain"] = std::round(gain * 100.0) / 100.0;
                output = outputStats(withInsert, root, render, trackId.toStdString(), index, base);
                for (int pass = 0; pass < 3; ++pass) {
                    const double err = lpc::kMasteringTargetLufs - output.integrated;
                    if (std::abs(err) < 0.4) break;
                    const double candidate = std::clamp(gain + err, -24.0, 36.0);
                    if (candidate == gain) break;
                    std::map<std::string, double> trial = base;
                    trial["gain"] = std::round(candidate * 100.0) / 100.0;
                    const lpc::LoudnessStats got = outputStats(withInsert, root, render, trackId.toStdString(), index, trial);
                    if (std::abs(lpc::kMasteringTargetLufs - got.integrated) >= std::abs(err) - 0.2) break;  // no nearer: the limiter has taken what it could
                    gain = candidate;
                    base = trial;
                    output = got;
                }
                a.gainDb = std::round(gain * 100.0) / 100.0;
            }
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, a, error, trackId, index, generation, trim, output] {
            if (!self) return;
            self->masteringBusy_ = false;
            emit self->masteringBusyChanged();
            if (generation != self->generation_) return;
            if (!error.isEmpty()) {
                self->setError("Mastering Assistant: " + error);
                return;
            }
            if (a.before.integrated < -69.0) {
                emit self->notice("Mastering Assistant: the mix is silent");
                return;
            }
            self->masteringStats_ = QVariantMap{{"integratedLufs", a.before.integrated}, {"momentaryMax", a.before.momentaryMax}, {"shortTermMax", a.before.shortTermMax},
                                                {"range", a.before.range}, {"truePeakDb", a.before.truePeakDb}, {"gainDb", a.gainDb}, {"compTrim", trim}};
            const QVariantMap outMap = outputMap(output);
            for (auto it = outMap.cbegin(); it != outMap.cend(); ++it) self->masteringStats_.insert(it.key(), it.value());
            nlohmann::json commands = nlohmann::json::array();
            auto set = [&](const std::string& param, double value) {
                commands.push_back({{"type", "set_insert_param"}, {"trackId", trackId.toStdString()}, {"index", index}, {"param", param}, {"value", value}});
            };
            set("gain", std::round(a.gainDb * 100.0) / 100.0);
            set("compTrim", trim);
            for (int b = 0; b < lpc::kMasteringBands; ++b) set("eq" + std::to_string(b + 1), std::round(a.eqDb[static_cast<std::size_t>(b)] * 100.0) / 100.0);
            self->sendCommand({{"type", "transaction"}, {"commands", commands}});
            emit self->masteringAnalysed();
        }, Qt::QueuedConnection);
    });
}

// The loudness and true peak of the project as it sounds now, with the insert on (the "Output" line of the window).
void ProjectController::masteringMeasure(const QString& trackId, int index) {
    if (!host_ || masteringBusy_) return;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::Track* track = nullptr;
    for (const lpc::Track& t : project.tracks)
        if (t.id.toString() == trackId.toStdString()) track = &t;
    if (!track || index < 0 || index >= static_cast<int>(track->strip.inserts.size()) || lpc::projectEndFrame(project) == 0) return;
    std::map<std::string, double> params = track->strip.inserts[static_cast<std::size_t>(index)].params;
    params["bypass"] = 0.0;
    lpc::RenderOptions render;
    render.tailSeconds = 0.3;
    if (loopEndBeats_ > loopStartBeats_) {
        const auto toFrames = [this](double beats) {
            return static_cast<std::int64_t>(std::llround(tempoMap_.ticksToSamples(static_cast<lpc::Ticks>(std::llround(beats * lpc::kPPQ)), sampleRate_)));
        };
        render.startFrame = toFrames(loopStartBeats_);
        render.frames = toFrames(loopEndBeats_) - render.startFrame;
    }
    masteringBusy_ = true;
    emit masteringBusyChanged();
    const std::filesystem::path root = dir_;
    QPointer<ProjectController> self(this);
    const std::uint64_t generation = generation_;
    (void)QtConcurrent::run([self, project, root, render, trackId, index, params, generation] {
        lpc::LoudnessStats out;
        QString error;
        try {
            out = outputStats(project, root, render, trackId.toStdString(), index, params);
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, out, error, generation] {
            if (!self) return;
            self->masteringBusy_ = false;
            emit self->masteringBusyChanged();
            if (generation != self->generation_) return;
            if (!error.isEmpty()) {
                self->setError("Mastering Assistant: " + error);
                return;
            }
            const QVariantMap m = outputMap(out);
            for (auto it = m.cbegin(); it != m.cend(); ++it) self->masteringStats_.insert(it.key(), it.value());
            emit self->masteringAnalysed();
        }, Qt::QueuedConnection);
    });
}

}  // namespace jad
