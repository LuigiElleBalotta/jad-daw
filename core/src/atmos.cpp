#include "lpc/atmos.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <stdexcept>

#include "lpc/automation.h"
#include "lpc/effect_specs.h"
#include "lpc/offline_render.h"
#include "lpc/processor_ids.h"
#include "lpc/validation.h"
#include "lpc/wav.h"

namespace lpc {

namespace {

double paramOf(const ProcessorRef& ref, const char* name) {
    const EffectSpec* spec = findEffectSpec(kProcObjectPanner);
    const EffectParam* p = spec ? spec->find(name) : nullptr;
    if (!p) return 0.0;
    const auto it = ref.params.find(name);
    return std::clamp(it == ref.params.end() ? p->def : it->second, p->min, p->max);
}

int indexOfParam(const char* name) {
    const EffectSpec* spec = findEffectSpec(kProcObjectPanner);
    if (!spec) return -1;
    for (std::size_t i = 0; i < spec->params.size(); ++i)
        if (spec->params[i].name == name) return static_cast<int>(i);
    return -1;
}

std::string safeName(std::string name) {
    for (char& c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_')) c = '_';
    if (name.empty()) name = "Object";
    if (name.size() > 40) name.resize(40);
    return name;
}

}  // namespace

bool dolbyAtmosOn(const Project& p) {
    const auto it = p.settings.find("spatialAudio");
    return it != p.settings.end() && it->second == "dolbyAtmos";
}

int objectPannerIndex(const Track& t) {
    for (std::size_t i = 0; i < t.strip.inserts.size(); ++i)
        if (t.strip.inserts[i].processorId == kProcObjectPanner) return static_cast<int>(i);
    return -1;
}

std::vector<Uuid> atmosObjects(const Project& p) {
    std::vector<Uuid> out;
    for (const Track& t : p.tracks)
        if (objectPannerIndex(t) >= 0) out.push_back(t.id);
    return out;
}

AdmPosition objectPositionAt(const Project& p, const Track& t, Ticks tick) {
    const int at = objectPannerIndex(t);
    AdmPosition pos;
    if (at < 0) return pos;
    const ProcessorRef& ref = t.strip.inserts[static_cast<std::size_t>(at)];
    pos.x = paramOf(ref, "x");
    pos.y = paramOf(ref, "y");
    pos.z = paramOf(ref, "z");
    pos.size = paramOf(ref, "size");
    if (t.automationMode == "off") return pos;
    for (const AutomationLane& lane : playingLanes(p, t)) {
        const auto target = parseParamTarget(lane.target);
        if (!target || target->processorId != kProcObjectPanner || target->ordinal != 0 || lane.points.empty()) continue;
        const EffectSpec* spec = findEffectSpec(kProcObjectPanner);
        if (!spec || target->index < 0 || target->index >= static_cast<int>(spec->params.size())) continue;
        const EffectParam& param = spec->params[static_cast<std::size_t>(target->index)];
        const double n = std::clamp(automationValueAt(lane.points, tick, (paramOf(ref, param.name.c_str()) - param.min) / (param.max - param.min)), 0.0, 1.0);
        const double value = param.min + n * (param.max - param.min);
        if (param.name == "x") pos.x = value;
        else if (param.name == "y") pos.y = value;
        else if (param.name == "z") pos.z = value;
        else if (param.name == "size") pos.size = value;
    }
    return pos;
}

AdmFile renderAdm(const Project& project, MediaStore& media, const AtmosExportOptions& options) {
    const std::vector<Uuid> objects = atmosObjects(project);
    if (objects.size() > static_cast<std::size_t>(kMaxAtmosObjects)) throw std::runtime_error("a Dolby Atmos master has at most 118 object tracks");
    RenderOptions base;
    base.startFrame = options.startFrame;
    base.tailSeconds = options.tailSeconds;
    base.plugins = options.plugins;
    base.frames = options.frames;
    if (base.frames < 0) {
        const std::int64_t end = projectEndFrame(project);
        if (end <= options.startFrame) throw std::runtime_error("nothing to export: the project has no regions");
        base.frames = end - options.startFrame + static_cast<std::int64_t>(std::llround(options.tailSeconds * project.sampleRate));
    }
    base.tailSeconds = 0.0;

    AdmFile file;
    file.sampleRate = project.sampleRate;
    file.bitDepth = options.bitDepth == 16 ? 16 : 24;

    // the bed: the tracks that are not objects, with the master's processing
    {
        Project bed = project;
        for (Track& t : bed.tracks)
            if (objectPannerIndex(t) >= 0) t.on = false;  // an object is not part of the bed (its sends to buses are lost with it)
        const RenderResult r = renderOffline(bed, media, base);
        file.bed.assign(static_cast<std::size_t>(kAdmBedChannels), std::vector<float>(static_cast<std::size_t>(r.frames), 0.0f));
        for (std::int64_t i = 0; i < r.frames; ++i) {
            file.bed[0][static_cast<std::size_t>(i)] = r.interleaved[static_cast<std::size_t>(i) * 2];
            file.bed[1][static_cast<std::size_t>(i)] = r.interleaved[static_cast<std::size_t>(i) * 2 + 1];
        }
    }
    for (const Uuid& id : objects) {
        const Track* source = project.findTrack(id);
        Project q = project;
        q.panLaw = PanLaw::Minus3Comp;  // the centre is unity: a mono track panned to the middle comes out as it is on both sides
        q.masterVolumeDb = 0.0f;
        for (Track& t : q.tracks) {
            if (t.id == id) {
                t.strip.pan = 0.0f;
                t.strip.inserts.erase(t.strip.inserts.begin() + objectPannerIndex(t));
                t.strip.mute = false;
                t.strip.sends.clear();         // an object is its own sound, without the buses
                t.strip.output = Uuid{};       // straight to the master
            } else if (t.kind == TrackKind::Audio || t.kind == TrackKind::Instrument || t.kind == TrackKind::Midi) {
                t.on = false;
            } else if (t.kind == TrackKind::Master) {
                t.strip.inserts.clear();       // the master's processing is not part of an object
            }
        }
        const RenderResult r = renderOffline(q, media, base);
        AdmObject object;
        object.name = source->name.empty() ? "Object" : source->name;
        object.samples.resize(static_cast<std::size_t>(r.frames));
        for (std::int64_t i = 0; i < r.frames; ++i) object.samples[static_cast<std::size_t>(i)] = 0.5f * (r.interleaved[static_cast<std::size_t>(i) * 2] + r.interleaved[static_cast<std::size_t>(i) * 2 + 1]);
        // the movement, sampled and then merged where nothing changes
        const double seconds = static_cast<double>(r.frames) / project.sampleRate;
        const double step = 1.0 / std::max(1.0, options.positionsPerSecond);
        for (double t = 0.0; t < seconds + 1e-9; t += step) {
            const std::int64_t frame = options.startFrame + static_cast<std::int64_t>(std::llround(t * project.sampleRate));
            AdmPosition pos = objectPositionAt(project, *source, project.tempoMap.samplesToTicks(static_cast<double>(frame), project.sampleRate));
            pos.time = t;
            if (!object.positions.empty()) {
                const AdmPosition& last = object.positions.back();
                if (last.x == pos.x && last.y == pos.y && last.z == pos.z && last.size == pos.size) continue;
            }
            object.positions.push_back(pos);
        }
        if (object.positions.empty()) object.positions.push_back(AdmPosition{});
        file.objects.push_back(std::move(object));
    }
    return file;
}

Project projectFromAdm(const AdmFile& file, const std::filesystem::path& folder) {
    Project p;
    p.sampleRate = file.sampleRate;
    p.settings["spatialAudio"] = "dolbyAtmos";
    p.settings["atmosMonitor"] = "binaural";
    std::filesystem::create_directories(folder / "audio");
    std::set<std::string> used;
    const auto fileFor = [&](const std::string& name) {
        std::string base = safeName(name), candidate = base;
        for (int n = 2; used.count(candidate); ++n) candidate = base + " " + std::to_string(n);
        used.insert(candidate);
        return candidate;
    };
    const auto addTrack = [&](const std::string& name, const std::vector<float>& interleaved, int channels, Track& track) {
        const std::string stem = fileFor(name);
        const std::filesystem::path path = folder / "audio" / (stem + ".wav");
        writeWav(path, file.sampleRate, channels, interleaved, WavFormat::Pcm24);
        MediaItem item;
        item.id = Uuid::random();
        item.path = "audio/" + stem + ".wav";
        item.hash = "adm";
        item.sampleRate = file.sampleRate;
        item.channels = channels;
        item.frames = static_cast<std::int64_t>(interleaved.size() / static_cast<std::size_t>(channels));
        p.mediaPool.push_back(item);
        track.id = Uuid::random();
        track.kind = TrackKind::Audio;
        track.name = name.empty() ? stem : name;
        Region r;
        r.id = Uuid::random();
        r.start = 0;
        r.length = std::max<Ticks>(1, p.tempoMap.samplesToTicks(static_cast<double>(item.frames), file.sampleRate));
        r.mediaId = item.id;
        track.regions.push_back(r);
    };
    if (!file.bed.empty()) {  // the bed, folded to stereo
        const std::size_t n = file.bed[0].size();
        std::vector<float> stereo(n * 2);
        const float c = 0.7071f, s = 0.7071f, r = 0.5f, t = 0.35f;
        for (std::size_t i = 0; i < n; ++i) {
            const auto at = [&](int ch) { return file.bed[static_cast<std::size_t>(ch)].size() > i ? file.bed[static_cast<std::size_t>(ch)][i] : 0.0f; };
            stereo[i * 2] = at(0) + c * at(2) + s * at(4) + r * at(6) + t * at(8);
            stereo[i * 2 + 1] = at(1) + c * at(2) + s * at(5) + r * at(7) + t * at(9);
        }
        bool silent = true;
        for (const float v : stereo) silent = silent && std::abs(v) < 1e-7f;
        if (!silent) {
            Track bed;
            addTrack("Bed", stereo, 2, bed);
            p.tracks.push_back(std::move(bed));
        }
    }
    const int ix = indexOfParam("x"), iy = indexOfParam("y"), iz = indexOfParam("z"), isize = indexOfParam("size");
    for (const AdmObject& o : file.objects) {
        Track t;
        addTrack(o.name, o.samples, 1, t);
        ProcessorRef panner;
        panner.processorId = kProcObjectPanner;
        const AdmPosition first = o.positions.empty() ? AdmPosition{} : o.positions.front();
        panner.params = {{"x", first.x}, {"y", first.y}, {"z", first.z}, {"size", first.size}, {"mode", 1.0}};
        t.strip.inserts.push_back(panner);
        if (o.positions.size() > 1) {
            const auto lane = [&](int index, const auto& value) {
                AutomationLane l;
                l.id = Uuid::random();
                l.target = makeParamTarget(kProcObjectPanner, 0, index);
                for (const AdmPosition& pos : o.positions) l.points.push_back(AutomationPoint{p.tempoMap.samplesToTicks(pos.time * file.sampleRate, file.sampleRate), value(pos), 0.0});
                t.automation.push_back(std::move(l));
            };
            lane(ix, [](const AdmPosition& q) { return (q.x + 1.0) / 2.0; });
            lane(iy, [](const AdmPosition& q) { return (q.y + 1.0) / 2.0; });
            lane(iz, [](const AdmPosition& q) { return q.z; });
            lane(isize, [](const AdmPosition& q) { return q.size; });
        }
        p.tracks.push_back(std::move(t));
    }
    if (auto e = checkProject(p)) throw std::runtime_error("the file does not make a valid project: " + e->message);
    return p;
}

}  // namespace lpc
