// Flex Time and Flex Pitch in the controller.
//
// A flexed region plays a file that was rendered from the sound it was recorded as (Region::flex says from which, and with what
// markers and note edits). Every edit asks for a new rendering in a worker thread and, when it is done, replaces the region in one
// command together with the new file, so each edit is one undo step. Meanwhile the note under the mouse is heard through the engine's
// audition voice, rendered the same way but only for that note.
#include <QCryptographicHash>
#include <QFile>
#include <QPointer>
#include <QSettings>
#include <QUuid>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>

#include "bridge/project_controller.h"
#include "lpc/audio/audition.h"
#include "lpc/flex_model.h"
#include "lpc/flex_pitch.h"
#include "lpc/flex_time.h"
#include "lpc/model_json.h"
#include "lpc/project_host.h"
#include "lpc/wav.h"

namespace jad {

namespace {

std::filesystem::path mediaPath(const std::filesystem::path& dir, const QString& relative) {
    const QByteArray rel = relative.toUtf8();
    return dir / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(rel.constData()), static_cast<std::size_t>(rel.size())));
}

lpc::WavData readWindow(const std::filesystem::path& file, std::int64_t offset, std::int64_t frames) {
    lpc::WavFile wav(file);
    lpc::WavData d;
    d.sampleRate = wav.sampleRate();
    d.channels = wav.channels();
    frames = std::max<std::int64_t>(0, std::min(frames, wav.frames() - offset));
    d.samples.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(d.channels));
    wav.readFrames(offset, frames, d.samples.data());
    return d;
}

QString hashOf(const std::filesystem::path& file) {
    QFile f(QString::fromStdU16String(file.u16string()));
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (f.open(QIODevice::ReadOnly) && h.addData(&f)) return QString::fromLatin1(h.result().toHex());
    return {};
}

// The flex data of a region: its own, or what it would get now (the whole of what it plays, unflexed).
lpc::FlexData flexDataOf(const RegionRow& row) {
    const nlohmann::json j = nlohmann::json::parse(row.json, nullptr, false);
    if (!j.is_discarded() && j.contains("flex") && !j["flex"].is_null()) {
        try {
            return j.at("flex").get<lpc::FlexData>();
        } catch (const std::exception&) {
        }
    }
    lpc::FlexData f;
    f.sourceMediaId = lpc::Uuid::parse(row.mediaId.toStdString()).value_or(lpc::Uuid{});
    f.sourceOffset = row.sourceOffsetFrames;
    f.sourceLength = std::max<std::int64_t>(1, row.lengthFrames);
    return f;
}

QString windowKey(const lpc::FlexData& f) { return QString("%1:%2:%3").arg(QString::fromStdString(f.sourceMediaId.toString())).arg(f.sourceOffset).arg(f.sourceLength); }

bool neutral(const lpc::FlexNoteData& n) {
    return n.semitones == 0.0 && n.fineCents == 0.0 && n.correct == 0.0 && n.vibrato == 1.0 && n.driftIn == 0.0 && n.driftOut == 0.0 && n.gainDb == 0.0 && n.formantCents == 0.0;
}

// Does the sound have to be rendered again, or is it the recorded one?
bool needsBake(const lpc::FlexData& f, std::int64_t dstFrames) {
    if (dstFrames != f.sourceLength) return true;
    for (const lpc::FlexMarker& m : f.markers)
        if (m.src != m.dst) return true;
    for (const lpc::FlexNoteData& n : f.notes)
        if (!neutral(n)) return true;
    return false;
}

QVariantMap noteMap(int index, const lpc::FlexNoteData& n, const lpc::TimeMap& map, double total) {
    return QVariantMap{{"index", index},
                       {"start", map.toDest(static_cast<double>(n.start)) / total},
                       {"end", map.toDest(static_cast<double>(n.start + n.length)) / total},
                       {"pitch", n.pitch + n.semitones + n.fineCents / 100.0},
                       {"original", n.pitch},
                       {"semitones", n.semitones}, {"fineCents", n.fineCents}, {"correct", n.correct}, {"vibrato", n.vibrato},
                       {"driftIn", n.driftIn}, {"driftOut", n.driftOut}, {"gainDb", n.gainDb}, {"formantCents", n.formantCents}};
}

lpc::FlexTimeParams timeParams(const std::string& mode, const std::map<std::string, double>& p) {
    lpc::FlexTimeParams t;
    t.mode = (mode == "pitch" || mode.empty()) ? lpc::FlexMode::Monophonic : lpc::flexModeFromName(mode.c_str());
    if (t.mode == lpc::FlexMode::Off) t.mode = lpc::FlexMode::Monophonic;
    auto get = [&](const char* key, double fallback) {
        const auto it = p.find(key);
        return it == p.end() ? fallback : it->second;
    };
    t.decay = std::clamp(get("decay", 0.5), 0.0, 1.0);
    t.sliceLength = std::clamp(get("sliceLength", 1.0), 0.05, 1.0);
    t.loopLengthMs = std::clamp(get("loopLengthMs", 20.0), 2.0, 200.0);
    t.loopOffsetMs = std::clamp(get("loopOffsetMs", 0.0), 0.0, 200.0);
    t.grainMs = std::clamp(get("grainMs", 30.0), 5.0, 200.0);
    return t;
}

double sensitivityOf(const std::map<std::string, double>& p) {
    const auto it = p.find("sensitivity");
    return it == p.end() ? 0.5 : std::clamp(it->second, 0.0, 1.0);
}

std::map<std::string, double> paramsOf(const TrackRow& t) {
    std::map<std::string, double> p;
    for (auto it = t.flexParams.begin(); it != t.flexParams.end(); ++it) p[it.key().toStdString()] = it.value().toDouble();
    return p;
}

