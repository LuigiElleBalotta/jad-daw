// The commands of lpc-cli that let a program (an AI assistant through the MCP server) look at a project and listen to it: they print JSON.
//   snapshot <project>                                  the project as a mixer sees it (tracks, inserts with parameters, sends, regions, tempo)
//   analyze <project|audio file> [--track T] [--seconds a:b] [--no-tempo]   level, loudness, spectrum, stereo, problems, tempo, and a plain reading
//   masking <project> <trackA> <trackB>                 where two tracks pile up in the spectrum
//   apply <project> <commands.json|-> [--out dir] [--dry-run]               JSON commands (one, a list, or {"commands": [...]}) applied in order; nothing is
//                                                       saved unless all of them are accepted
#include "cli_ears.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>

#include <nlohmann/json.hpp>

#include "lpc/audio_analysis.h"
#include "lpc/audio_image.h"
#include "lpc/wav.h"
#include "lpc/audio_decode.h"
#include "lpc/commands.h"
#include "lpc/effect_specs.h"
#include "lpc/mastering_fit.h"
#include "lpc/media_store.h"
#include "lpc/model_json.h"
#include "lpc/offline_render.h"
#include "lpc/processor_ids.h"
#include "lpc/project_summary.h"
#include "lpc/project_io.h"

namespace lpc::cli {

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

struct Usage {
    std::string message;
};

fs::path toPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string text_of(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

const Track* findTrackBy(const Project& p, const std::string& selector) {
    for (const Track& t : p.tracks)
        if (t.id.toString() == selector) return &t;
    const Track* found = nullptr;
    for (const Track& t : p.tracks)
        if (lower(t.name) == lower(selector)) {
            if (found) throw Usage{"the name \"" + selector + "\" is used by more than one track: use the id"};
            found = &t;
        }
    if (!found) throw Usage{"no track \"" + selector + "\" in the project"};
    return found;
}

struct Options {
    std::vector<std::string> positional;
    std::vector<std::pair<std::string, std::string>> named;
    std::optional<std::string> get(const std::string& k) const {
        for (const auto& [a, b] : named)
            if (a == k) return b;
        return std::nullopt;
    }
    bool has(const std::string& k) const { return get(k).has_value(); }
};

Options parseOptions(const std::vector<std::string>& args, const std::vector<std::string>& valued, const std::vector<std::string>& flags) {
    Options o;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a.rfind("--", 0) == 0) {
            if (std::find(flags.begin(), flags.end(), a) != flags.end()) {
                o.named.emplace_back(a, "1");
            } else if (std::find(valued.begin(), valued.end(), a) != valued.end()) {
                if (i + 1 >= args.size()) throw Usage{"option " + a + " needs a value"};
                o.named.emplace_back(a, args[++i]);
            } else {
                throw Usage{"unknown option " + a};
            }
        } else {
            o.positional.push_back(a);
        }
    }
    return o;
}

// The project with only `selector` heard (every other track that is not a bus, aux or the master is muted), so what is rendered is that track after its
// own fader, inserts and the buses it feeds.
Project soloed(Project p, const Track& keep) {
    for (Track& t : p.tracks)
        if (t.id != keep.id && t.kind != TrackKind::Bus && t.kind != TrackKind::Aux && t.kind != TrackKind::Master) t.strip.mute = true;
    return p;
}

WavData renderProject(const Project& p, const fs::path& dir, double fromSec, double toSec) {
    MediaStore media(dir, /*streaming=*/false);
    RenderOptions o;
    o.startFrame = static_cast<std::int64_t>(std::llround(std::max(0.0, fromSec) * p.sampleRate));
    if (toSec > fromSec) o.frames = static_cast<std::int64_t>(std::llround((toSec - fromSec) * p.sampleRate));
    o.tailSeconds = 0.3;
    const RenderResult r = renderOffline(p, media, o);
    WavData d;
    d.sampleRate = r.sampleRate;
    d.channels = 2;
    d.samples = r.interleaved;
    return d;
}

bool isAudioFile(const fs::path& p) {
    const std::string e = lower(p.extension().string());
    return e == ".wav" || e == ".mp3" || e == ".flac" || e == ".aif" || e == ".aiff" || e == ".ogg" || e == ".m4a";
}

