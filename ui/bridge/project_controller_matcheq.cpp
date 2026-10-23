// Match EQ in the controller: Learn renders what reaches the insert (the track alone, or the mix on the master), measures its spectrum, works out
// the correction towards a factory reference curve and writes it into the insert's parameters b0..b30 in one undo step.
#include <QPointer>
#include <QtConcurrent>

#include <cmath>
#include <nlohmann/json.hpp>

#include "bridge/project_controller.h"
#include "lpc/effect_presets.h"
#include "lpc/match_eq.h"
#include "lpc/match_eq_fit.h"
#include "lpc/processor_ids.h"
#include "lpc/project_host.h"

namespace jad {

void ProjectController::matchEqApplyBands(const QString& trackId, int index, const QVariantList& gains) {
    if (!host_ || gains.size() != lpc::kMatchEqBands) return;
    nlohmann::json commands = nlohmann::json::array();
    for (int b = 0; b < lpc::kMatchEqBands; ++b) {
        const double g = gains.at(b).toDouble();
        commands.push_back({{"type", "set_insert_param"}, {"trackId", trackId.toStdString()}, {"index", index}, {"param", "b" + std::to_string(b)}, {"value", std::isfinite(g) ? std::clamp(g, -30.0, 30.0) : 0.0}});
    }
    sendCommand({{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::matchEqLearn(const QString& trackId, int index, const QString& curveName) {
    if (!host_ || !effectPresets_) return;
    if (masteringBusy_) {
        emit notice("Match EQ is still analysing");
        return;
    }
    const std::vector<double>* target = effectPresets_->matchEqCurve(curveName.toStdString());
    if (!target) {
        emit notice("Match EQ: choose a reference curve first");
        return;
    }
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
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
    emit notice("Match EQ: analysing…");
    const std::filesystem::path root = dir_;
    const std::vector<double> curve = *target;
    QPointer<ProjectController> self(this);
    const std::uint64_t generation = generation_;
    (void)QtConcurrent::run([self, project, root, render, trackId, index, curve, generation] {
        lpc::MatchEqFit fit;
        QString error;
        try {
            fit = lpc::fitMatchEq(project, root, lpc::Uuid::parse(trackId.toStdString()).value_or(lpc::Uuid{}), index, curve, render);
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, fit, error, trackId, index, generation] {
            if (!self) return;
            self->masteringBusy_ = false;
            emit self->masteringBusyChanged();
            if (generation != self->generation_) return;
            if (!error.isEmpty()) {
                self->setError("Match EQ: " + error);
                return;
            }
            if (fit.silent) {
                emit self->notice("Match EQ: there is no sound to analyse");
                return;
            }
            nlohmann::json commands = nlohmann::json::array();
            for (int b = 0; b < lpc::kMatchEqBands; ++b)
                commands.push_back({{"type", "set_insert_param"}, {"trackId", trackId.toStdString()}, {"index", index}, {"param", "b" + std::to_string(b)},
                                    {"value", fit.params.at("b" + std::to_string(b))}});
            self->sendCommand({{"type", "transaction"}, {"commands", commands}});
            emit self->notice("Match EQ: learned");
        }, Qt::QueuedConnection);
    });
}

}  // namespace jad