// stereo float buffers at the engine's rate, with short fades so that a repeating clip does not click
lpc::audio::AuditionClip makeClip(const lpc::WavData& d, int engineRate) {
    lpc::audio::AuditionClip clip;
    if (d.frames() == 0 || d.sampleRate <= 0 || d.channels <= 0) return clip;
    const double ratio = static_cast<double>(d.sampleRate) / engineRate;
    const std::int64_t n = static_cast<std::int64_t>(static_cast<double>(d.frames()) / ratio);
    clip.left.resize(static_cast<std::size_t>(n));
    clip.right.resize(static_cast<std::size_t>(n));
    auto at = [&](std::int64_t i, int c) { return d.samples[static_cast<std::size_t>(std::clamp<std::int64_t>(i, 0, d.frames() - 1) * d.channels + std::min(c, d.channels - 1))]; };
    const std::int64_t fade = std::max<std::int64_t>(1, std::min<std::int64_t>(static_cast<std::int64_t>(0.004 * engineRate), n / 4));
    for (std::int64_t i = 0; i < n; ++i) {
        const double pos = static_cast<double>(i) * ratio;
        const std::int64_t a = static_cast<std::int64_t>(pos);
        const float f = static_cast<float>(pos - static_cast<double>(a));
        double g = 1.0;
        if (i < fade) g = static_cast<double>(i) / fade;
        else if (n - i <= fade) g = static_cast<double>(n - i) / fade;
        clip.left[static_cast<std::size_t>(i)] = static_cast<float>(g * (at(a, 0) * (1 - f) + at(a + 1, 0) * f));
        clip.right[static_cast<std::size_t>(i)] = static_cast<float>(g * (at(a, 1) * (1 - f) + at(a + 1, 1) * f));
    }
    return clip;
}

}  // namespace

// ---------------------------------------------------------------- state

struct ProjectController::FlexState {
    struct Entry {
        bool transientsDone = false, pitchDone = false, running = false;
        double sensitivity = -1.0;
        std::vector<std::int64_t> transients;
        std::shared_ptr<lpc::PitchAnalysis> pitch;
    };
    QHash<QString, Entry> entries;  // by the window of the source they describe
    QHash<QString, int> tokens;     // by region: the rendering asked for last
    QString audioKey;               // the sound the preview works on
    std::shared_ptr<lpc::WavData> audio;
    bool previewBusy = false;
    struct Pending {
        QString regionId;
        int index = 0;
        QVariantMap edit;
    };
    std::optional<Pending> pending;
};

ProjectController::FlexState& ProjectController::flexState() {
    if (!flex_) flex_ = std::make_shared<FlexState>();
    return *flex_;
}

void ProjectController::setFlexView(bool on) {
    if (on == flexView_) return;
    flexView_ = on;
    emit flexViewChanged();
}

bool ProjectController::flexAvailable(const QString& regionId) const {
    const RegionRow* row = regions_.find(regionId);
    if (!row || !row->audio || row->missing) return false;
    const TrackRow* track = tracks_.find(row->trackId);
    return track && !track->flexMode.isEmpty();
}

// ---------------------------------------------------------------- analysis

void ProjectController::flexAnalyse(const QString& regionId) {
    const RegionRow* row = regions_.find(regionId);
    if (!host_ || !row || !row->audio || row->missing || row->lengthFrames <= 0) return;
    const TrackRow* track = tracks_.find(row->trackId);
    if (!track) return;
    const lpc::FlexData data = flexDataOf(*row);
    const QString key = windowKey(data);
    FlexState& st = flexState();
    FlexState::Entry& entry = st.entries[key];
    const bool wantPitch = track->flexMode == "pitch";
    const double sensitivity = sensitivityOf(paramsOf(*track));
    const bool transientsOk = entry.transientsDone && entry.sensitivity == sensitivity;
    if (transientsOk && (!wantPitch || entry.pitchDone)) {
        emit flexAnalysisReady(regionId);
        return;
    }
    if (entry.running) return;
    entry.running = true;
    const std::filesystem::path file = mediaPath(dir_, mediaPaths_.value(QString::fromStdString(data.sourceMediaId.toString())));
    const bool doTransients = !transientsOk, doPitch = wantPitch && !entry.pitchDone;
    QPointer<ProjectController> self(this);
    const std::uint64_t generation = generation_;
    (void)QtConcurrent::run([self, key, regionId, file, data, doTransients, doPitch, sensitivity, generation] {
        std::vector<std::int64_t> transients;
        std::shared_ptr<lpc::PitchAnalysis> pitch;
        bool ok = true;
        try {
            const lpc::WavData wav = readWindow(file, data.sourceOffset, data.sourceLength);
            if (doTransients) transients = lpc::findTransients(wav, sensitivity);
            if (doPitch) pitch = std::make_shared<lpc::PitchAnalysis>(lpc::analysePitch(wav));
        } catch (const std::exception&) {
            ok = false;
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, key, regionId, transients = std::move(transients), pitch, doTransients, doPitch, sensitivity, ok, generation]() mutable {
            if (!self || generation != self->generation_) return;
            FlexState::Entry& e = self->flexState().entries[key];
            e.running = false;
            if (!ok) return;
            if (doTransients) {
                e.transients = std::move(transients);
                e.transientsDone = true;
                e.sensitivity = sensitivity;
            }
            if (doPitch) {
                e.pitch = pitch;
                e.pitchDone = true;
            }
            emit self->flexAnalysisReady(regionId);
        }, Qt::QueuedConnection);
    });
}

QVariantList ProjectController::flexTransients(const QString& regionId) {
    const RegionRow* row = regions_.find(regionId);
    if (!row || !row->audio || row->missing || row->lengthFrames <= 0) return {};
    const lpc::FlexData data = flexDataOf(*row);
    const FlexState::Entry entry = flexState().entries.value(windowKey(data));
    const TrackRow* track = tracks_.find(row->trackId);
    if (!entry.transientsDone || (track && entry.sensitivity != sensitivityOf(paramsOf(*track)))) {
        flexAnalyse(regionId);
        if (!entry.transientsDone) return {};
    }
    const lpc::TimeMap map(data.sourceLength, row->lengthFrames, lpc::flexAnchors(data));
    QVariantList out;
    for (std::int64_t t : entry.transients) {
        const double frac = map.toDest(static_cast<double>(t)) / static_cast<double>(row->lengthFrames);
        if (frac > 0.0 && frac < 1.0) out.append(frac);
    }
    return out;
}

