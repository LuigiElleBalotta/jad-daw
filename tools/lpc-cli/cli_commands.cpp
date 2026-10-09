#include "cli_commands.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <optional>
#include <ostream>

#include "lpc/demo_project.h"
#include "lpc/media_store.h"
#include "lpc/offline_render.h"
#include "lpc/processor_ids.h"
#include "lpc/project_io.h"
#include "lpc/wav.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace lpc::cli {

namespace {
PluginHostFactory gPluginHostFactory;

bool usesPlugins(const lpc::Project& p) {
    for (const lpc::Track& t : p.tracks)
        for (const lpc::ProcessorRef& i : t.strip.inserts)
            if (lpc::isVst3Id(i.processorId)) return true;
    return false;
}
}  // namespace

void setPluginHostFactory(PluginHostFactory factory) { gPluginHostFactory = std::move(factory); }

#ifdef _WIN32
std::string wideToUtf8(const wchar_t* wide) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), size, nullptr, nullptr);
    return out;
}
#endif

namespace {

namespace fs = std::filesystem;

constexpr const char* kUsage =
    "usage:\n"
    "  lpc-cli demo <project-dir>\n"
    "  lpc-cli info <project-dir>\n"
    "  lpc-cli render <project-dir> <out.wav> [--seconds N] [--bits 16|24|32]\n"
    "  lpc-cli play <project-dir>\n";

fs::path toPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string text(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

struct UsageError {
    std::string message;
};

// Splits "positional... --option value ..." and validates the options against `allowed`.
struct Parsed {
    std::vector<std::string> positional;
    std::vector<std::pair<std::string, std::string>> options;
    std::optional<std::string> get(const std::string& name) const {
        for (const auto& [k, v] : options)
            if (k == name) return v;
        return std::nullopt;
    }
};

Parsed parse(const std::vector<std::string>& args, std::size_t from, const std::vector<std::string>& allowed) {
    Parsed p;
    for (std::size_t i = from; i < args.size(); ++i) {
        if (args[i].rfind("--", 0) == 0) {
            if (std::find(allowed.begin(), allowed.end(), args[i]) == allowed.end()) throw UsageError{"unknown option " + args[i]};
            if (i + 1 >= args.size()) throw UsageError{"option " + args[i] + " needs a value"};
            p.options.emplace_back(args[i], args[i + 1]);
            ++i;
        } else {
            p.positional.push_back(args[i]);
        }
    }
    return p;
}

int cmdDemo(const std::vector<std::string>& args, std::ostream& out) {
    const Parsed p = parse(args, 1, {});
    if (p.positional.size() != 1) throw UsageError{"demo needs exactly one project directory"};
    const fs::path dir = toPath(p.positional[0]);
    saveProject(makeDemoProject(dir), dir);
    out << "demo project written to " << text(dir) << "\n";
    return 0;
}

int cmdInfo(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const Parsed p = parse(args, 1, {});
    if (p.positional.size() != 1) throw UsageError{"info needs exactly one project directory"};
    const fs::path dir = toPath(p.positional[0]);
    const Project project = loadProject(dir);
    out << "name: " << project.name << "\n"
        << "sample rate: " << project.sampleRate << "\n"
        << "tempo: " << project.tempoMap.tempos().front().bpm << " bpm (" << project.tempoMap.tempos().size() << " tempo events)\n"
        << "tracks: " << project.tracks.size() << "\n";
    for (const Track& t : project.tracks) {
        static const char* kinds[] = {"audio", "midi", "instrument", "aux", "bus", "master"};
        out << "  - " << t.name << " [" << kinds[static_cast<int>(t.kind)] << "] regions=" << t.regions.size() << " gain=" << t.strip.gainDb
            << " dB pan=" << t.strip.pan << " sends=" << t.strip.sends.size() << "\n";
    }
    out << "media: " << project.mediaPool.size() << "\n";
    MediaStore media(dir, false);
    for (const MediaItem& m : project.mediaPool) {
        out << "  - " << m.path << "\n";
        media.open(m);
    }
    for (const std::string& w : media.warnings()) err << "warning: " << w << "\n";
    return 0;
}

int cmdRender(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const Parsed p = parse(args, 1, {"--seconds", "--bits"});
    if (p.positional.size() != 2) throw UsageError{"render needs a project directory and an output file"};

    std::optional<double> seconds;
    if (const auto s = p.get("--seconds")) {
        try {
            std::size_t used = 0;
            const double v = std::stod(*s, &used);
            if (used != s->size() || !std::isfinite(v) || v < 0.0) throw std::invalid_argument("x");
            seconds = v;
        } catch (const std::exception&) {
            throw UsageError{"--seconds needs a non-negative number"};
        }
    }
    WavFormat format = WavFormat::Pcm24;
    if (const auto b = p.get("--bits")) {
        if (*b == "16") format = WavFormat::Pcm16;
        else if (*b == "24") format = WavFormat::Pcm24;
        else if (*b == "32") format = WavFormat::Float32;
        else throw UsageError{"--bits must be 16, 24 or 32"};
    }

    const fs::path dir = toPath(p.positional[0]);
    const Project project = loadProject(dir);
    MediaStore media(dir, /*streaming=*/false);  // memory sources: deterministic output
    RenderOptions options;
    if (seconds) options.frames = static_cast<std::int64_t>(std::llround(std::min(*seconds * project.sampleRate, 1e15)));
    std::shared_ptr<lpc::IPluginHost> plugins;
    if (usesPlugins(project)) {
        if (!gPluginHostFactory) err << "warning: this build cannot host plug-ins; they are skipped\n";
        else plugins = gPluginHostFactory();
        options.plugins = plugins.get();
        if (plugins) {
            std::vector<std::string> installed;
            for (const lpc::PluginDescriptor& d : plugins->catalogue()) installed.push_back(d.id);
            for (const lpc::Track& t : project.tracks)
                for (const lpc::ProcessorRef& i : t.strip.inserts)
                    if (lpc::isVst3Id(i.processorId) && std::find(installed.begin(), installed.end(), i.processorId) == installed.end())
                        err << "warning: plug-in " << (i.label.empty() ? i.processorId : i.label) << " is not installed; it passes the sound through\n";
        }
    }
    const RenderResult result = renderOffline(project, media, options);
    writeWav(toPath(p.positional[1]), result.sampleRate, 2, result.interleaved, format);
    for (const std::string& w : media.warnings()) err << "warning: " << w << "\n";
    out << "rendered " << result.frames << " frames (" << static_cast<double>(result.frames) / result.sampleRate << " s) to "
        << p.positional[1] << "\n";
    return 0;
}

}  // namespace

int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        err << kUsage;
        return 1;
    }
    try {
        const std::string& cmd = args[0];
        if (cmd == "demo") return cmdDemo(args, out);
        if (cmd == "info") return cmdInfo(args, out, err);
        if (cmd == "render") return cmdRender(args, out, err);
        if (cmd == "play") return runPlay(std::vector<std::string>(args.begin() + 1, args.end()), out, err);
        err << "unknown command: " << cmd << "\n" << kUsage;
        return 1;
    } catch (const UsageError& e) {
        err << "error: " << e.message << "\n" << kUsage;
        return 1;
    } catch (const std::exception& e) {
        err << "error: " << e.what() << "\n";
        return 2;
    }
}

}  // namespace lpc::cli
