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
#include "lpc/audio_decode.h"
#include "lpc/commands.h"
#include "lpc/media_store.h"
#include "lpc/model_json.h"
#include "lpc/offline_render.h"
#include "lpc/project_io.h"

namespace lpc::cli {

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

struct Usage {
    std::string message;
};

fs::path toPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

const char* kindName(TrackKind k) {
    static const char* names[] = {"audio", "midi", "instrument", "aux", "bus", "master"};
    return names[static_cast<int>(k)];
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

json snapshotOf(const Project& p) {
    json j;
    j["name"] = p.name;
    j["sampleRate"] = p.sampleRate;
    j["tempo"] = json::array();
    for (const auto& e : p.tempoMap.tempos()) j["tempo"].push_back({{"beat", static_cast<double>(e.tick) / kPPQ}, {"bpm", e.bpm}});
    j["timeSignature"] = json::array();
    for (const auto& e : p.tempoMap.signatures()) j["timeSignature"].push_back({{"beat", static_cast<double>(e.tick) / kPPQ}, {"numerator", e.numerator}, {"denominator", e.denominator}});
    j["markers"] = json::array();
    for (const Marker& m : p.markers) j["markers"].push_back({{"id", m.id.toString()}, {"beat", static_cast<double>(m.tick) / kPPQ}, {"name", m.name}, {"text", m.text}});
    j["lengthSeconds"] = std::round(static_cast<double>(projectEndFrame(p)) / std::max(1, p.sampleRate) * 100.0) / 100.0;
    json tracks = json::array();
    for (const Track& t : p.tracks) {
        json x = {{"id", t.id.toString()}, {"name", t.name}, {"kind", kindName(t.kind)}, {"gainDb", t.strip.gainDb}, {"pan", t.strip.pan}, {"mute", t.strip.mute}, {"solo", t.strip.solo}};
        if (!t.strip.output.isNull()) {
            const Track* out = p.findTrack(t.strip.output);
            x["output"] = out ? out->name : t.strip.output.toString();
        } else if (t.kind != TrackKind::Master) {
            x["output"] = "Master";
        }
        if (t.instrument) x["instrument"] = {{"processorId", t.instrument->processorId}, {"params", t.instrument->params}, {"label", t.instrument->label}};
        x["inserts"] = json::array();
        int index = 0;
        for (const ProcessorRef& i : t.strip.inserts) x["inserts"].push_back({{"index", index++}, {"processorId", i.processorId}, {"label", i.label}, {"bypass", i.bypass}, {"params", i.params}});
        x["sends"] = json::array();
        for (const Send& s : t.strip.sends) {
            const Track* target = p.findTrack(s.targetTrackId);
            x["sends"].push_back({{"id", s.id.toString()}, {"target", target ? target->name : s.targetTrackId.toString()}, {"levelDb", s.levelDb}, {"preFader", s.preFader}});
        }
        x["regions"] = json::array();
        for (const Region& r : t.regions) {
            json rr = {{"id", r.id.toString()}, {"startBeat", static_cast<double>(r.start) / kPPQ}, {"lengthBeats", static_cast<double>(r.length) / kPPQ},
                       {"muted", r.muted}, {"gainDb", r.gainDb}, {"notes", static_cast<int>(r.notes.size())}};
            if (!r.mediaId.isNull())
                if (const MediaItem* m = p.findMedia(r.mediaId)) rr["file"] = m->path;
            if (r.flex) rr["flex"] = true;
            if (!r.generator.empty()) rr["sessionPlayer"] = true;
            x["regions"].push_back(std::move(rr));
        }
        if (!t.automation.empty()) {
            x["automation"] = json::array();
            for (const AutomationLane& l : t.automation) x["automation"].push_back({{"target", l.target}, {"points", static_cast<int>(l.points.size())}});
        }
        if (t.flexMode.size()) x["flexMode"] = t.flexMode;
        if (!t.stack.empty()) x["stack"] = t.stack;
        if (!t.parent.isNull()) x["parent"] = t.parent.toString();
        if (!t.alternatives.empty()) {
            x["alternative"] = t.alternative.empty() ? "Main" : t.alternative;
            x["otherAlternatives"] = static_cast<int>(t.alternatives.size());
        }
        tracks.push_back(std::move(x));
    }
    j["tracks"] = std::move(tracks);
    return j;
}

int cmdSnapshot(const std::vector<std::string>& args, std::ostream& out) {
    const Options o = parseOptions(args, {}, {"--full"});
    if (o.positional.size() != 1) throw Usage{"snapshot needs a project directory"};
    const Project p = loadProject(toPath(o.positional[0]));
    out << (o.has("--full") ? toJson(p) : snapshotOf(p)).dump(1) << "\n";
    return 0;
}

int cmdAnalyze(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const Options o = parseOptions(args, {"--track", "--seconds"}, {"--no-tempo"});
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
        if (const auto t = o.get("--track")) {
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
    const AudioReport r = analyseAudio(audio, options);
    json j = toJson(r);
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

bool isEarsCommand(const std::string& cmd) { return cmd == "snapshot" || cmd == "analyze" || cmd == "masking" || cmd == "apply"; }

int runEars(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    try {
        const std::string& cmd = args[0];
        if (cmd == "snapshot") return cmdSnapshot(args, out);
        if (cmd == "analyze") return cmdAnalyze(args, out, err);
        if (cmd == "masking") return cmdMasking(args, out);
        if (cmd == "apply") return cmdApply(args, out);
    } catch (const Usage& e) {
        err << "error: " << e.message << "\n";
        return 1;
    }
    return 1;
}

}  // namespace lpc::cli