// The flex data of the region, with its notes (from the analysis when it has none yet). False while the analysis is still to come.
bool ProjectController::flexPrepare(const QString& regionId, lpc::FlexData& data, bool needNotes) {
    const RegionRow* row = regions_.find(regionId);
    if (!host_ || !row || !row->audio || row->missing || row->lengthFrames <= 0) return false;
    data = flexDataOf(*row);
    if (!needNotes || !data.notes.empty()) return true;
    const FlexState::Entry entry = flexState().entries.value(windowKey(data));
    if (!entry.pitchDone || !entry.pitch) {
        flexAnalyse(regionId);
        emit notice("Analysing the pitch…");
        return false;
    }
    data.notes = lpc::flexNoteData(entry.pitch->notes);
    return true;
}

// ---------------------------------------------------------------- rendering

void ProjectController::flexApplyOne(const QString& regionId, const lpc::FlexData& data, const QString& what) {
    flexApply({FlexEdit{regionId, data, false, {}, {}}}, nlohmann::json::array(), what);
}

void ProjectController::flexApply(std::vector<FlexEdit> edits, const nlohmann::json& prefix, const QString& what) {
    if (!host_) return;
    auto sendPrefix = [&] {
        if (prefix.empty()) return;
        sendCommand(prefix.size() == 1 ? prefix.front() : nlohmann::json{{"type", "transaction"}, {"commands", prefix}});
    };
    struct Job {
        QString regionId;
        lpc::FlexData data;
        std::string mode;
        std::map<std::string, double> params;
        std::filesystem::path source;
        std::int64_t dstFrames = 0;
        bool bake = false;
        std::vector<std::int64_t> transients;
        bool haveTransients = false;
        std::shared_ptr<lpc::PitchAnalysis> pitch;
        double sensitivity = 0.5;
        int token = 0;
    };
    std::vector<Job> jobs;
    FlexState& st = flexState();
    for (FlexEdit& e : edits) {
        const RegionRow* row = regions_.find(e.regionId);
        const TrackRow* track = row ? tracks_.find(row->trackId) : nullptr;
        if (!row || !track || !row->audio || row->missing || row->lengthFrames <= 0) continue;
        Job job;
        job.regionId = e.regionId;
        job.data = std::move(e.data);
        job.mode = e.overrideTrack ? e.mode : track->flexMode.toStdString();
        job.params = e.overrideTrack ? e.params : paramsOf(*track);
        job.sensitivity = sensitivityOf(job.params);
        job.dstFrames = row->lengthFrames;
        job.bake = needsBake(job.data, job.dstFrames);
        job.source = mediaPath(dir_, mediaPaths_.value(QString::fromStdString(job.data.sourceMediaId.toString())));
        const FlexState::Entry entry = st.entries.value(windowKey(job.data));
        if (entry.transientsDone && entry.sensitivity == job.sensitivity) {
            job.transients = entry.transients;
            job.haveTransients = true;
        }
        job.pitch = entry.pitch;
        job.token = ++st.tokens[job.regionId];
        jobs.push_back(std::move(job));
    }
    if (jobs.empty()) {
        sendPrefix();
        return;
    }
    ++flexBusy_;
    emit flexBusyChanged();
    if (!what.isEmpty()) emit notice(what + "…");
    const std::filesystem::path projectDir = dir_;
    const int rate = sampleRate_;
    QPointer<ProjectController> self(this);
    const std::uint64_t generation = generation_;
    (void)QtConcurrent::run([self, jobs = std::move(jobs), prefix, projectDir, rate, generation]() mutable {
        struct Result {
            Job job;
            std::filesystem::path target;
            QString hash;
            std::int64_t frames = 0;
            int channels = 0, sampleRate = 0;
            QString error;
        };
        std::vector<Result> results;
        for (Job& job : jobs) {
            Result r;
            r.job = std::move(job);
            Job& j = r.job;
            if (j.bake) {
                try {
                    const lpc::WavData src = readWindow(j.source, j.data.sourceOffset, j.data.sourceLength);
                    lpc::WavData work = src;
                    bool pitchEdited = false;
                    for (const lpc::FlexNoteData& n : j.data.notes) pitchEdited = pitchEdited || !neutral(n);
                    if (pitchEdited) {
                        lpc::PitchAnalysis a = j.pitch ? *j.pitch : lpc::analysePitch(src);
                        a.notes = lpc::flexNotes(j.data);
                        work = lpc::renderFlexPitch(src, a, lpc::flexEdits(j.data));
                    }
                    bool timeEdited = j.dstFrames != j.data.sourceLength;
                    for (const lpc::FlexMarker& m : j.data.markers) timeEdited = timeEdited || m.src != m.dst;
                    if (timeEdited) {
                        if (!j.haveTransients) j.transients = lpc::findTransients(src, j.sensitivity);
                        work = lpc::renderFlexTime(work, lpc::flexAnchors(j.data), j.transients, timeParams(j.mode, j.params), j.dstFrames);
                    }
                    r.frames = work.frames();
                    r.channels = work.channels;
                    r.sampleRate = work.sampleRate;
                    const std::filesystem::path audioDir = projectDir / "audio";
                    std::filesystem::create_directories(audioDir);
                    std::u16string stem = j.source.stem().u16string();
                    if (const std::size_t at = stem.find(u" (flex"); at != std::u16string::npos) stem.erase(at);  // a render of a render is named after the recording
                    for (int n = 1;; ++n) {
                        r.target = audioDir / (stem + u" (flex" + (n > 1 ? u" " + QString::number(n).toStdU16String() : std::u16string()) + u").wav");
                        if (!std::filesystem::exists(r.target)) break;
                    }
                    lpc::writeWav(r.target, work.sampleRate > 0 ? work.sampleRate : rate, work.channels, work.samples, lpc::WavFormat::Pcm24);
                    r.hash = hashOf(r.target);
                } catch (const std::exception& e) {
                    r.error = QString("Cannot render the flex edit: ") + QString::fromUtf8(e.what());
                    r.target.clear();
                }
            }
            results.push_back(std::move(r));
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, results = std::move(results), prefix, projectDir, rate, generation]() mutable {
            std::error_code ignore;
            auto discard = [&] {
                for (auto& r : results)
                    if (!r.target.empty()) std::filesystem::remove(r.target, ignore);
            };
            if (!self) {
                discard();
                return;
            }
            --self->flexBusy_;
            emit self->flexBusyChanged();
            if (generation != self->generation_ || !self->host_ || self->dir_ != projectDir) {
                discard();
                return;
            }
            nlohmann::json commands = prefix;
            std::vector<std::filesystem::path> files;
            for (auto& r : results) {
                const Job& j = r.job;
                if (!r.error.isEmpty()) {
                    self->setError(r.error);
                    continue;
                }
                const RegionRow* row = self->regions_.find(j.regionId);
                if (!row || row->lengthFrames != j.dstFrames || self->flexState().tokens.value(j.regionId) != j.token) {
                    if (!r.target.empty()) std::filesystem::remove(r.target, ignore);  // the region went away, changed, or a newer edit is on its way
                    continue;
                }
                nlohmann::json region = nlohmann::json::parse(row->json, nullptr, false);
                if (region.is_discarded()) continue;
                if (j.bake) {
                    lpc::MediaItem item;
                    item.id = lpc::Uuid::random();
                    const std::u8string rel = (std::filesystem::path("audio") / r.target.filename()).generic_u8string();
                    item.path.assign(rel.begin(), rel.end());
                    item.hash = r.hash.toStdString();
                    item.sampleRate = r.sampleRate > 0 ? r.sampleRate : rate;
                    item.channels = r.channels;
                    item.frames = r.frames;
                    commands.push_back({{"type", "add_media"}, {"item", item}, {"index", -1}});
                    region["mediaId"] = item.id.toString();
                    region["sourceOffsetFrames"] = 0;
                    files.push_back(r.target);
                } else {
                    region["mediaId"] = j.data.sourceMediaId.toString();
                    region["sourceOffsetFrames"] = j.data.sourceOffset;
                }
                if (!j.data.markers.empty() || !j.data.notes.empty()) region["flex"] = j.data;
                else region.erase("flex");
                commands.push_back({{"type", "replace_region"}, {"region", region}});
            }
            if (commands.empty()) return;
            self->sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}}, [files](bool accepted) {
                if (accepted) return;
                std::error_code ec;
                for (const auto& f : files) std::filesystem::remove(f, ec);
            });
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------- the track

void ProjectController::setTrackFlexMode(const QString& trackId, const QString& mode) {
    const TrackRow* track = tracks_.find(trackId);
    if (!host_ || !track || track->kind != "audio") return;
    static const QStringList modes{"", "slicing", "rhythmic", "monophonic", "polyphonic", "tempophone", "speed", "automatic", "pitch"};
    if (!modes.contains(mode)) return;
    nlohmann::json prefix = nlohmann::json::array();
    prefix.push_back({{"type", "set_track_props"}, {"trackId", trackId.toStdString()}, {"flexMode", mode.toStdString()}});
    // the regions that are flexed are rendered again with the new algorithm, in the same undo step
    std::vector<FlexEdit> edits;
    if (!mode.isEmpty()) {
        for (const RegionRow& r : regions_.rows()) {
            if (r.trackId != trackId || !r.flexed) continue;
            lpc::FlexData data = flexDataOf(r);
            if (data.markers.empty()) continue;
            edits.push_back(FlexEdit{r.id, std::move(data), true, mode.toStdString(), paramsOf(*track)});
        }
    }
    if (!edits.empty()) {
        flexApply(std::move(edits), prefix, "Applying the algorithm");
        return;
    }
    QPointer<ProjectController> self(this);
    sendCommand(prefix.front(), [self, trackId](bool accepted) {
        if (!accepted || !self) return;
        QTimer::singleShot(0, self.data(), [self, trackId] {  // the snapshot with the new mode is in; start looking at the sound
            if (!self) return;
            for (const RegionRow& r : self->regions_.rows())
                if (r.trackId == trackId && r.audio) self->flexAnalyse(r.id);
        });
    });
}

void ProjectController::setTrackFlexParam(const QString& trackId, const QString& key, double value) {
    const TrackRow* track = tracks_.find(trackId);
    if (!host_ || !track || track->kind != "audio" || !std::isfinite(value)) return;
    std::map<std::string, double> params = paramsOf(*track);
    params[key.toStdString()] = value;
    nlohmann::json p = nlohmann::json::object();
    for (const auto& [k, v] : params) p[k] = v;
    nlohmann::json prefix = nlohmann::json::array();
    prefix.push_back({{"type", "set_track_props"}, {"trackId", trackId.toStdString()}, {"flexParams", p}});
    std::vector<FlexEdit> edits;
    if (key != "sensitivity") {
        for (const RegionRow& r : regions_.rows()) {
            if (r.trackId != trackId || !r.flexed) continue;
            lpc::FlexData data = flexDataOf(r);
            if (data.markers.empty()) continue;
            edits.push_back(FlexEdit{r.id, std::move(data), true, track->flexMode.toStdString(), params});
        }
    }
    if (!edits.empty()) flexApply(std::move(edits), prefix, "Applying the setting");
    else sendCommand(prefix.front());
}

// ---------------------------------------------------------------- Flex Time: the markers

namespace {
constexpr double kNearSeconds = 0.015;
}

void ProjectController::flexAddMarker(const QString& regionId, double fraction, bool threeMarkers) {
    lpc::FlexData data;
    if (!std::isfinite(fraction) || !flexPrepare(regionId, data, false)) return;
    const RegionRow* row = regions_.find(regionId);
    const lpc::TimeMap map(data.sourceLength, row->lengthFrames, lpc::flexAnchors(data));
    const FlexState::Entry entry = flexState().entries.value(windowKey(data));
    const double dst = std::clamp(fraction, 0.0, 1.0) * static_cast<double>(row->lengthFrames);
    const double near = kNearSeconds * sampleRate_;
    // snap to the transient that is close, if there is one
    std::int64_t src = static_cast<std::int64_t>(std::llround(map.toSource(dst)));
    std::int64_t dstFrame = static_cast<std::int64_t>(std::llround(dst));
    int snapped = -1;
    for (std::size_t i = 0; i < entry.transients.size(); ++i) {
        if (std::abs(map.toDest(static_cast<double>(entry.transients[i])) - dst) <= near && (snapped < 0 || std::abs(map.toDest(static_cast<double>(entry.transients[i])) - dst) < std::abs(map.toDest(static_cast<double>(entry.transients[static_cast<std::size_t>(snapped)])) - dst)))
            snapped = static_cast<int>(i);
    }
    std::vector<lpc::FlexMarker> add;
    auto put = [&](std::int64_t s) {
        if (s <= 0 || s >= data.sourceLength) return;
        const std::int64_t d = static_cast<std::int64_t>(std::llround(map.toDest(static_cast<double>(s))));
        if (d <= 0 || d >= row->lengthFrames) return;
        for (const lpc::FlexMarker& m : data.markers)
            if (std::abs(m.src - s) < sampleRate_ / 500) return;
        add.push_back({s, d});
    };
    if (snapped >= 0) {
        put(entry.transients[static_cast<std::size_t>(snapped)]);
        if (threeMarkers) {
            if (snapped > 0) put(entry.transients[static_cast<std::size_t>(snapped) - 1]);
            if (static_cast<std::size_t>(snapped) + 1 < entry.transients.size()) put(entry.transients[static_cast<std::size_t>(snapped) + 1]);
        }
    } else {
        if (src > 0 && src < data.sourceLength && dstFrame > 0 && dstFrame < row->lengthFrames) {
            bool clash = false;
            for (const lpc::FlexMarker& m : data.markers) clash = clash || std::abs(m.src - src) < sampleRate_ / 500;
            if (!clash) add.push_back({src, dstFrame});
        }
    }
    if (add.empty()) return;
    data.markers.insert(data.markers.end(), add.begin(), add.end());
    std::sort(data.markers.begin(), data.markers.end(), [](const lpc::FlexMarker& a, const lpc::FlexMarker& b) { return a.src < b.src; });
    flexApplyOne(regionId, data, "");
}

void ProjectController::flexMoveMarker(const QString& regionId, int index, double fraction, bool slide) {
    lpc::FlexData data;
    if (!std::isfinite(fraction) || !flexPrepare(regionId, data, false)) return;
    const RegionRow* row = regions_.find(regionId);
    if (index < 0 || index >= static_cast<int>(data.markers.size())) return;
    const std::size_t i = static_cast<std::size_t>(index);
    const std::int64_t gap = std::max<std::int64_t>(1, sampleRate_ / 100);
    const std::int64_t lo = (i > 0 ? data.markers[i - 1].dst : 0) + gap;
    const std::int64_t hi = (i + 1 < data.markers.size() ? data.markers[i + 1].dst : row->lengthFrames) - gap;
    if (hi < lo) return;
    const std::int64_t target = std::clamp<std::int64_t>(static_cast<std::int64_t>(std::llround(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(row->lengthFrames))), lo, hi);
    if (slide) {  // the marker moves along the sound; nothing is stretched
        const lpc::TimeMap map(data.sourceLength, row->lengthFrames, lpc::flexAnchors(data));
        std::int64_t s = static_cast<std::int64_t>(std::llround(map.toSource(static_cast<double>(target))));
        const std::int64_t sLo = (i > 0 ? data.markers[i - 1].src : 0) + 1, sHi = (i + 1 < data.markers.size() ? data.markers[i + 1].src : data.sourceLength) - 1;
        s = std::clamp(s, sLo, std::max(sLo, sHi));
        data.markers[i] = {s, target};
    } else {
        data.markers[i].dst = target;
    }
    flexApplyOne(regionId, data, "");
}

void ProjectController::flexDeleteMarker(const QString& regionId, int index) {
    lpc::FlexData data;
    if (!flexPrepare(regionId, data, false)) return;
    if (index < 0 || index >= static_cast<int>(data.markers.size())) return;
    data.markers.erase(data.markers.begin() + index);
    flexApplyOne(regionId, data, "");
}

void ProjectController::flexNeutralMarker(const QString& regionId, int index) {
    lpc::FlexData data;
    if (!flexPrepare(regionId, data, false)) return;
    const RegionRow* row = regions_.find(regionId);
    if (index < 0 || index >= static_cast<int>(data.markers.size())) return;
    const std::size_t i = static_cast<std::size_t>(index);
    const std::int64_t gap = std::max<std::int64_t>(1, sampleRate_ / 100);
    const std::int64_t lo = (i > 0 ? data.markers[i - 1].dst : 0) + gap;
    const std::int64_t hi = (i + 1 < data.markers.size() ? data.markers[i + 1].dst : row->lengthFrames) - gap;
    if (hi < lo) return;
    data.markers[i].dst = std::clamp(data.markers[i].src, lo, hi);
    flexApplyOne(regionId, data, "");
}

void ProjectController::flexResetEdits() {
    std::vector<FlexEdit> edits;
    for (const RegionRow* r : selectedRegionRows()) {
        if (!r->audio || !r->flexed) continue;
        lpc::FlexData data = flexDataOf(*r);
        data.markers.clear();
        data.notes.clear();
        edits.push_back(FlexEdit{r->id, std::move(data), false, {}, {}});
    }
    if (edits.empty()) {
        emit notice("Select a flexed region first");
        return;
    }
    flexApply(std::move(edits), nlohmann::json::array(), "Resetting the flex edits");
}

void ProjectController::flexSlice(bool atMarkers) {
    nlohmann::json commands = nlohmann::json::array();
    bool waiting = false;
    for (const RegionRow* r : selectedRegionRows()) {
        if (!r->audio || r->missing || r->lengthFrames <= 0) continue;
        const lpc::FlexData data = flexDataOf(*r);
        const lpc::TimeMap map(data.sourceLength, r->lengthFrames, lpc::flexAnchors(data));
        std::vector<std::int64_t> at;  // in frames from the start of the region as it plays
        if (atMarkers) {
            for (const lpc::FlexMarker& m : data.markers) at.push_back(m.dst);
        } else {
            const FlexState::Entry entry = flexState().entries.value(windowKey(data));
            if (!entry.transientsDone) {
                flexAnalyse(r->id);
                waiting = true;
                continue;
            }
            for (std::int64_t t : entry.transients) at.push_back(static_cast<std::int64_t>(std::llround(map.toDest(static_cast<double>(t)))));
        }
        std::sort(at.begin(), at.end());
        const std::int64_t gap = std::max<std::int64_t>(1, sampleRate_ / 50);
        QString current = r->id;
        std::int64_t last = 0;
        const double startSamples = tempoMap_.ticksToSamples(static_cast<std::int64_t>(std::llround(r->startBeats * lpc::kPPQ)), sampleRate_);
        for (std::int64_t frame : at) {
            if (frame < last + gap || frame > r->lengthFrames - gap) continue;
            last = frame;
            const double beats = static_cast<double>(tempoMap_.samplesToTicks(startSamples + static_cast<double>(frame), sampleRate_)) / lpc::kPPQ;
            const QString right = QUuid::createUuid().toString(QUuid::WithoutBraces);
            commands.push_back({{"type", "split_region"}, {"regionId", current.toStdString()}, {"at", regionPosition(*r, beats)}, {"newRegionId", right.toStdString()}});
            current = right;
        }
    }
    if (commands.empty()) {
        emit notice(waiting ? "Analysing the sound… try again in a moment" : (atMarkers ? "There are no flex markers to cut at" : "There are no transients to cut at"));
        return;
    }
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
}

void ProjectController::flexQuantize(double gridBeats, double strengthPercent) {
    if (!std::isfinite(gridBeats) || gridBeats <= 0 || !std::isfinite(strengthPercent)) return;
    const double strength = std::clamp(strengthPercent, 0.0, 100.0) / 100.0;
    std::vector<FlexEdit> edits;
    bool waiting = false;
    for (const RegionRow* r : selectedRegionRows()) {
        if (!r->audio || r->missing || r->lengthFrames <= 0) continue;
        lpc::FlexData data = flexDataOf(*r);
        const FlexState::Entry entry = flexState().entries.value(windowKey(data));
        if (!entry.transientsDone) {
            flexAnalyse(r->id);
            waiting = true;
            continue;
        }
        const lpc::TimeMap map(data.sourceLength, r->lengthFrames, lpc::flexAnchors(data));
        const double startSamples = tempoMap_.ticksToSamples(static_cast<std::int64_t>(std::llround(r->startBeats * lpc::kPPQ)), sampleRate_);
        const std::int64_t gap = std::max<std::int64_t>(1, sampleRate_ / 100);
        std::vector<lpc::FlexMarker> markers;
        std::int64_t lastSrc = 0, lastDst = 0;
        for (std::int64_t t : entry.transients) {
            if (t <= lastSrc || t >= data.sourceLength) continue;
            const double dst0 = map.toDest(static_cast<double>(t));
            const double posBeats = static_cast<double>(tempoMap_.samplesToTicks(startSamples + dst0, sampleRate_)) / lpc::kPPQ;
            const double gridded = std::round(posBeats / gridBeats) * gridBeats;
            const double gridFrames = tempoMap_.ticksToSamples(static_cast<std::int64_t>(std::llround(gridded * lpc::kPPQ)), sampleRate_) - startSamples;
            const std::int64_t target = static_cast<std::int64_t>(std::llround(dst0 + strength * (gridFrames - dst0)));
            if (target <= lastDst + gap || target >= r->lengthFrames - gap) continue;
            markers.push_back({t, target});
            lastSrc = t;
            lastDst = target;
        }
        data.markers = std::move(markers);
        edits.push_back(FlexEdit{r->id, std::move(data), false, {}, {}});
    }
    if (edits.empty()) {
        emit notice(waiting ? "Analysing the sound… try again in a moment" : "Select an audio region first");
        return;
    }
    flexApply(std::move(edits), nlohmann::json::array(), "Quantizing");
}

// ---------------------------------------------------------------- Flex Pitch: the notes

QVariantList ProjectController::flexPitchNotes(const QString& regionId) {
    const RegionRow* row = regions_.find(regionId);
    if (!row || !row->audio || row->missing || row->lengthFrames <= 0) return {};
    if (!row->flexNotes.isEmpty()) return row->flexNotes;
    const lpc::FlexData data = flexDataOf(*row);
    const FlexState::Entry entry = flexState().entries.value(windowKey(data));
    if (!entry.pitchDone || !entry.pitch) {
        flexAnalyse(regionId);
        return {};
    }
    const lpc::TimeMap map(data.sourceLength, row->lengthFrames, lpc::flexAnchors(data));
    const double total = static_cast<double>(row->lengthFrames);
    QVariantList out;
    const std::vector<lpc::FlexNoteData> notes = lpc::flexNoteData(entry.pitch->notes);
    for (std::size_t i = 0; i < notes.size(); ++i) out.append(noteMap(static_cast<int>(i), notes[i], map, total));
    return out;
}

void ProjectController::flexNoteEdit(const QString& regionId, int index, const QVariantMap& edit) {
    lpc::FlexData data;
    if (!flexPrepare(regionId, data, true)) return;
    if (index < 0 || index >= static_cast<int>(data.notes.size())) return;
    lpc::FlexNoteData& n = data.notes[static_cast<std::size_t>(index)];
    auto num = [&](const char* key, double& target, double lo, double hi) {
        if (!edit.contains(key)) return;
        const double v = edit.value(key).toDouble();
        if (std::isfinite(v)) target = std::clamp(v, lo, hi);
    };
    num("semitones", n.semitones, -48, 48);
    num("fineCents", n.fineCents, -100, 100);
    num("correct", n.correct, 0, 1);
    num("vibrato", n.vibrato, 0, 4);
    num("driftIn", n.driftIn, 0, 1);
    num("driftOut", n.driftOut, 0, 1);
    num("gainDb", n.gainDb, -24, 24);
    num("formantCents", n.formantCents, -1200, 1200);
    flexApplyOne(regionId, data, "");
}

void ProjectController::flexNoteTime(const QString& regionId, int index, double startFraction, double endFraction) {
    lpc::FlexData data;
    if (!std::isfinite(startFraction) || !std::isfinite(endFraction) || !flexPrepare(regionId, data, true)) return;
    const RegionRow* row = regions_.find(regionId);
    if (index < 0 || index >= static_cast<int>(data.notes.size())) return;
    const lpc::FlexNoteData n = data.notes[static_cast<std::size_t>(index)];
    const std::int64_t gap = std::max<std::int64_t>(1, sampleRate_ / 100);
    const std::int64_t srcA = n.start, srcB = n.start + n.length;
    // the neighbours outside the note limit how far its edges can go
    std::int64_t loDst = 0, hiDst = row->lengthFrames;
    std::vector<lpc::FlexMarker> kept;
    for (const lpc::FlexMarker& m : data.markers) {
        if (m.src < srcA - 1) { loDst = std::max(loDst, m.dst); kept.push_back(m); }
        else if (m.src > srcB + 1) { hiDst = std::min(hiDst, m.dst); kept.push_back(m); }
    }
    std::int64_t a = static_cast<std::int64_t>(std::llround(std::clamp(startFraction, 0.0, 1.0) * static_cast<double>(row->lengthFrames)));
    std::int64_t b = static_cast<std::int64_t>(std::llround(std::clamp(endFraction, 0.0, 1.0) * static_cast<double>(row->lengthFrames)));
    a = std::max(a, loDst + gap);
    b = std::min(b, hiDst - gap);
    if (b - a < gap) return;
    if (srcA > 0) kept.push_back({srcA, a});
    if (srcB < data.sourceLength) kept.push_back({srcB, b});
    std::sort(kept.begin(), kept.end(), [](const lpc::FlexMarker& x, const lpc::FlexMarker& y) { return x.src < y.src; });
    data.markers = std::move(kept);
    flexApplyOne(regionId, data, "");
}

void ProjectController::flexNoteSplit(const QString& regionId, int index, double fraction) {
    lpc::FlexData data;
    if (!std::isfinite(fraction) || !flexPrepare(regionId, data, true)) return;
    const RegionRow* row = regions_.find(regionId);
    if (index < 0 || index >= static_cast<int>(data.notes.size())) return;
    const lpc::TimeMap map(data.sourceLength, row->lengthFrames, lpc::flexAnchors(data));
    const lpc::FlexNoteData n = data.notes[static_cast<std::size_t>(index)];
    const std::int64_t at = static_cast<std::int64_t>(std::llround(map.toSource(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(row->lengthFrames))));
    const std::int64_t gap = std::max<std::int64_t>(1, sampleRate_ / 100);
    if (at <= n.start + gap || at >= n.start + n.length - gap) return;
    lpc::FlexNoteData first = n, second = n;
    first.length = at - n.start;
    second.start = at;
    second.length = n.start + n.length - at;
    data.notes[static_cast<std::size_t>(index)] = first;
    data.notes.insert(data.notes.begin() + index + 1, second);
    flexApplyOne(regionId, data, "");
}

void ProjectController::flexNoteJoin(const QString& regionId, int index) {
    lpc::FlexData data;
    if (!flexPrepare(regionId, data, true)) return;
    if (index < 0 || index + 1 >= static_cast<int>(data.notes.size())) return;
    lpc::FlexNoteData& a = data.notes[static_cast<std::size_t>(index)];
    const lpc::FlexNoteData b = data.notes[static_cast<std::size_t>(index) + 1];
    a.length = b.start + b.length - a.start;
    data.notes.erase(data.notes.begin() + index + 1);
    flexApplyOne(regionId, data, "");
}

// ---------------------------------------------------------------- hearing a note

void ProjectController::flexNotePreview(const QString& regionId, int index, const QVariantMap& edit) {
    FlexState& st = flexState();
    flexStopped_ = false;
    if (st.previewBusy) {  // the last request wins
        st.pending = FlexState::Pending{regionId, index, edit};
        return;
    }
    lpc::FlexData data;
    if (!flexPrepare(regionId, data, true)) return;
    if (index < 0 || index >= static_cast<int>(data.notes.size())) return;
    const lpc::FlexNoteData base = data.notes[static_cast<std::size_t>(index)];
    for (lpc::FlexNoteData& n : data.notes) {  // only this note is edited: the others sound as recorded
        n.semitones = n.fineCents = n.correct = n.driftIn = n.driftOut = n.gainDb = n.formantCents = 0.0;
        n.vibrato = 1.0;
    }
    lpc::FlexNoteData& note = data.notes[static_cast<std::size_t>(index)];
    note = base;
    auto num = [&](const char* key, double& target, double lo, double hi) {
        if (!edit.contains(key)) return;
        const double v = edit.value(key).toDouble();
        if (std::isfinite(v)) target = std::clamp(v, lo, hi);
    };
    num("semitones", note.semitones, -48, 48);
    num("fineCents", note.fineCents, -100, 100);
    num("correct", note.correct, 0, 1);
    num("vibrato", note.vibrato, 0, 4);
    num("driftIn", note.driftIn, 0, 1);
    num("driftOut", note.driftOut, 0, 1);
    num("gainDb", note.gainDb, -24, 24);
    num("formantCents", note.formantCents, -1200, 1200);
    const QString key = windowKey(data);
    const FlexState::Entry entry = st.entries.value(key);
    const std::shared_ptr<lpc::WavData> cached = st.audioKey == key ? st.audio : nullptr;
    const std::filesystem::path file = mediaPath(dir_, mediaPaths_.value(QString::fromStdString(data.sourceMediaId.toString())));
    const int engineRate = sampleRate_;
    st.previewBusy = true;
    QPointer<ProjectController> self(this);
    const std::uint64_t generation = generation_;
    (void)QtConcurrent::run([self, key, data, index, entry, cached, file, engineRate, generation] {
        std::shared_ptr<lpc::WavData> audio = cached;
        std::shared_ptr<lpc::PitchAnalysis> pitch = entry.pitch;
        lpc::audio::AuditionClip clip;
        try {
            if (!audio) audio = std::make_shared<lpc::WavData>(readWindow(file, data.sourceOffset, data.sourceLength));
            if (!pitch) pitch = std::make_shared<lpc::PitchAnalysis>(lpc::analysePitch(*audio));
            lpc::PitchAnalysis a = *pitch;
            a.notes = lpc::flexNotes(data);
            const std::vector<lpc::NoteEdit> edits = lpc::flexEdits(data);
            const lpc::FlexNote& n = a.notes[static_cast<std::size_t>(index)];
            const std::int64_t pad = static_cast<std::int64_t>(0.02 * audio->sampleRate);
            const lpc::WavData part = lpc::renderFlexPitch(*audio, a, edits, std::max<std::int64_t>(0, n.start - pad), std::min(audio->frames(), n.start + n.length + pad));
            clip = makeClip(part, engineRate);
        } catch (const std::exception&) {
            clip = {};
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, key, audio, pitch, clip = std::move(clip), generation]() mutable {
            if (!self) return;
            FlexState& st = self->flexState();
            st.previewBusy = false;
            if (generation == self->generation_ && self->host_) {
                st.audioKey = key;
                st.audio = audio;
                FlexState::Entry& e = st.entries[key];
                if (!e.pitchDone && pitch) { e.pitch = pitch; e.pitchDone = true; }
                if (!clip.left.empty() && !self->flexStopped_) {
                    const std::int64_t length = static_cast<std::int64_t>(clip.left.size());
                    std::int64_t from = self->host_->auditionPosition();
                    if (from < 0 || from >= length) from = 0;  // a clip that was not playing starts at its beginning
                    (void)self->host_->audition(std::move(clip), from, true);
                }
            }
            if (st.pending) {
                const FlexState::Pending p = *st.pending;
                st.pending.reset();
                self->flexNotePreview(p.regionId, p.index, p.edit);
            }
        }, Qt::QueuedConnection);
    });
}