void parseRange(const std::optional<std::string>& s, double& from, double& to) {
    from = 0;
    to = -1;
    if (!s) return;
    const std::size_t colon = s->find(':');
    try {
        if (colon == std::string::npos) throw std::invalid_argument("x");
        from = std::stod(s->substr(0, colon));
        to = std::stod(s->substr(colon + 1));
    } catch (const std::exception&) {
        throw Usage{"--seconds needs a range like 10:40"};
    }
    if (!(from >= 0 && to > from)) throw Usage{"--seconds needs a range like 10:40"};
}

int cmdSnapshot(const std::vector<std::string>& args, std::ostream& out) {
    const Options o = parseOptions(args, {}, {"--full"});
    if (o.positional.size() != 1) throw Usage{"snapshot needs a project directory"};
    const Project p = loadProject(toPath(o.positional[0]));
    out << (o.has("--full") ? toJson(p) : projectSummary(p)).dump(1) << "\n";
    return 0;
}

int cmdAnalyze(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const Options o = parseOptions(args, {"--track", "--seconds", "--region", "--image", "--kind"}, {"--no-tempo"});
    if (o.positional.size() != 1) throw Usage{"analyze needs a project directory or an audio file"};
    const fs::path target = toPath(o.positional[0]);
    double from = 0, to = -1;
    parseRange(o.get("--seconds"), from, to);
    AnalysisOptions options;
    options.tempo = !o.has("--no-tempo");
    WavData audio;
    std::string what;
    if (fs::is_regular_file(target) && isAudioFile(target)) {
        audio = decodeAudioFile(target);
        if (to > from) {  // a range of the file
            const std::int64_t a = std::min<std::int64_t>(audio.frames(), static_cast<std::int64_t>(from * audio.sampleRate)), b = std::min<std::int64_t>(audio.frames(), static_cast<std::int64_t>(to * audio.sampleRate));
            audio.samples = std::vector<float>(audio.samples.begin() + a * audio.channels, audio.samples.begin() + b * audio.channels);
        }
        what = "file " + target.filename().string();
    } else {
        const Project p = loadProject(target);
        if (const auto region = o.get("--region")) {  // one region alone: a take, to compare with the others
            const Track* owner = nullptr;
            const Region* hit = nullptr;
            for (const Track& t : p.tracks)
                for (const Region& r : t.regions)
                    if (r.id.toString() == *region) owner = &t, hit = &r;
            if (!owner) throw Usage{"no region " + *region + " in the project"};
            if (!(to > from)) {  // the span of the region, unless a range was given
                const double rate = std::max(1, p.sampleRate);
                if (hit->timeBase == TimeBase::Absolute) {
                    from = static_cast<double>(hit->start) / 1e6;
                    to = static_cast<double>(hit->start + hit->length) / 1e6;
                } else {
                    from = p.tempoMap.ticksToSamples(hit->start, p.sampleRate) / rate;
                    to = p.tempoMap.ticksToSamples(hit->start + hit->length, p.sampleRate) / rate;
                }
            }
            Project only = p;
            for (Track& t : only.tracks)
                if (t.id == owner->id) std::erase_if(t.regions, [&](const Region& r) { return r.id.toString() != *region; });
            audio = renderProject(soloed(only, *owner), target, from, to);
            what = "region " + *region + " alone (track \"" + owner->name + "\")";
        } else if (const auto t = o.get("--track")) {
            const Track* track = findTrackBy(p, *t);
            if (track->kind == TrackKind::Master) {
                audio = renderProject(p, target, from, to);
                what = "the master";
            } else {
                audio = renderProject(soloed(p, *track), target, from, to);
                what = "track \"" + track->name + "\" alone (after its fader, inserts and buses)";
            }
        } else {
            audio = renderProject(p, target, from, to);
            what = "the whole mix";
        }
    }
    if (const auto png = o.get("--image")) {  // a picture of the same audio, for a model that can look
        const std::string kind = o.get("--kind").value_or("spectrogram");
        Image img;
        if (kind == "spectrogram") img = spectrogramImage(audio);
        else if (kind == "waveform") img = waveformImage(audio);
        else if (kind == "spectrum") img = spectrumImage(audio);
        else throw Usage{"--kind must be spectrogram, waveform or spectrum"};
        if (img.width == 0 || audio.frames() < 8192) throw Usage{"the audio is too short for a picture"};
        writePng(toPath(*png), img);
    }
    const AudioReport r = analyseAudio(audio, options);
    json j = toJson(r);
    if (o.has("--image")) j["image"] = {{"file", *o.get("--image")}, {"kind", o.get("--kind").value_or("spectrogram")}};
    j["analysed"] = what;
    j["reading"] = describeReport(r);
    out << j.dump(1) << "\n";
    (void)err;
    return 0;
}

