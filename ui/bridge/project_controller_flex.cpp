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
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>

#include "bridge/project_controller.h"
#include "lpc/audio/audition.h"
#include "lpc/drum_replace.h"
#include "lpc/flex_model.h"
#include "lpc/flex_pitch.h"
#include "lpc/flex_time.h"
#include "lpc/model_json.h"
#include "lpc/project_host.h"
#include "lpc/session_player.h"
#include "lpc/processor_ids.h"
#include "lpc/tempo_analysis.h"
#include "lpc/wav.h"

namespace jad {

namespace {
// Flex Pitch listens with Logic's neural pitch tracker when the model is there (it falls back to YIN)
lpc::PitchOptions neuralPitch() {
    lpc::PitchOptions o;
    o.neural = true;
    return o;
}
}  // namespace

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


// A marker moved: only the stretch of sound between the markers around it changes. That part is rendered again and put into the sound that
// was rendered before (a few milliseconds of cross-fade at its two ends), so an edit costs the same whatever the length of the recording.
// False when this cannot be done (the caller renders everything); `unchanged` when the two sets of markers sound the same.
bool renderChanged(const std::filesystem::path& sourceFile, const lpc::FlexData& now, const lpc::FlexData& before, std::int64_t dstFrames,
                   const std::filesystem::path& previousFile, std::int64_t previousOffset, const std::vector<std::int64_t>& transients, bool haveTransients,
                   double sensitivity, const lpc::FlexTimeParams& params, lpc::WavData& out, bool& unchanged) {
    unchanged = false;
    const lpc::TimeMap oldMap(before.sourceLength, dstFrames, lpc::flexAnchors(before)), newMap(now.sourceLength, dstFrames, lpc::flexAnchors(now));
    std::vector<std::int64_t> cuts{0, dstFrames};
    for (const lpc::FlexAnchor& a : oldMap.points()) cuts.push_back(a.dst);
    for (const lpc::FlexAnchor& a : newMap.points()) cuts.push_back(a.dst);
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    std::int64_t dirtyLo = dstFrames, dirtyHi = 0;
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
        bool differs = false;
        for (double at : {0.0, 0.5, 1.0}) {
            const double x = static_cast<double>(cuts[i]) + at * static_cast<double>(cuts[i + 1] - cuts[i]);
            if (std::abs(oldMap.toSource(x) - newMap.toSource(x)) > 1.0) differs = true;
        }
        if (differs) {
            dirtyLo = std::min(dirtyLo, cuts[i]);
            dirtyHi = std::max(dirtyHi, cuts[i + 1]);
        }
    }
    if (dirtyHi <= dirtyLo) {
        unchanged = true;
        return true;
    }
    lpc::WavFile previousWav(previousFile);
    const int rate = previousWav.sampleRate(), channels = previousWav.channels();
    const std::int64_t pad = static_cast<std::int64_t>(0.3 * rate), seam = static_cast<std::int64_t>(0.015 * rate);
    const std::int64_t windowLo = std::max<std::int64_t>(0, dirtyLo - pad), windowHi = std::min(dstFrames, dirtyHi + pad);
    // the sound as it was, and the source of the window as it is mapped now
    lpc::WavData result = readWindow(previousFile, previousOffset, dstFrames);
    if (result.channels != channels || result.sampleRate != rate) return false;
    result.samples.resize(static_cast<std::size_t>(dstFrames) * static_cast<std::size_t>(channels), 0.0f);
    const std::int64_t srcLo = std::clamp<std::int64_t>(static_cast<std::int64_t>(std::floor(newMap.toSource(static_cast<double>(windowLo)))), 0, now.sourceLength - 1);
    const std::int64_t srcHi = std::clamp<std::int64_t>(static_cast<std::int64_t>(std::ceil(newMap.toSource(static_cast<double>(windowHi)))), srcLo + 1, now.sourceLength);
    const lpc::WavData sub = readWindow(sourceFile, now.sourceOffset + srcLo, srcHi - srcLo);
    if (sub.channels != channels || sub.sampleRate != rate || sub.frames() <= 0) return false;
    std::vector<lpc::FlexAnchor> anchors;
    for (const lpc::FlexMarker& m : now.markers)
        if (m.src > srcLo && m.src < srcHi && m.dst > windowLo && m.dst < windowHi) anchors.push_back({m.src - srcLo, m.dst - windowLo});
    std::vector<std::int64_t> hits;
    if (haveTransients) {
        for (std::int64_t t : transients)
            if (t > srcLo && t < srcHi) hits.push_back(t - srcLo);
    } else {
        hits = lpc::findTransients(sub, sensitivity);
    }
    const lpc::WavData fresh = lpc::renderFlexTime(sub, anchors, hits, params, windowHi - windowLo);
    if (fresh.frames() != windowHi - windowLo) return false;
    for (std::int64_t i = dirtyLo; i < dirtyHi; ++i) {
        const double w = std::min({1.0, static_cast<double>(i - dirtyLo + 1) / static_cast<double>(seam), static_cast<double>(dirtyHi - i) / static_cast<double>(seam)});
        for (int c = 0; c < channels; ++c) {
            float& dst = result.samples[static_cast<std::size_t>(i * channels + c)];
            const float add = fresh.samples[static_cast<std::size_t>((i - windowLo) * channels + c)];
            dst = static_cast<float>(dst * (1.0 - w) + add * w);
        }
    }
    out = std::move(result);
    return true;
}

}  // namespace

// ---------------------------------------------------------------- state

