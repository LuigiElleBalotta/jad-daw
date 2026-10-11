// The live link of the controller: what an AI assistant connected to the running app can ask (see live_link.h). Every request is a JSON object with an "op":
//   hello                      the app, the project, whether it plays
//   state                      the project as a mixer sees it (same JSON as `lpc-cli snapshot`)
//   meters                     playhead, master peak and every track's peak and hold (dB), gain reduction
//   apply  {commands: [...]}   commands of the project model, all in one undo step (one command is accepted as {command: {...}})
//   play | stop | locate {seconds} | undo | redo | reset_peaks
//   export {dir}               a copy of the project as it is now (saved or not) in `dir`, for `lpc-cli analyze` to listen to
#include <QDir>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "bridge/live_link.h"
#include "bridge/project_controller.h"
#include "lpc/model_json.h"
#include "lpc/project_host.h"
#include "lpc/project_io.h"
#include "lpc/project_summary.h"

namespace jad {

namespace {

using nlohmann::json;

double toDb(double linear) { return linear > 1e-6 ? std::round(20.0 * std::log10(linear) * 10.0) / 10.0 : -120.0; }

json failure(const std::string& message) { return {{"ok", false}, {"error", message}}; }

}  // namespace

bool ProjectController::liveLinkOn() const { return live_ && live_->running(); }

QString ProjectController::liveLinkInfoPath() const { return LiveLink::infoFilePath(); }

void ProjectController::setLiveLink(bool on) {
    if (on == liveLinkOn()) return;
    if (on) {
        if (!live_) live_ = std::make_unique<LiveLink>([this](const json& request, LiveLink::Reply reply) { liveHandle(request, std::move(reply)); });
        if (!live_->start()) {
            setError("The AI assistant link could not open a port");
            return;
        }
        emit notice(QString("AI assistant link on (port %1)").arg(live_->port()));
    } else {
        live_->stop();
        emit notice("AI assistant link off");
    }
    QSettings().setValue("ai/liveLink", on);
    emit liveLinkChanged();
}

void ProjectController::liveHandle(const json& request, std::function<void(const json&)> reply) {
    const std::string op = request.value("op", std::string());
    if (op == "hello") {
        reply({{"ok", true}, {"app", "JAD Daw"}, {"hasProject", hasProject()}, {"project", projectName().toStdString()}, {"playing", playing()},
               {"positionSeconds", positionSeconds()}, {"dirty", dirty()}, {"audioDegraded", degraded()}, {"deviceError", deviceError().toStdString()},
               {"lastError", lastError().toStdString()}});
        return;
    }
    if (!host_) {
        reply(failure("no project is open"));
        return;
    }
    try {
        if (op == "state") {
            const json summary = host_->read([](const lpc::Project& p) { return lpc::projectSummary(p); }).get();
            reply({{"ok", true}, {"project", summary}, {"playing", playing()}, {"positionSeconds", positionSeconds()}});
        } else if (op == "meters") {
            json tracks = json::array();
            for (const TrackRow& t : allRows_) {
                const double reduction = reduction_.value(t.id);
                json x = {{"id", t.id.toStdString()}, {"name", t.name.toStdString()}, {"peakDb", toDb(meter_.value(t.id))}, {"holdDb", toDb(hold_.value(t.id))}};
                if (reduction > 0.0) x["gainReductionDb"] = std::round(reduction * 10.0) / 10.0;
                tracks.push_back(std::move(x));
            }
            reply({{"ok", true}, {"playing", playing()}, {"recording", recording()}, {"positionSeconds", positionSeconds()}, {"positionBeats", positionBeats()},
                   {"masterPeakDb", toDb(masterPeak())}, {"tracks", tracks}});
        } else if (op == "apply") {
            json commands = json::array();
            if (request.contains("commands")) commands = request["commands"];
            else if (request.contains("command")) commands.push_back(request["command"]);
            if (!commands.is_array() || commands.empty()) {
                reply(failure("apply needs \"commands\": a list of commands"));
                return;
            }
            const json one = commands.size() == 1 ? commands.front() : json{{"type", "transaction"}, {"commands", commands}};
            setError(QString());
            sendCommand(one, [this, reply](bool ok) { reply(ok ? json{{"ok", true}} : failure(lastError().toStdString())); });
        } else if (op == "play") {
            play();
            reply({{"ok", true}});
        } else if (op == "stop") {
            stop();
            reply({{"ok", true}});
        } else if (op == "locate") {
            locateSeconds(request.value("seconds", 0.0));
            reply({{"ok", true}});
        } else if (op == "undo") {
            undo();
            reply({{"ok", true}});
        } else if (op == "redo") {
            redo();
            reply({{"ok", true}});
        } else if (op == "reset_peaks") {
            resetPeaks();
            reply({{"ok", true}});
        } else if (op == "export") {
            const std::string dirText = request.value("dir", std::string());
            if (dirText.empty()) {
                reply(failure("export needs \"dir\""));
                return;
            }
            const std::filesystem::path dest = std::filesystem::path(std::u8string(dirText.begin(), dirText.end()));
            commitPluginStates();
            host_->read([](const lpc::Project&) { return 0; }).get();
            const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
            std::filesystem::create_directories(dest);
            if (std::filesystem::weakly_canonical(dest) != std::filesystem::weakly_canonical(dir_))
                std::filesystem::copy(dir_, dest, std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing);
            lpc::saveProject(project, dest);
            reply({{"ok", true}, {"dir", dirText}});
        } else {
            reply(failure("unknown op: " + op));
        }
    } catch (const std::exception& e) {
        reply(failure(e.what()));
    }
}

}  // namespace jad