int cmdMasking(const std::vector<std::string>& args, std::ostream& out) {
    const Options o = parseOptions(args, {"--seconds"}, {});
    if (o.positional.size() != 3) throw Usage{"masking needs a project directory and two tracks"};
    const fs::path dir = toPath(o.positional[0]);
    const Project p = loadProject(dir);
    double from = 0, to = -1;
    parseRange(o.get("--seconds"), from, to);
    AnalysisOptions options;
    options.tempo = false;
    options.timeline = false;
    const Track* a = findTrackBy(p, o.positional[1]);
    const Track* b = findTrackBy(p, o.positional[2]);
    const AudioReport ra = analyseAudio(renderProject(soloed(p, *a), dir, from, to), options);
    const AudioReport rb = analyseAudio(renderProject(soloed(p, *b), dir, from, to), options);
    const MaskingReport m = compareMasking(ra, rb);
    json j = {{"trackA", a->name}, {"trackB", b->name}, {"score", std::round(m.score * 100.0) / 100.0}, {"worstBandsHz", m.worstHz}};
    j["overlapByBand"] = json::array();
    for (std::size_t i = 0; i < m.overlap.size(); ++i) j["overlapByBand"].push_back({{"hz", std::round(m.bandCentres[i])}, {"overlap", std::round(m.overlap[i] * 100.0) / 100.0}});
    std::string advice;
    if (m.score > 0.5 && !m.worstHz.empty()) advice = "The two tracks pile up around " + std::to_string(static_cast<int>(m.worstHz.front())) + " Hz: cut one of them there with an EQ, or pan them apart.";
    else if (m.score > 0.25) advice = "Some overlap: check the bands listed.";
    else advice = "The two tracks sit in different parts of the spectrum.";
    j["advice"] = advice;
    out << j.dump(1) << "\n";
    return 0;
}