void ProjectController::flexStopPreview() {
    flexStopped_ = true;
    FlexState& st = flexState();
    st.pending.reset();
    if (host_) (void)host_->stopAudition();
}

// ---------------------------------------------------------------- MIDI from the notes

void ProjectController::flexCreateMidiTrack(const QString& regionId) {
    lpc::FlexData data;
    if (!flexPrepare(regionId, data, true)) return;
    const RegionRow* row = regions_.find(regionId);
    const TrackRow* source = tracks_.find(row->trackId);
    if (data.notes.empty()) {
        emit notice("No notes were found in this region");
        return;
    }
    const lpc::TimeMap map(data.sourceLength, row->lengthFrames, lpc::flexAnchors(data));
    const lpc::Ticks startTick = static_cast<lpc::Ticks>(std::llround(row->startBeats * lpc::kPPQ));
    const double startSamples = tempoMap_.ticksToSamples(startTick, sampleRate_);
    auto tickAt = [&](double dstFrames) { return static_cast<std::int64_t>(tempoMap_.samplesToTicks(startSamples + dstFrames, sampleRate_)) - startTick; };
    nlohmann::json notes = nlohmann::json::array();
    for (const lpc::FlexNoteData& n : data.notes) {
        const std::int64_t a = tickAt(map.toDest(static_cast<double>(n.start))), b = tickAt(map.toDest(static_cast<double>(n.start + n.length)));
        const int pitch = static_cast<int>(std::lround(n.pitch + n.semitones + n.fineCents / 100.0));
        if (pitch < 0 || pitch > 127 || b <= a) continue;
        notes.push_back({{"start", std::max<std::int64_t>(0, a)}, {"length", std::max<std::int64_t>(1, b - a)}, {"note", pitch}, {"velocity", 100}});
    }
    if (notes.empty()) return;
    const std::string nullId = "00000000-0000-0000-0000-000000000000";
    const QString trackId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const std::string name = (source ? source->name : QString("Audio")).toStdString() + " MIDI";
    nlohmann::json track = {{"id", trackId.toStdString()},
                            {"kind", "instrument"},
                            {"name", name},
                            {"color", ""},
                            {"strip", {{"gainDb", 0}, {"pan", 0}, {"mute", false}, {"solo", false}, {"inserts", nlohmann::json::array()},
                                       {"sends", nlohmann::json::array()}, {"output", nullId}}},
                            {"regions", nlohmann::json::array()},
                            {"automation", nlohmann::json::array()},
                            {"instrument", {{"processorId", "builtin.sine"}, {"params", nlohmann::json::object()}, {"state", ""}}}};
    nlohmann::json region = {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()}, {"timeBase", "musical"}, {"start", startTick},
                             {"length", std::max<std::int64_t>(1, static_cast<std::int64_t>(std::llround(row->lengthBeats * lpc::kPPQ)))}, {"mediaId", nullId},
                             {"sourceOffsetFrames", 0}, {"gainDb", 0}, {"notes", notes}};
    nlohmann::json commands = nlohmann::json::array();
    commands.push_back({{"type", "add_track"}, {"index", -1}, {"track", track}});
    commands.push_back({{"type", "add_region"}, {"trackId", trackId.toStdString()}, {"index", -1}, {"region", region}});
    sendCommand({{"type", "transaction"}, {"commands", commands}});
    emit notice(QString("Created a MIDI track with %1 note%2").arg(notes.size()).arg(notes.size() == 1 ? "" : "s"));
}

}  // namespace jad
