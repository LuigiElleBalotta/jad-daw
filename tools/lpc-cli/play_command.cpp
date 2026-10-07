#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iomanip>
#include <ostream>
#include <thread>

#include <juce_events/juce_events.h>

#include "cli_commands.h"
#include "juce_device.h"
#include "lpc/audio/engine.h"
#include "lpc/media_store.h"
#include "lpc/offline_render.h"
#include "lpc/project_host.h"
#include "lpc/project_io.h"

namespace lpc::cli {

namespace {

std::atomic<bool> gInterrupted{false};
extern "C" void onInterrupt(int) { gInterrupted.store(true); }

struct EngineCallback final : IAudioCallback {
    explicit EngineCallback(audio::AudioEngine& e) : engine(e) {}
    void process(float* l, float* r, int n) noexcept override { engine.processBlock(l, r, n); }
    audio::AudioEngine& engine;
};

}  // namespace

int runPlay(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.size() != 1) {
        err << "error: play needs exactly one project directory\nusage: lpc-cli play <project-dir>\n";
        return 1;
    }
    try {
        juce::ScopedJuceInitialiser_GUI juceInit;
        const std::filesystem::path dir = std::filesystem::path(std::u8string(args[0].begin(), args[0].end()));
        const Project project = loadProject(dir);
        MediaStore media(dir, /*streaming=*/true);
        audio::AudioEngine engine(static_cast<double>(project.sampleRate));
        EngineCallback callback(engine);

        auto device = makeJuceAudioDevice();
        std::string error;
        if (!device->open(static_cast<double>(project.sampleRate), 256, callback, error)) {
            err << "error: " << error << "\n";
            return 2;
        }
        // the device is running before the host posts the project, so the message queue is always drained
        const std::int64_t end = projectEndFrame(project) + project.sampleRate / 2;
        int underruns = 0;
        {
            ProjectHost host(project, engine, media);
            host.play().get();
            std::signal(SIGINT, onInterrupt);
            out << "playing " << project.name << " (Ctrl+C to stop)\n";
            int tick = 0;
            while (!gInterrupted.load() && engine.positionFrames() < end) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                underruns += media.takeUnderruns();
                if (++tick % 10 == 0) {
                    const double s = static_cast<double>(engine.positionFrames()) / project.sampleRate;
                    out << std::fixed << std::setprecision(1) << "  " << s << " s  peak " << std::setprecision(3) << engine.masterPeak()
                        << "  underruns " << underruns << "\n";
                }
            }
            host.stop().get();
        }
        device->close();
        for (const std::string& w : media.warnings()) err << "warning: " << w << "\n";
        out << "finished, " << underruns << " underrun(s)\n";
        return 0;
    } catch (const std::exception& e) {
        err << "error: " << e.what() << "\n";
        return 2;
    }
}

}  // namespace lpc::cli