struct ProjectController::FlexState {
    struct Entry {
        bool transientsDone = false, pitchDone = false, running = false;
        double sensitivity = -1.0;
        std::vector<std::int64_t> transients;
        std::shared_ptr<lpc::PitchAnalysis> pitch;
        std::shared_ptr<lpc::TempoAnalysis> tempo;
        bool tempoDone = false, tempoRunning = false;
    };
    QHash<QString, int> wanted;     // by region: what to do when its tempo is known (1 follow, 2 apply the tempo to the project)
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
            if (doPitch) pitch = std::make_shared<lpc::PitchAnalysis>(lpc::analysePitch(wav, neuralPitch()));
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
        std::int64_t expectFrames = 0;  // the length the region has now (it must still have it when the rendering is done)
        bool bake = false;
        std::vector<std::int64_t> transients;
        bool haveTransients = false;
        std::shared_ptr<lpc::PitchAnalysis> pitch;
        double sensitivity = 0.5;
        int token = 0;
        bool incremental = false;  // only the stretch between the markers that moved is rendered again, into the sound rendered before
        lpc::FlexData before;
        std::filesystem::path previousFile;
        std::int64_t previousOffset = 0;
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
        job.expectFrames = row->lengthFrames;
        job.dstFrames = e.lengthFrames > 0 ? e.lengthFrames : row->lengthFrames;
        job.bake = needsBake(job.data, job.dstFrames);
        {
            // the region already plays a rendering made from this same sound with these notes: a change of markers alone can be patched into it
            bool noPitchEdit = true;
            for (const lpc::FlexNoteData& n : job.data.notes) noPitchEdit = noPitchEdit && neutral(n);
            lpc::FlexData before = flexDataOf(*row);  // a region that was never flexed plays its own source: the same as a rendering without markers
            for (const lpc::FlexNoteData& n : before.notes) noPitchEdit = noPitchEdit && neutral(n);
            if (job.bake && !e.overrideTrack && job.dstFrames == job.expectFrames && noPitchEdit && before.sourceMediaId == job.data.sourceMediaId && before.sourceOffset == job.data.sourceOffset &&
                before.sourceLength == job.data.sourceLength) {
                job.incremental = true;
                job.before = std::move(before);
                job.previousFile = mediaPath(dir_, mediaPaths_.value(row->mediaId));
                job.previousOffset = row->sourceOffsetFrames;
            }
        }
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
            bool keep = false;  // the region keeps the sound it plays: only its flex data changes
        };
        std::vector<Result> results;
        for (Job& job : jobs) {
            Result r;
            r.job = std::move(job);
            Job& j = r.job;
            bool patched = false;
            if (j.bake && j.incremental) {
                try {
                    lpc::WavData out;
                    bool unchanged = false;
                    if (renderChanged(j.source, j.data, j.before, j.dstFrames, j.previousFile, j.previousOffset, j.transients, j.haveTransients, j.sensitivity, timeParams(j.mode, j.params), out, unchanged)) {
                        patched = true;
                        if (unchanged) {
                            r.keep = true;
                        } else {
                            r.frames = out.frames();
                            r.channels = out.channels;
                            r.sampleRate = out.sampleRate;
                            const std::filesystem::path audioDir = projectDir / "audio";
                            std::filesystem::create_directories(audioDir);
                            std::u16string stem = j.source.stem().u16string();
                            if (const std::size_t at = stem.find(u" (flex"); at != std::u16string::npos) stem.erase(at);
                            for (int n = 1;; ++n) {
                                r.target = audioDir / (stem + u" (flex" + (n > 1 ? u" " + QString::number(n).toStdU16String() : std::u16string()) + u").wav");
                                if (!std::filesystem::exists(r.target)) break;
                            }
                            lpc::writeWav(r.target, out.sampleRate > 0 ? out.sampleRate : rate, out.channels, out.samples, lpc::WavFormat::Pcm24);
                            r.hash = hashOf(r.target);
                        }
                    }
                } catch (const std::exception&) {
                    patched = false;
                    r.target.clear();
                }
            }
            if (j.bake && !patched) {
                try {
                    const lpc::WavData src = readWindow(j.source, j.data.sourceOffset, j.data.sourceLength);
                    lpc::WavData work = src;
                    bool pitchEdited = false;
                    for (const lpc::FlexNoteData& n : j.data.notes) pitchEdited = pitchEdited || !neutral(n);
                    if (pitchEdited) {
                        lpc::PitchAnalysis a = j.pitch ? *j.pitch : lpc::analysePitch(src, neuralPitch());
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
                if (!row || row->lengthFrames != j.expectFrames || self->flexState().tokens.value(j.regionId) != j.token) {
                    if (!r.target.empty()) std::filesystem::remove(r.target, ignore);  // the region went away, changed, or a newer edit is on its way
                    continue;
                }
                nlohmann::json region = nlohmann::json::parse(row->json, nullptr, false);
                if (region.is_discarded()) continue;
                if (r.keep) {
                    // the sound is the one the region plays already
                } else if (j.bake) {
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
                if (!j.data.markers.empty() || !j.data.notes.empty() || j.data.beats) region["flex"] = j.data;
                else region.erase("flex");
                if (j.dstFrames != j.expectFrames) {  // the region becomes as long as the sound it plays now
                    const std::int64_t startUnit = region["start"].get<std::int64_t>();
                    const std::int64_t newLength = row->absolute
                        ? static_cast<std::int64_t>(std::llround(static_cast<double>(j.dstFrames) * 1e6 / rate))
                        : static_cast<std::int64_t>(self->tempoMap_.samplesToTicks(self->tempoMap_.ticksToSamples(startUnit, rate) + static_cast<double>(j.dstFrames), rate)) - startUnit;
                    region["length"] = std::max<std::int64_t>(1, newLength);
                    const std::int64_t limit = region["length"].get<std::int64_t>();
                    if (region.value("fadeIn", std::int64_t{0}) > limit) region["fadeIn"] = 0;
                    if (region.value("fadeOut", std::int64_t{0}) > limit) region["fadeOut"] = 0;
                }
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
    if (!std::isfinite(fraction) || !flexPrepare(regionId, data, false) || !moveMarkerData(regionId, data, index, fraction, slide)) return;
    flexApplyOne(regionId, data, "");
}

// The markers of `data` with marker `index` moved to `fraction` of the region (kept between its neighbours).
bool ProjectController::moveMarkerData(const QString& regionId, lpc::FlexData& data, int index, double fraction, bool slide) const {
    const RegionRow* row = regions_.find(regionId);
    if (!row || index < 0 || index >= static_cast<int>(data.markers.size())) return false;
    const std::size_t i = static_cast<std::size_t>(index);
    const std::int64_t gap = std::max<std::int64_t>(1, sampleRate_ / 100);
    const std::int64_t lo = (i > 0 ? data.markers[i - 1].dst : 0) + gap;
    const std::int64_t hi = (i + 1 < data.markers.size() ? data.markers[i + 1].dst : row->lengthFrames) - gap;
    if (hi < lo) return false;
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
    return true;
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

// Q-Reference: the first selected region is the reference. Its transients go to the grid; a transient of another selected region that falls
// together with one of the reference moves to the same place, so the group stays tight with it (a kick and a bass that played together).
void ProjectController::flexQuantizeGroup(double gridBeats, double strengthPercent) {
    if (!std::isfinite(gridBeats) || gridBeats <= 0 || !std::isfinite(strengthPercent)) return;
    const double strength = std::clamp(strengthPercent, 0.0, 100.0) / 100.0;
    std::vector<const RegionRow*> rows;
    for (const RegionRow* r : selectedRegionRows())
        if (r->audio && !r->missing && r->lengthFrames > 0) rows.push_back(r);
    if (rows.size() < 2) {
        emit notice("Select the reference region and at least one more audio region");
        return;
    }
    struct Hit { double from; double to; };  // timeline samples
    auto startOf = [&](const RegionRow* r) { return tempoMap_.ticksToSamples(static_cast<std::int64_t>(std::llround(r->startBeats * lpc::kPPQ)), sampleRate_); };
    std::vector<Hit> reference;
    for (const RegionRow* r : rows) {
        lpc::FlexData data = flexDataOf(*r);
        if (!flexState().entries.value(windowKey(data)).transientsDone) {
            for (const RegionRow* q : rows) flexAnalyse(q->id);
            emit notice("Analysing the sound… try again in a moment");
            return;
        }
    }
    {
        const RegionRow* r = rows.front();
        const lpc::FlexData data = flexDataOf(*r);
        const lpc::TimeMap map(data.sourceLength, r->lengthFrames, lpc::flexAnchors(data));
        const double start = startOf(r);
        for (std::int64_t t : flexState().entries.value(windowKey(data)).transients) {
            const double at = start + map.toDest(static_cast<double>(t));
            const double posBeats = static_cast<double>(tempoMap_.samplesToTicks(at, sampleRate_)) / lpc::kPPQ;
            const double gridded = std::round(posBeats / gridBeats) * gridBeats;
            const double gridSamples = tempoMap_.ticksToSamples(static_cast<std::int64_t>(std::llround(gridded * lpc::kPPQ)), sampleRate_);
            reference.push_back({at, at + strength * (gridSamples - at)});
        }
    }
    const double tolerance = 0.03 * sampleRate_;
    const std::int64_t gap = std::max<std::int64_t>(1, sampleRate_ / 100);
    std::vector<FlexEdit> edits;
    for (const RegionRow* r : rows) {
        lpc::FlexData data = flexDataOf(*r);
        const lpc::TimeMap map(data.sourceLength, r->lengthFrames, lpc::flexAnchors(data));
        const double start = startOf(r);
        std::vector<lpc::FlexMarker> markers;
        std::int64_t lastSrc = 0, lastDst = 0;
        for (std::int64_t t : flexState().entries.value(windowKey(data)).transients) {
            if (t <= lastSrc || t >= data.sourceLength) continue;
            const double at = start + map.toDest(static_cast<double>(t));
            const Hit* best = nullptr;
            for (const Hit& h : reference)
                if (std::abs(h.from - at) <= tolerance && (!best || std::abs(h.from - at) < std::abs(best->from - at))) best = &h;
            if (!best) continue;
            const std::int64_t target = static_cast<std::int64_t>(std::llround(best->to - start));
            if (target <= lastDst + gap || target >= r->lengthFrames - gap) continue;
            markers.push_back({t, target});
            lastSrc = t;
            lastDst = target;
        }
        data.markers = std::move(markers);
        edits.push_back(FlexEdit{r->id, std::move(data), false, {}, {}});
    }
    flexApply(std::move(edits), nlohmann::json::array(), "Quantizing to the reference");
}

// ---------------------------------------------------------------- Smart Tempo and Flex & Follow
//
// The beats of a recording are found once (lpc::analyseTempo) and kept in the region's flex data. A region that follows the project tempo
// gets one flex marker on every beat, each at the place where that beat of the project falls: the sound is stretched to the grid, and
// again whenever the tempo of the project (or the place of the region) changes.

void ProjectController::smartAnalyse(const QString& regionId) {
    const RegionRow* row = regions_.find(regionId);
    if (!host_ || !row || !row->audio || row->missing || row->lengthFrames <= 0) return;
    const lpc::FlexData data = flexDataOf(*row);
    const QString key = windowKey(data);
    FlexState& st = flexState();
    FlexState::Entry& entry = st.entries[key];
    if (entry.tempoDone) {
        emit flexAnalysisReady(regionId);
        return;
    }
    if (entry.tempoRunning) return;
    entry.tempoRunning = true;
    emit notice("Analysing the tempo…");
    const std::filesystem::path file = mediaPath(dir_, mediaPaths_.value(QString::fromStdString(data.sourceMediaId.toString())));
    QPointer<ProjectController> self(this);
    const std::uint64_t generation = generation_;
    (void)QtConcurrent::run([self, key, regionId, file, data, generation] {
        std::shared_ptr<lpc::TempoAnalysis> result;
        try {
            result = std::make_shared<lpc::TempoAnalysis>(lpc::analyseTempo(readWindow(file, data.sourceOffset, data.sourceLength), 60.0, 200.0, 0, /*neural=*/true));
        } catch (const std::exception&) {
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, key, regionId, result, generation]() mutable {
            if (!self || generation != self->generation_) return;
            FlexState& st = self->flexState();
            FlexState::Entry& e = st.entries[key];
            e.tempoRunning = false;
            if (!result) {
                st.wanted.remove(regionId);
                return;
            }
            e.tempo = result;
            e.tempoDone = true;
            const int what = st.wanted.take(regionId);
            if (result->bpm <= 0.0 || result->beats.size() < 3) {
                if (what) emit self->notice("No steady tempo was found in this region");
                return;
            }
            emit self->flexAnalysisReady(regionId);
            if (what == 1) self->smartFollowRegion(regionId, true);
            else if (what == 2) self->smartApplyRegionTempo(regionId);
        }, Qt::QueuedConnection);
    });
}

// The markers that put every beat of the sound on a beat of the project (the bars on the project's bars). The anchor is kept when the beats
// still lie on the grid; otherwise (the first time, or the region was moved or cut) it is found again from where the sound plays now.
bool ProjectController::smartMarkers(const RegionRow& row, lpc::FlexData& data) const {
    if (!data.beats || data.beats->beats.size() < 2 || row.lengthFrames <= 0) return false;
    lpc::FlexBeats& b = *data.beats;
    const double bar = std::max(1, beatsPerBar_) * 4.0 / std::max(1, beatUnit_);  // in quarter-note beats
    const lpc::Ticks startTick = static_cast<lpc::Ticks>(std::llround(row.startBeats * lpc::kPPQ));
    const double startSamples = tempoMap_.ticksToSamples(startTick, sampleRate_);
    const int d = std::clamp(b.firstDownbeat, 0, static_cast<int>(b.beats.size()) - 1);
    // the bar line the first downbeat falls on
    auto onGrid = [&](double anchor) {
        const double at = row.startBeats + anchor + d;
        return std::abs(at / bar - std::round(at / bar)) < 0.001;
    };
    if (!onGrid(b.anchor)) {
        const double natural = static_cast<double>(tempoMap_.samplesToTicks(startSamples + static_cast<double>(b.beats[static_cast<std::size_t>(d)]), sampleRate_)) / lpc::kPPQ;
        b.anchor = std::round(natural / bar) * bar - d - row.startBeats;
    }
    data.markers.clear();
    std::int64_t lastSrc = 0, lastDst = 0;
    for (std::size_t i = 0; i < b.beats.size(); ++i) {
        const std::int64_t src = b.beats[i];
        const double beat = row.startBeats + b.anchor + static_cast<double>(i);
        const double frames = tempoMap_.ticksToSamples(static_cast<lpc::Ticks>(std::llround(beat * lpc::kPPQ)), sampleRate_) - startSamples;
        const std::int64_t dst = static_cast<std::int64_t>(std::llround(frames));
        if (src <= lastSrc || src >= data.sourceLength || dst <= lastDst || dst >= row.lengthFrames) continue;
        data.markers.push_back({src, dst});
        lastSrc = src;
        lastDst = dst;
    }
    return true;
}

void ProjectController::smartFollow(bool on) {
    bool any = false;
    for (const RegionRow* r : selectedRegionRows()) {
        if (!r->audio || r->missing) continue;
        any = true;
        smartFollowRegion(r->id, on);
    }
    if (!any) emit notice("Select an audio region first");
}

void ProjectController::smartFollowRegion(const QString& regionId, bool on) {
    const RegionRow* row = regions_.find(regionId);
    if (!host_ || !row || !row->audio || row->missing || row->lengthFrames <= 0) return;
    const TrackRow* track = tracks_.find(row->trackId);
    if (!track) return;
    lpc::FlexData data = flexDataOf(*row);
    FlexState& st = flexState();
    if (!on) {
        if (!data.beats || !data.beats->follow) return;
        data.beats->follow = false;
        data.markers.clear();
        flexApplyOne(regionId, data, "Leaving the project tempo");
        return;
    }
    if (!data.beats || data.beats->beats.empty()) {
        const FlexState::Entry entry = st.entries.value(windowKey(data));
        if (!entry.tempoDone || !entry.tempo) {
            st.wanted[regionId] = 1;
            smartAnalyse(regionId);
            return;
        }
        if (entry.tempo->bpm <= 0.0 || entry.tempo->beats.size() < 3) {
            emit notice("No steady tempo was found in this region");
            return;
        }
        lpc::FlexBeats b;
        b.bpm = entry.tempo->bpm;
        b.beatsPerBar = entry.tempo->beatsPerBar;
        b.firstDownbeat = entry.tempo->firstDownbeat;
        b.beats = entry.tempo->beats;
        data.beats = std::move(b);
    }
    const bool wasFollowing = data.beats->follow;
    data.beats->follow = true;
    if (!smartMarkers(*row, data)) return;
    // the first time, the region becomes as long as the sound is once it is on the grid (the last beat, then the rest of the recording as it was)
    std::int64_t newLength = 0;
    if (!wasFollowing && !data.markers.empty()) {
        newLength = data.markers.back().dst + (data.sourceLength - data.markers.back().src);
        if (newLength < 1 || newLength == row->lengthFrames) newLength = 0;
    }
    if (track->flexMode.isEmpty()) {  // Flex & Follow needs Flex on the track: it gets the automatic algorithm
        nlohmann::json prefix = nlohmann::json::array();
        prefix.push_back({{"type", "set_track_props"}, {"trackId", row->trackId.toStdString()}, {"flexMode", "automatic"}});
        FlexEdit edit{regionId, std::move(data), true, "automatic", paramsOf(*track)};
        edit.lengthFrames = newLength;
        flexApply({std::move(edit)}, prefix, "Conforming to the project tempo");
        return;
    }
    FlexEdit edit{regionId, std::move(data), false, {}, {}};
    edit.lengthFrames = newLength;
    flexApply({std::move(edit)}, nlohmann::json::array(), "Conforming to the project tempo");
}

void ProjectController::smartMoveBeat(const QString& regionId, int index, double fraction) {
    const RegionRow* row = regions_.find(regionId);
    if (!host_ || !row || !row->audio || !std::isfinite(fraction)) return;
    lpc::FlexData data = flexDataOf(*row);
    if (!data.beats || index < 0 || index >= static_cast<int>(data.beats->beats.size())) return;
    const lpc::TimeMap map(data.sourceLength, row->lengthFrames, lpc::flexAnchors(data));
    std::int64_t src = static_cast<std::int64_t>(std::llround(map.toSource(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(row->lengthFrames))));
    std::vector<std::int64_t>& beats = data.beats->beats;
    const std::size_t i = static_cast<std::size_t>(index);
    const std::int64_t lo = i > 0 ? beats[i - 1] + 1 : 0, hi = (i + 1 < beats.size() ? beats[i + 1] : data.sourceLength) - 1;
    if (hi < lo) return;
    src = std::clamp(src, lo, hi);
    if (src == beats[i]) return;
    beats[i] = src;
    if (data.beats->follow && !smartMarkers(*row, data)) return;
    flexApplyOne(regionId, data, data.beats->follow ? "Moving the beat" : "");
}

// Taps (D and T while the sound plays) are collected and applied together once they stop, as one edit.
void ProjectController::smartHint(const QString& regionId, double fraction, bool downbeat) {
    if (!host_ || !std::isfinite(fraction)) return;
    hints_[regionId].append({fraction, downbeat});
    const int serial = ++hintSerial_;
    QPointer<ProjectController> self(this);
    QTimer::singleShot(450, this, [self, serial] {
        if (!self || self->hintSerial_ != serial) return;
        const auto all = self->hints_;
        self->hints_.clear();
        for (auto it = all.begin(); it != all.end(); ++it) self->smartApplyHints(it.key(), it.value());
    });
}

void ProjectController::smartApplyHints(const QString& regionId, const QList<QPair<double, bool>>& hints) {
    const RegionRow* row = regions_.find(regionId);
    if (!host_ || !row || !row->audio) return;
    lpc::FlexData data = flexDataOf(*row);
    if (!data.beats || data.beats->beats.size() < 2) {
        emit notice("Follow Tempo first, so the beats are found");
        return;
    }
    const lpc::TimeMap map(data.sourceLength, row->lengthFrames, lpc::flexAnchors(data));
    std::vector<std::int64_t>& beats = data.beats->beats;
    const std::int64_t period = std::max<std::int64_t>(1, (beats.back() - beats.front()) / static_cast<std::int64_t>(beats.size() - 1));
    const int bpb = std::max(1, data.beats->beatsPerBar);
    bool changed = false;
    for (const auto& [fraction, downbeat] : hints) {
        const std::int64_t src = static_cast<std::int64_t>(std::llround(map.toSource(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(row->lengthFrames))));
        std::size_t nearest = 0;
        for (std::size_t i = 0; i < beats.size(); ++i)
            if (std::llabs(beats[i] - src) < std::llabs(beats[nearest] - src)) nearest = i;
        if (std::llabs(beats[nearest] - src) > period * 3 / 10) {  // no beat there: a beat hint adds one
            if (downbeat) continue;
            const std::size_t at = static_cast<std::size_t>(std::lower_bound(beats.begin(), beats.end(), src) - beats.begin());
            beats.insert(beats.begin() + static_cast<std::ptrdiff_t>(at), src);
            if (static_cast<int>(at) <= data.beats->firstDownbeat) data.beats->firstDownbeat = (data.beats->firstDownbeat + 1) % bpb;
            nearest = at;
            changed = true;
        }
        if (downbeat) {
            data.beats->firstDownbeat = static_cast<int>(nearest % static_cast<std::size_t>(bpb));
            changed = true;
        }
    }
    if (!changed) return;
    data.beats->anchor = 0.0;
    if (data.beats->follow && !smartMarkers(*row, data)) return;
    flexApplyOne(regionId, data, data.beats->follow ? "Applying the hints" : "");
}

void ProjectController::smartDeleteBeat(const QString& regionId, int index) {
    const RegionRow* row = regions_.find(regionId);
    if (!host_ || !row || !row->audio) return;
    lpc::FlexData data = flexDataOf(*row);
    if (!data.beats || index < 0 || index >= static_cast<int>(data.beats->beats.size()) || data.beats->beats.size() <= 3) return;
    data.beats->beats.erase(data.beats->beats.begin() + index);
    const int bpb = std::max(1, data.beats->beatsPerBar);
    if (index < data.beats->firstDownbeat) data.beats->firstDownbeat = ((data.beats->firstDownbeat - 1) % bpb + bpb) % bpb;
    data.beats->anchor = 0.0;
    if (data.beats->follow && !smartMarkers(*row, data)) return;
    flexApplyOne(regionId, data, data.beats->follow ? "Deleting the beat" : "");
}

void ProjectController::smartShiftDownbeat(const QString& regionId, int beats) {
    const RegionRow* row = regions_.find(regionId);
    if (!host_ || !row || !row->audio) return;
    lpc::FlexData data = flexDataOf(*row);
    if (!data.beats || data.beats->beats.empty()) return;
    const int bpb = std::max(1, data.beats->beatsPerBar);
    data.beats->firstDownbeat = ((data.beats->firstDownbeat + beats) % bpb + bpb) % bpb;
    data.beats->anchor = 0.0;  // found again, for the new downbeat
    if (data.beats->follow && !smartMarkers(*row, data)) return;
    flexApplyOne(regionId, data, data.beats->follow ? "Moving the downbeat" : "");
}

// Apply Region Tempo to Project Tempo: the project plays at the tempo found in the sound, and the sound is left as recorded.
void ProjectController::smartApplyRegionTempo(const QString& regionId) {
    const RegionRow* row = regions_.find(regionId);
    if (!host_ || !row || !row->audio || row->missing) return;
    const lpc::FlexData data = flexDataOf(*row);
    FlexState& st = flexState();
    const FlexState::Entry entry = st.entries.value(windowKey(data));
    double bpm = data.beats ? data.beats->bpm : 0.0;
    if (bpm <= 0.0) {
        if (!entry.tempoDone || !entry.tempo) {
            st.wanted[regionId] = 2;
            smartAnalyse(regionId);
            return;
        }
        bpm = entry.tempo->bpm;
    }
    if (bpm <= 0.0) {
        emit notice("No steady tempo was found in this region");
        return;
    }
    // the beats: from the flex data, or from the analysis that was just made
    std::vector<std::int64_t> beats = data.beats ? data.beats->beats : std::vector<std::int64_t>();
    if (beats.empty() && entry.tempo) beats = entry.tempo->beats;
    // the tempo between every two beats, smoothed (the median of three), to see whether the recording keeps its tempo
    std::vector<double> perBeat;
    for (std::size_t i = 0; i + 1 < beats.size(); ++i) perBeat.push_back(60.0 * sampleRate_ / static_cast<double>(std::max<std::int64_t>(1, beats[i + 1] - beats[i])));
    std::vector<double> smooth = perBeat;
    for (std::size_t i = 1; i + 1 < perBeat.size(); ++i) {
        std::array<double, 3> w{perBeat[i - 1], perBeat[i], perBeat[i + 1]};
        std::sort(w.begin(), w.end());
        smooth[i] = w[1];
    }
    double lo = 1e9, hi = 0.0;
    for (double v : smooth) { lo = std::min(lo, v); hi = std::max(hi, v); }
    if (smooth.size() < 8 || (hi - lo) / std::max(1.0, bpm) < 0.04) {  // a steady recording: one tempo
        sendCommand({{"type", "set_tempo"}, {"tick", 0}, {"bpm", std::clamp(std::round(bpm * 100.0) / 100.0, 20.0, 999.0)}});
        return;
    }
    // a recording that speeds up or slows down: tempo events along its beats, put where each beat falls on the project's beats
    lpc::FlexData probe = data;
    if (!probe.beats) {
        lpc::FlexBeats b;
        b.bpm = bpm;
        b.beatsPerBar = entry.tempo ? entry.tempo->beatsPerBar : 4;
        b.firstDownbeat = entry.tempo ? entry.tempo->firstDownbeat : 0;
        b.beats = beats;
        probe.beats = b;
    }
    if (!smartMarkers(*row, probe)) return;  // finds where the first beat lands (the anchor)
    const double anchor = probe.beats->anchor;
    nlohmann::json commands = nlohmann::json::array();
    double written = -1.0;
    lpc::Ticks lastTick = 0;
    for (std::size_t i = 0; i < smooth.size(); ++i) {
        const lpc::Ticks tick = static_cast<lpc::Ticks>(std::llround((row->startBeats + anchor + static_cast<double>(i)) * lpc::kPPQ));
        if (tick < 0) continue;
        if (written < 0.0 || std::abs(smooth[i] - written) > 0.5) {
            const double v = std::clamp(std::round(smooth[i] * 100.0) / 100.0, 20.0, 999.0);
            commands.push_back({{"type", "set_tempo"}, {"tick", tick}, {"bpm", v}});
            written = v;
        }
        lastTick = tick;
    }
    // after the recording the tempo it had before comes back
    const double before = tempoMap_.bpmAt(lastTick + lpc::kPPQ);
    if (std::abs(before - written) > 0.01) commands.push_back({{"type", "set_tempo"}, {"tick", lastTick + lpc::kPPQ}, {"bpm", before}});
    if (commands.empty()) return;
    sendCommand(commands.size() == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
    emit notice(QString("The tempo follows the recording (%1 to %2 bpm)").arg(lo, 0, 'f', 1).arg(hi, 0, 'f', 1));
}

void ProjectController::smartRefollow() {
    if (!host_) return;
    if (flexBusy_ > 0) {
        if (refollowQueued_) return;
        refollowQueued_ = true;
        QTimer::singleShot(400, this, [this] {
            refollowQueued_ = false;
            smartRefollow();
        });
        return;
    }
    std::vector<FlexEdit> edits;
    for (const RegionRow& r : regionRows_) {
        if (!r.followTempo || !r.audio || r.missing) continue;
        lpc::FlexData data = flexDataOf(r);
        const lpc::FlexData before = data;
        if (!smartMarkers(r, data)) continue;
        bool same = before.markers.size() == data.markers.size();
        for (std::size_t i = 0; same && i < data.markers.size(); ++i)
            same = before.markers[i].src == data.markers[i].src && std::llabs(before.markers[i].dst - data.markers[i].dst) <= 1;
        if (same) continue;
        edits.push_back(FlexEdit{r.id, std::move(data), false, {}, {}});
    }
    if (!edits.empty()) flexApply(std::move(edits), nlohmann::json::array(), QString());
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

QVariantList ProjectController::flexPitchCurves(const QString& regionId) {
    const RegionRow* row = regions_.find(regionId);
    if (!row || !row->audio || row->missing || row->lengthFrames <= 0) return {};
    lpc::FlexData data = flexDataOf(*row);
    const FlexState::Entry entry = flexState().entries.value(windowKey(data));
    if (!entry.pitchDone || !entry.pitch) return {};
    if (data.notes.empty()) data.notes = lpc::flexNoteData(entry.pitch->notes);
    lpc::PitchAnalysis a = *entry.pitch;
    a.notes = lpc::flexNotes(data);
    const std::vector<lpc::NoteEdit> edits = lpc::flexEdits(data);
    const lpc::TimeMap map(data.sourceLength, row->lengthFrames, lpc::flexAnchors(data));
    const double total = static_cast<double>(row->lengthFrames);
    QVariantList out;
    for (std::size_t i = 0; i < a.notes.size(); ++i) {
        const std::vector<float> curve = lpc::flexNoteCurve(a, a.notes[i], i < edits.size() ? edits[i] : lpc::NoteEdit{});
        QVariantList points;
        for (std::size_t k = 0; k < curve.size(); ++k) {
            points.append(map.toDest(static_cast<double>(a.notes[i].start) + static_cast<double>(k) * a.hop) / total);
            points.append(static_cast<double>(curve[k]));
        }
        out.append(QVariantMap{{"index", static_cast<int>(i)}, {"points", points}});
    }
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
    if (edit.contains("_marker")) {  // a marker being dragged: the stretch between its neighbours is rendered with it where it is now
        flexMarkerPreviewRun(regionId, index, edit.value("_marker").toDouble(), edit.value("_slide").toBool());
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

void ProjectController::flexMarkerPreview(const QString& regionId, int index, double fraction, bool slide) {
    flexNotePreview(regionId, index, {{"_marker", fraction}, {"_slide", slide}});
}

void ProjectController::flexMarkerPreviewRun(const QString& regionId, int index, double fraction, bool slide) {
    FlexState& st = flexState();
    lpc::FlexData data;
    if (!std::isfinite(fraction) || !flexPrepare(regionId, data, false) || !moveMarkerData(regionId, data, index, fraction, slide)) return;
    const RegionRow* row = regions_.find(regionId);
    const std::size_t i = static_cast<std::size_t>(index);
    const std::int64_t dstFrames = row->lengthFrames;
    const std::int64_t from = i > 0 ? data.markers[i - 1].dst : 0;
    const std::int64_t to = i + 1 < data.markers.size() ? data.markers[i + 1].dst : dstFrames;
    const QString key = windowKey(data);
    const FlexState::Entry entry = st.entries.value(key);
    const TrackRow* track = tracks_.find(row->trackId);
    const std::string mode = track ? track->flexMode.toStdString() : std::string();
    const std::map<std::string, double> params = track ? paramsOf(*track) : std::map<std::string, double>{};
    const std::filesystem::path file = mediaPath(dir_, mediaPaths_.value(QString::fromStdString(data.sourceMediaId.toString())));
    const int engineRate = sampleRate_;
    const double sensitivity = sensitivityOf(params);
    st.previewBusy = true;
    QPointer<ProjectController> self(this);
    const std::uint64_t generation = generation_;
    (void)QtConcurrent::run([self, data, file, entry, from, to, dstFrames, mode, params, sensitivity, engineRate, generation] {
        lpc::audio::AuditionClip clip;
        try {
            const lpc::TimeMap map(data.sourceLength, dstFrames, lpc::flexAnchors(data));
            const std::int64_t srcLo = std::clamp<std::int64_t>(static_cast<std::int64_t>(std::floor(map.toSource(static_cast<double>(from)))), 0, data.sourceLength - 1);
            const std::int64_t srcHi = std::clamp<std::int64_t>(static_cast<std::int64_t>(std::ceil(map.toSource(static_cast<double>(to)))), srcLo + 1, data.sourceLength);
            const lpc::WavData sub = readWindow(file, data.sourceOffset + srcLo, srcHi - srcLo);
            std::vector<lpc::FlexAnchor> anchors;
            for (const lpc::FlexMarker& m : data.markers)
                if (m.src > srcLo && m.src < srcHi && m.dst > from && m.dst < to) anchors.push_back({m.src - srcLo, m.dst - from});
            std::vector<std::int64_t> hits;
            if (entry.transientsDone) {
                for (std::int64_t t : entry.transients)
                    if (t > srcLo && t < srcHi) hits.push_back(t - srcLo);
            } else {
                hits = lpc::findTransients(sub, sensitivity);
            }
            const lpc::WavData out = lpc::renderFlexTime(sub, anchors, hits, timeParams(mode, params), to - from);
            clip = makeClip(out, engineRate);
        } catch (const std::exception&) {
            clip = {};
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, clip = std::move(clip), generation]() mutable {
            if (!self) return;
            FlexState& st = self->flexState();
            st.previewBusy = false;
            if (generation == self->generation_ && self->host_ && !clip.left.empty() && !self->flexStopped_) {
                const std::int64_t length = static_cast<std::int64_t>(clip.left.size());
                std::int64_t at = self->host_->auditionPosition();
                if (at < 0 || at >= length) at = 0;
                (void)self->host_->audition(std::move(clip), at, true);
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

// ---------------------------------------------------------------- Drum Replacement

// A new instrument track on the Drum Kit with a note for every hit of the region (the sound picks the note, the level the velocity).
void ProjectController::appendDrumHitsTrack(nlohmann::json& commands, const RegionRow& row, const std::vector<lpc::DrumHit>& hits, const QString& sound) {
    const lpc::Ticks startTick = static_cast<lpc::Ticks>(std::llround(row.startBeats * lpc::kPPQ));
    const double startSamples = tempoMap_.ticksToSamples(startTick, sampleRate_);
    const int note = sound == "kick" ? 36 : (sound == "tom" ? 47 : (sound == "hihat" ? 42 : (sound == "clap" ? 39 : 38)));
    nlohmann::json notes = nlohmann::json::array();
    for (const lpc::DrumHit& h : hits) {
        const std::int64_t at = static_cast<std::int64_t>(tempoMap_.samplesToTicks(startSamples + static_cast<double>(h.frame), sampleRate_)) - startTick;
        const int velocity = std::clamp(static_cast<int>(std::lround(20.0 + 107.0 * std::min(h.level, 1.2) / 1.2)), 1, 127);
        notes.push_back({{"start", std::max<std::int64_t>(0, at)}, {"length", lpc::kPPQ / 8}, {"note", note}, {"velocity", velocity}});
    }
    const std::string nullId = "00000000-0000-0000-0000-000000000000";
    const QString trackId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    nlohmann::json track = {{"id", trackId.toStdString()}, {"kind", "instrument"}, {"name", "Drum Hits"}, {"color", ""},
                            {"strip", {{"gainDb", 0}, {"pan", 0}, {"mute", false}, {"solo", false}, {"inserts", nlohmann::json::array()}, {"sends", nlohmann::json::array()}, {"output", nullId}}},
                            {"regions", nlohmann::json::array()}, {"automation", nlohmann::json::array()},
                            {"instrument", {{"processorId", lpc::kProcDrums}, {"params", nlohmann::json::object()}, {"state", ""}}}};
    nlohmann::json region = {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()}, {"timeBase", "musical"}, {"start", startTick},
                             {"length", std::max<std::int64_t>(1, static_cast<std::int64_t>(std::llround(row.lengthBeats * lpc::kPPQ)))}, {"mediaId", nullId},
                             {"sourceOffsetFrames", 0}, {"gainDb", 0}, {"notes", notes}, {"name", "Drum Hits"}};
    commands.push_back({{"type", "add_track"}, {"index", -1}, {"track", track}});
    commands.push_back({{"type", "add_region"}, {"trackId", trackId.toStdString()}, {"index", -1}, {"region", region}});
}

// Track > Replace or Double Drum Track: the hits of the selected audio regions get a drum sound. The regions are rendered in a worker thread and
// replaced together with their new files in one undo step (the recording stays in the project).
void ProjectController::drumReplace(const QVariantMap& options) {
    if (!host_) return;
    std::vector<RegionRow> rows;
    bool flexed = false;
    for (const RegionRow* r : selectedRegionRows()) {
        if (!r->audio || r->missing || r->lengthFrames <= 0) continue;
        if (r->flexed) { flexed = true; continue; }
        rows.push_back(*r);
    }
    if (rows.empty()) {
        emit notice(flexed ? "Reset the Flex edits of the region first" : "Select an audio region first");
        return;
    }
    lpc::DrumReplaceParams params;
    const QString modeName = options.value("mode", "replace").toString();
    const bool midiOnly = modeName == "midi";                          // only the hits are made, as MIDI on the Drum Kit; the audio stays
    const bool alsoMidi = midiOnly || options.value("midi", false).toBool();
    params.replace = modeName != "double";
    params.mix = std::clamp(options.value("mix", 100.0).toDouble() / 100.0, 0.0, 1.0);
    params.levelDb = std::clamp(options.value("levelDb", 0.0).toDouble(), -24.0, 12.0);
    params.velocity = std::clamp(options.value("velocity", 70.0).toDouble() / 100.0, 0.0, 1.0);
    params.sensitivity = std::clamp(options.value("sensitivity", 50.0).toDouble() / 100.0, 0.0, 1.0);
    params.minGapMs = std::clamp(options.value("minGapMs", 60.0).toDouble(), 10.0, 500.0);
    const QString sound = options.value("sound", "snare").toString();
    std::filesystem::path customFile;
    if (sound == QLatin1String("file")) {
        customFile = QUrl(options.value("file").toString()).isLocalFile() ? std::filesystem::path(QUrl(options.value("file").toString()).toLocalFile().toStdWString())
                                                                         : std::filesystem::path(options.value("file").toString().toStdWString());
        if (!std::filesystem::exists(customFile)) {
            emit notice("The drum sound file was not found");
            return;
        }
    }
    emit notice(QString("Replacing the drums of %1 region%2…").arg(rows.size()).arg(rows.size() == 1 ? "" : "s"));
    ++flexBusy_;
    emit flexBusyChanged();
    const std::filesystem::path projectDir = dir_;
    const int rate = sampleRate_;
    std::vector<std::filesystem::path> sources;
    for (const RegionRow& r : rows) sources.push_back(mediaPath(projectDir, mediaPaths_.value(r.mediaId)));
    QPointer<ProjectController> self(this);
    const std::uint64_t generation = generation_;
    (void)QtConcurrent::run([self, rows, sources, params, sound, customFile, projectDir, rate, generation, midiOnly, alsoMidi] {
        struct Result {
            RegionRow row;
            std::filesystem::path target;
            QString hash;
            std::int64_t frames = 0;
            int channels = 0, hits = 0;
            std::vector<lpc::DrumHit> hitList;  // for the MIDI track
        };
        std::vector<Result> results;
        QString error;
        try {
            lpc::WavData sample;
            if (!customFile.empty()) sample = lpc::readWav(customFile);
            for (std::size_t k = 0; k < rows.size(); ++k) {
                const RegionRow& row = rows[k];
                const lpc::WavData audio = readWindow(sources[k], row.sourceOffsetFrames, row.lengthFrames);
                lpc::WavData s = customFile.empty() ? lpc::synthDrum(lpc::drumSoundFromName(sound.toStdString()), audio.sampleRate) : sample;
                if (s.sampleRate != audio.sampleRate && s.sampleRate > 0) {  // a sound file at another rate: linear resampling is enough for a drum hit
                    const double ratio = static_cast<double>(s.sampleRate) / audio.sampleRate;
                    lpc::WavData r;
                    r.sampleRate = audio.sampleRate;
                    r.channels = s.channels;
                    const std::int64_t n = static_cast<std::int64_t>(static_cast<double>(s.frames()) / ratio);
                    r.samples.resize(static_cast<std::size_t>(n * s.channels));
                    for (std::int64_t i = 0; i < n; ++i) {
                        const double pos = static_cast<double>(i) * ratio;
                        const std::int64_t a = static_cast<std::int64_t>(pos);
                        const float f = static_cast<float>(pos - static_cast<double>(a));
                        for (int c = 0; c < s.channels; ++c) {
                            const float x0 = s.samples[static_cast<std::size_t>(std::min(a, s.frames() - 1) * s.channels + c)], x1 = s.samples[static_cast<std::size_t>(std::min(a + 1, s.frames() - 1) * s.channels + c)];
                            r.samples[static_cast<std::size_t>(i * s.channels + c)] = x0 * (1 - f) + x1 * f;
                        }
                    }
                    s = std::move(r);
                }
                const std::vector<lpc::DrumHit> hits = lpc::findDrumHits(audio, params.sensitivity, params.minGapMs);
                if (hits.empty()) continue;
                if (midiOnly) {
                    Result only;
                    only.row = row;
                    only.hits = static_cast<int>(hits.size());
                    only.hitList = hits;
                    results.push_back(std::move(only));
                    continue;
                }
                const lpc::WavData out = lpc::replaceDrums(audio, hits, s, params);
                Result res;
                res.row = row;
                if (alsoMidi) res.hitList = hits;
                res.frames = out.frames();
                res.channels = out.channels;
                res.hits = static_cast<int>(hits.size());
                const std::filesystem::path audioDir = projectDir / "audio";
                std::filesystem::create_directories(audioDir);
                std::u16string stem = sources[k].stem().u16string();
                for (int n = 1;; ++n) {
                    res.target = audioDir / (stem + u" (drums" + (n > 1 ? u" " + QString::number(n).toStdU16String() : std::u16string()) + u").wav");
                    if (!std::filesystem::exists(res.target)) break;
                }
                lpc::writeWav(res.target, out.sampleRate > 0 ? out.sampleRate : rate, out.channels, out.samples, lpc::WavFormat::Pcm24);
                res.hash = hashOf(res.target);
                results.push_back(std::move(res));
            }
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, results = std::move(results), error, projectDir, rate, generation, sound, midiOnly]() mutable {
            std::error_code ignore;
            auto discard = [&] {
                for (auto& r : results) std::filesystem::remove(r.target, ignore);
            };
            if (!self) { discard(); return; }
            --self->flexBusy_;
            emit self->flexBusyChanged();
            if (generation != self->generation_ || !self->host_ || self->dir_ != projectDir) { discard(); return; }
            if (!error.isEmpty()) {
                discard();
                self->setError("Cannot replace the drums: " + error);
                return;
            }
            if (results.empty()) {
                emit self->notice("No drum hits were found: raise the sensitivity");
                return;
            }
            nlohmann::json commands = nlohmann::json::array();
            std::vector<std::filesystem::path> files;
            int total = 0;
            for (auto& r : results) {
                const RegionRow* row = self->regions_.find(r.row.id);
                if (!row || row->lengthFrames != r.row.lengthFrames) { std::filesystem::remove(r.target, ignore); continue; }
                nlohmann::json region = nlohmann::json::parse(row->json, nullptr, false);
                if (region.is_discarded()) continue;
                if (!r.hitList.empty()) self->appendDrumHitsTrack(commands, *row, r.hitList, sound);
                if (r.target.empty()) { total += r.hits; continue; }  // MIDI only: the audio stays as it is
                lpc::MediaItem item;
                item.id = lpc::Uuid::random();
                const std::u8string rel = (std::filesystem::path("audio") / r.target.filename()).generic_u8string();
                item.path.assign(rel.begin(), rel.end());
                item.hash = r.hash.toStdString();
                item.sampleRate = rate;
                item.channels = r.channels;
                item.frames = r.frames;
                commands.push_back({{"type", "add_media"}, {"item", item}, {"index", -1}});
                region["mediaId"] = item.id.toString();
                region["sourceOffsetFrames"] = 0;
                commands.push_back({{"type", "replace_region"}, {"region", region}});
                files.push_back(r.target);
                total += r.hits;
            }
            if (commands.empty()) return;
            self->sendCommand({{"type", "transaction"}, {"commands", commands}}, [files](bool accepted) {
                if (accepted) return;
                std::error_code ec;
                for (const auto& f : files) std::filesystem::remove(f, ec);
            });
            emit self->notice(QString(midiOnly ? "Made MIDI for %1 drum hit%2" : "Replaced %1 drum hit%2").arg(total).arg(total == 1 ? "" : "s"));
        }, Qt::QueuedConnection);
    });
}


// ---------------------------------------------------------------- Session Players

// Track > New Session Player Track: an instrument track with a region of generated notes (drums on the Drum Kit, bass and keys on the synth)
// that follow the chord progression. The part starts on the bar the playhead is in and repeats the progression `repeats` times. The settings
// stay in the region (Region::generator), so the part can be made again with another seed (Regenerate Session Player Part).

namespace {

struct SessionPart {
    nlohmann::json notes = nlohmann::json::array();
    nlohmann::json generator;
    double progressionBeats = 0.0;
    int repeats = 1;
    lpc::SessionKind kind = lpc::SessionKind::Drummer;
    std::string error;
};

SessionPart buildSession(const QVariantMap& options, int beatsPerBar) {
    SessionPart out;
    const QString kindName = options.value("kind", "drummer").toString();
    lpc::SessionParams params;
    params.kind = kindName == "bass" ? lpc::SessionKind::Bass : (kindName == "keys" ? lpc::SessionKind::Keys : lpc::SessionKind::Drummer);
    out.kind = params.kind;
    const auto& styles = lpc::sessionStyles(params.kind);
    params.style = options.value("style", QString::fromStdString(styles.front())).toString().toStdString();
    if (std::find(styles.begin(), styles.end(), params.style) == styles.end()) params.style = styles.front();
    params.intensity = std::clamp(options.value("intensity", 60.0).toDouble() / 100.0, 0.0, 1.0);
    params.swing = std::clamp(options.value("swing", 0.0).toDouble() / 100.0, 0.0, 1.0);
    params.beatsPerBar = std::max(1, beatsPerBar);
    params.seed = static_cast<unsigned>(options.value("seed", 1).toUInt());
    const std::string chordText = options.value("chords", "Am F C G").toString().toStdString();
    const std::vector<lpc::SessionChord> chords = lpc::parseChords(chordText, params.beatsPerBar, out.error);
    if (!out.error.empty()) return out;
    out.repeats = std::clamp(options.value("repeats", 2).toInt(), 1, 64);
    for (const lpc::SessionChord& c : chords) out.progressionBeats += c.beats;
    for (int r = 0; r < out.repeats; ++r) {
        // each repeat is generated again with its own seed, so the repeats are not copies of each other (the drummer's fills still fall every fourth bar)
        lpc::SessionParams again = params;
        again.seed = params.seed + static_cast<unsigned>(r);
        const std::int64_t offset = static_cast<std::int64_t>(std::llround(out.progressionBeats * r * lpc::kPPQ));
        for (const lpc::MidiNote& n : lpc::generateSession(chords, again))
            out.notes.push_back({{"start", n.start + offset}, {"length", n.length}, {"note", n.note}, {"velocity", n.velocity}});
    }
    out.generator = {{"kind", kindName.toStdString()}, {"style", params.style}, {"chords", chordText}, {"repeats", out.repeats},
                     {"intensity", params.intensity * 100.0}, {"swing", params.swing * 100.0}, {"seed", params.seed}};
    return out;
}

const char* sessionName(lpc::SessionKind k) {
    static const char* names[] = {"Drummer", "Bass Player", "Keyboard Player"};
    return names[static_cast<int>(k)];
}

}  // namespace

void ProjectController::newSessionPlayer(const QVariantMap& options) {
    if (!host_) return;
    const SessionPart part = buildSession(options, beatsPerBar_);
    if (!part.error.empty()) {
        emit notice(QString::fromStdString(part.error));
        return;
    }
    if (part.notes.empty()) {
        emit notice("The Session Player has nothing to play for these chords");
        return;
    }
    const double barBeats = std::max(1, beatsPerBar_) * 4.0 / std::max(1, beatUnit_);
    const double startBeats = options.contains("startBeats") ? std::max(0.0, options.value("startBeats").toDouble())
                                                             : std::floor(std::max(0.0, positionBeats_) / barBeats) * barBeats;
    const std::string nullId = "00000000-0000-0000-0000-000000000000";
    const QString trackId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    nlohmann::json instrument;
    if (part.kind == lpc::SessionKind::Drummer) {
        instrument = {{"processorId", lpc::kProcDrums}, {"params", nlohmann::json::object()}, {"state", ""}};
    } else if (part.kind == lpc::SessionKind::Bass) {
        instrument = {{"processorId", lpc::kProcSynth}, {"state", ""}, {"params", {{"wave", 2}, {"attack", 4}, {"decay", 200}, {"sustain", 60}, {"release", 120}, {"cutoff", 700}, {"level", -6}}}};
    } else {
        instrument = {{"processorId", lpc::kProcSynth}, {"state", ""}, {"params", {{"wave", 1}, {"attack", 8}, {"decay", 300}, {"sustain", 70}, {"release", 250}, {"cutoff", 5000}, {"level", -10}}}};
    }
    nlohmann::json track = {{"id", trackId.toStdString()}, {"kind", "instrument"}, {"name", sessionName(part.kind)}, {"color", ""},
                            {"strip", {{"gainDb", 0}, {"pan", 0}, {"mute", false}, {"solo", false}, {"inserts", nlohmann::json::array()}, {"sends", nlohmann::json::array()}, {"output", nullId}}},
                            {"regions", nlohmann::json::array()}, {"automation", nlohmann::json::array()}, {"instrument", instrument}};
    nlohmann::json region = {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()}, {"timeBase", "musical"},
                             {"start", static_cast<std::int64_t>(std::llround(startBeats * lpc::kPPQ))},
                             {"length", static_cast<std::int64_t>(std::llround(part.progressionBeats * part.repeats * lpc::kPPQ))}, {"mediaId", nullId}, {"sourceOffsetFrames", 0},
                             {"gainDb", 0}, {"notes", part.notes}, {"name", sessionName(part.kind)}, {"generator", part.generator.dump()}};
    nlohmann::json commands = nlohmann::json::array();
    commands.push_back({{"type", "add_track"}, {"index", -1}, {"track", track}});
    commands.push_back({{"type", "add_region"}, {"trackId", trackId.toStdString()}, {"index", -1}, {"region", region}});
    sendCommand({{"type", "transaction"}, {"commands", commands}});
    emit notice(QString("Created the %1 with %2 notes").arg(sessionName(part.kind)).arg(part.notes.size()));
}

// Makes the part again with the next seed (the same settings): one undo step, the region keeps its place and length.
void ProjectController::sessionRegenerate(const QString& regionId) {
    const RegionRow* row = regions_.find(regionId);
    if (!host_ || !row) return;
    nlohmann::json region = nlohmann::json::parse(row->json, nullptr, false);
    if (region.is_discarded() || !region.contains("generator")) {
        emit notice("This region was not made by a Session Player");
        return;
    }
    const nlohmann::json g = nlohmann::json::parse(region["generator"].get<std::string>(), nullptr, false);
    if (g.is_discarded() || !g.is_object()) return;
    QVariantMap options;
    options["kind"] = QString::fromStdString(g.value("kind", std::string("drummer")));
    options["style"] = QString::fromStdString(g.value("style", std::string()));
    options["chords"] = QString::fromStdString(g.value("chords", std::string("Am F C G")));
    options["repeats"] = g.value("repeats", 2);
    options["intensity"] = g.value("intensity", 60.0);
    options["swing"] = g.value("swing", 0.0);
    options["seed"] = static_cast<unsigned>(g.value("seed", 1u) + 1u);
    const SessionPart part = buildSession(options, beatsPerBar_);
    if (!part.error.empty()) {
        emit notice(QString::fromStdString(part.error));
        return;
    }
    const std::int64_t length = region.value("length", std::int64_t{0});
    nlohmann::json notes = nlohmann::json::array();
    for (const auto& n : part.notes)
        if (n["start"].get<std::int64_t>() < length) notes.push_back(n);
    region["notes"] = notes;
    region["generator"] = part.generator.dump();
    sendCommand({{"type", "replace_region"}, {"region", region}});
    emit notice("Made the part again");
}

// The chords written as marker names ("Am", "F", "C/E", "G7"...): one per bar from the marker to the next marker (a marker that is not a chord
// ends the one before it). `chords` is the text for the dialog, `startBeats` the bar of the first chord marker.
QVariantMap ProjectController::sessionChordsFromMarkers() const {
    std::vector<MarkerRow> markers = markerRows_;
    std::sort(markers.begin(), markers.end(), [](const MarkerRow& a, const MarkerRow& b) { return a.beats < b.beats; });
    const double barBeats = std::max(1, beatsPerBar_) * 4.0 / std::max(1, beatUnit_);
    QStringList bars;
    double first = -1.0;
    for (std::size_t i = 0; i < markers.size(); ++i) {
        std::string error;
        const std::string name = markers[i].name.trimmed().toStdString();
        if (name.empty() || name.find_first_of(" .%") != std::string::npos) continue;
        if (lpc::parseChords(name, std::max(1, beatsPerBar_), error).empty() || !error.empty()) continue;
        const double end = i + 1 < markers.size() ? markers[i + 1].beats : markers[i].beats + barBeats;
        if (first < 0) first = std::floor(markers[i].beats / barBeats) * barBeats;
        const int count = std::max(1, static_cast<int>(std::lround((end - markers[i].beats) / barBeats)));
        bars << QString::fromStdString(name);
        for (int k = 1; k < count; ++k) bars << QStringLiteral("%");
    }
    if (bars.isEmpty()) return {};
    return QVariantMap{{"chords", bars.join(' ')}, {"startBeats", first}, {"bars", static_cast<int>(bars.size())}};
}


}  // namespace jad