int cmdApply(const std::vector<std::string>& args, std::ostream& out) {
    const Options o = parseOptions(args, {"--out"}, {"--dry-run"});
    if (o.positional.size() != 2) throw Usage{"apply needs a project directory and a JSON file (or - for the standard input)"};
    const fs::path dir = toPath(o.positional[0]);
    Project p = loadProject(dir);
    std::string raw;
    if (o.positional[1] == "-") {
        raw.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    } else {
        std::ifstream in(toPath(o.positional[1]), std::ios::binary);
        if (!in) throw Usage{"cannot read " + o.positional[1]};
        raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const json doc = json::parse(raw, nullptr, false);
    if (doc.is_discarded()) throw Usage{"the commands are not valid JSON"};
    std::vector<json> list;
    if (doc.is_array()) list.assign(doc.begin(), doc.end());
    else if (doc.is_object() && doc.contains("commands")) list.assign(doc["commands"].begin(), doc["commands"].end());
    else list.push_back(doc);
    json results = json::array();
    bool allOk = true;
    for (const json& c : list) {
        try {
            const CommandPtr cmd = commandFromJson(c);
            const ApplyResult r = cmd->apply(p);
            if (r.ok()) {
                results.push_back({{"type", c.value("type", std::string())}, {"ok", true}});
            } else {
                allOk = false;
                results.push_back({{"type", c.value("type", std::string())}, {"ok", false}, {"error", r.error->code + ": " + r.error->message}});
                break;  // nothing after a refused command is applied
            }
        } catch (const std::exception& e) {
            allOk = false;
            results.push_back({{"type", c.value("type", std::string())}, {"ok", false}, {"error", e.what()}});
            break;
        }
    }
    json j = {{"applied", allOk}, {"results", results}};
    if (allOk && !o.has("--dry-run")) {
        const fs::path target = o.has("--out") ? toPath(*o.get("--out")) : dir;
        if (target != dir) fs::copy(dir, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        saveProject(p, target);
        j["savedTo"] = target.string();
    } else if (!allOk) {
        j["savedTo"] = nullptr;  // the project on disk was not touched
    }
    out << j.dump(1) << "\n";
    return allOk ? 0 : 2;
}

// The built-in effects and instruments with their parameters (name, unit, range, default): what `add_insert` and `set_insert_param` can use.
int cmdEffects(std::ostream& out) {
    auto specsJson = [](const std::vector<EffectSpec>& specs) {
        json list = json::array();
        for (const EffectSpec& s : specs) {
            json params = json::array();
            for (const EffectParam& p : s.params)
                params.push_back({{"name", p.name}, {"label", p.label}, {"unit", p.unit}, {"min", p.min}, {"max", p.max}, {"default", p.def}, {"logarithmic", p.logarithmic}});
            list.push_back({{"id", s.id}, {"name", s.name}, {"group", s.group}, {"parameters", params}});
        }
        return list;
    };
    out << json{{"effects", specsJson(effectSpecs())}, {"instruments", specsJson(instrumentSpecs())}}.dump(1) << "\n";
    return 0;
}

// Mastering Assistant on the project: the insert is added to the master when it is not there, analysed, and its parameters are written (unless --dry-run).
int cmdMastering(const std::vector<std::string>& args, std::ostream& out) {
    const Options o = parseOptions(args, {"--out", "--seconds"}, {"--dry-run"});
    if (o.positional.size() != 1) throw Usage{"mastering needs a project directory"};
    const fs::path dir = toPath(o.positional[0]);
    Project p = loadProject(dir);
    double from = 0, to = -1;
    parseRange(o.get("--seconds"), from, to);
    Track* master = nullptr;
    for (Track& t : p.tracks)
        if (t.kind == TrackKind::Master) master = &t;
    if (!master) throw std::runtime_error("the project has no master track");
    int index = -1;
    for (std::size_t i = 0; i < master->strip.inserts.size(); ++i)
        if (master->strip.inserts[i].processorId == kProcMastering) index = static_cast<int>(i);
    bool added = false;
    if (index < 0) {
        master->strip.inserts.push_back(ProcessorRef{kProcMastering, {}, ""});
        index = static_cast<int>(master->strip.inserts.size()) - 1;
        added = true;
    }
    RenderOptions render;
    render.tailSeconds = 0.3;
    render.startFrame = static_cast<std::int64_t>(std::llround(from * p.sampleRate));
    if (to > from) render.frames = static_cast<std::int64_t>(std::llround((to - from) * p.sampleRate));
    const MasteringFit fit = fitMastering(p, dir, master->id, index, render);
    if (fit.silent) throw std::runtime_error("the mix is silent: there is nothing to master");
    master->strip.inserts[static_cast<std::size_t>(index)].params = fit.params;
    json j = {{"added", added},
              {"before", {{"integratedLufs", fit.analysis.before.integrated}, {"rangeLu", fit.analysis.before.range}, {"truePeakDbtp", fit.analysis.before.truePeakDb}}},
              {"after", {{"integratedLufs", fit.output.integrated}, {"rangeLu", fit.output.range}, {"truePeakDbtp", fit.output.truePeakDb}}},
              {"parameters", fit.params}};
    if (!o.has("--dry-run")) {
        const fs::path target = o.has("--out") ? toPath(*o.get("--out")) : dir;
        if (target != dir) fs::copy(dir, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        saveProject(p, target);
        j["savedTo"] = target.string();
    }
    out << j.dump(1) << "\n";
    return 0;
}

}  // namespace

Project soloTrackProject(Project project, const std::string& selector) {
    try {
        const Track* t = findTrackBy(project, selector);
        if (t->kind == TrackKind::Master) return project;
        return soloed(std::move(project), *t);
    } catch (const Usage& u) {
        throw std::runtime_error(u.message);
    }
}

// Brings an audio file (wav, mp3, flac, aiff) into the project like the app does: converted to the project's rate and written as a 24-bit WAV in audio/,
// then a media item and a region (on `--track`, or on a new audio track) in one transaction.
int cmdImport(const std::vector<std::string>& args, std::ostream& out) {
    const Options o = parseOptions(args, {"--track", "--name", "--start-seconds"}, {});
    if (o.positional.size() != 2) throw Usage{"import needs a project directory and an audio file"};
    const fs::path dir = toPath(o.positional[0]);
    const fs::path source = toPath(o.positional[1]);
    Project p = loadProject(dir);
    if (!isImportableAudioExtension(lower(source.extension().string()))) throw Usage{"only WAV, MP3, FLAC and AIFF files can be imported"};
    double start = 0.0;
    if (const auto s = o.get("--start-seconds")) {
        try {
            start = std::stod(*s);
        } catch (const std::exception&) {
            throw Usage{"--start-seconds needs a number"};
        }
        if (!(start >= 0.0)) throw Usage{"--start-seconds needs a number of at least 0"};
    }
    const WavData data = convertForProject(decodeAudioFile(source), p.sampleRate);
    if (data.frames() == 0) throw Usage{"the file has no audio"};
    fs::create_directories(dir / "audio");
    fs::path target;
    for (int n = 1;; ++n) {
        target = dir / "audio" / (n == 1 ? source.stem().u16string() + u".wav" : source.stem().u16string() + u" (" + std::u16string(1, static_cast<char16_t>(u'0' + n % 10)) + u").wav");
        if (n > 9) throw Usage{"too many files with that name in audio/"};
        if (!fs::exists(target)) break;
    }
    writeWav(target, p.sampleRate, data.channels, data.samples, WavFormat::Pcm24);
    std::uint64_t h = 1469598103934665603ull;  // FNV-1a over the file: an identifier, not a security check
    {
        std::ifstream in(target, std::ios::binary);
        char buf[65536];
        while (in.read(buf, sizeof buf) || in.gcount() > 0) {
            for (std::streamsize i = 0; i < in.gcount(); ++i) h = (h ^ static_cast<unsigned char>(buf[i])) * 1099511628211ull;
            if (in.eof()) break;
        }
    }
    MediaItem item;
    item.id = Uuid::random();
    const std::u8string rel = (fs::path("audio") / target.filename()).generic_u8string();
    item.path.assign(rel.begin(), rel.end());
    item.hash = "fnv1a-" + [&] { char b[20]; std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h)); return std::string(b); }();
    item.sampleRate = p.sampleRate;
    item.channels = data.channels;
    item.frames = data.frames();
    Region region;
    region.id = Uuid::random();
    region.timeBase = TimeBase::Absolute;
    region.start = static_cast<std::int64_t>(std::llround(start * 1e6));
    region.length = std::max<std::int64_t>(1, std::llround(static_cast<double>(data.frames()) * 1e6 / p.sampleRate));
    region.mediaId = item.id;
    json commands = json::array();
    Uuid trackId;
    if (const auto selector = o.get("--track")) {
        trackId = findTrackBy(p, *selector)->id;
    } else {
        Track t;
        t.id = Uuid::random();
        t.kind = TrackKind::Audio;
        t.name = o.get("--name").value_or(text_of(source.stem()));
        trackId = t.id;
        commands.push_back({{"type", "add_track"}, {"track", t}, {"index", -1}});
    }
    commands.push_back({{"type", "add_media"}, {"item", item}, {"index", -1}});
    commands.push_back({{"type", "add_region"}, {"trackId", trackId.toString()}, {"region", region}, {"index", -1}});
    const ApplyResult r = commandFromJson({{"type", "transaction"}, {"commands", commands}})->apply(p);
    if (!r.ok()) {
        std::error_code ignore;
        fs::remove(target, ignore);
        throw Usage{"the project refused the import: " + r.error->code + ": " + r.error->message};
    }
    saveProject(p, dir);
    out << json{{"ok", true}, {"trackId", trackId.toString()}, {"file", item.path}, {"seconds", std::round(static_cast<double>(data.frames()) / p.sampleRate * 100.0) / 100.0}}.dump(1) << "\n";
    return 0;
}

bool isEarsCommand(const std::string& cmd) {
    return cmd == "snapshot" || cmd == "analyze" || cmd == "masking" || cmd == "apply" || cmd == "mastering" || cmd == "effects" || cmd == "import";
}

int runEars(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    try {
        const std::string& cmd = args[0];
        if (cmd == "snapshot") return cmdSnapshot(args, out);
        if (cmd == "analyze") return cmdAnalyze(args, out, err);
        if (cmd == "masking") return cmdMasking(args, out);
        if (cmd == "apply") return cmdApply(args, out);
        if (cmd == "mastering") return cmdMastering(args, out);
        if (cmd == "effects") return cmdEffects(out);
        if (cmd == "import") return cmdImport(args, out);
    } catch (const Usage& e) {
        err << "error: " << e.message << "\n";
        return 1;
    }
    return 1;
}

}  // namespace lpc::cli
