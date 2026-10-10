#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <atomic>
#include <random>
#include <chrono>
#include <thread>

#include <windows.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include "juce_plugin_host.h"
#include "lpc/commands.h"
#include "lpc/media_store.h"
#include "lpc/offline_render.h"
#include "lpc/validation.h"
#include "plugin_processor.h"

using namespace lpc;

namespace {

// JUCE lives exactly as long as the test run (created on the main thread, which is the message thread, and shut down before the
// process exits): a namespace-scope or function-local static is constructed too early or destroyed too late for JUCE's own statics.
struct JuceRun final : Catch::EventListenerBase {
    using Catch::EventListenerBase::EventListenerBase;
    void testRunStarting(const Catch::TestRunInfo&) override { init = std::make_unique<juce::ScopedJuceInitialiser_GUI>(); }
    void testRunEnded(const Catch::TestRunStats&) override { init.reset(); }
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> init;
};
CATCH_REGISTER_LISTENER(JuceRun)

void ensureJuce() {}

// Finds the test plug-in the way the scanner will: JUCE reads the .vst3 and describes it.
PluginDescriptor testPlugin() {
    ensureJuce();
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    const juce::File bundle = juce::File(LPC_TEST_VST3_DIR).getChildFile("LPC Test Gain.vst3");
    format.findAllTypesForFile(found, bundle.getFullPathName());
    REQUIRE(found.size() == 1);
    PluginDescriptor d;
    d.id = "vst3:00000000000000000000000000000001";
    d.name = found[0]->name.toStdString();
    d.vendor = found[0]->manufacturerName.toStdString();
    d.path = bundle.getFullPathName().toStdString();
    d.native = found[0]->createXml()->toString().toStdString();
    return d;
}

PluginDescriptor testSynth() {
    ensureJuce();
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    const juce::File bundle = juce::File(LPC_TEST_SYNTH_VST3_DIR).getChildFile("LPC Test Synth.vst3");
    format.findAllTypesForFile(found, bundle.getFullPathName());
    REQUIRE(found.size() == 1);
    REQUIRE(found[0]->isInstrument);
    PluginDescriptor d;
    d.id = "vst3:00000000000000000000000000000002";
    d.name = found[0]->name.toStdString();
    d.vendor = found[0]->manufacturerName.toStdString();
    d.path = bundle.getFullPathName().toStdString();
    d.native = found[0]->createXml()->toString().toStdString();
    d.instrument = true;
    return d;
}

// JUCE_MODAL_LOOPS_PERMITTED is 0, so MessageManager::runDispatchLoopUntil does not exist: run the thread's Win32 queue, which is
// where JUCE's hidden message window gets its messages.
void pump(int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

ProcessorRef refOf(const PluginDescriptor& d, const std::string& state = "") {
    ProcessorRef r;
    r.processorId = d.id;
    r.state = state;
    return r;
}

// The state of a plug-in whose gain parameter (0..2) is set to `gain`, in the format the hosted instance saves and loads.
std::string stateWithGain(const PluginDescriptor& d, float gain) {
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, juce::String(d.path));
    juce::String error;
    juce::AudioPluginFormatManager formats;
    formats.addFormat(new juce::VST3PluginFormat());
    auto instance = formats.createPluginInstance(*found[0], 48000.0, 512, error);
    REQUIRE(instance);
    instance->getParameters()[0]->setValue(gain / 2.0f);  // normalised 0..1 over the range 0..2
    juce::MemoryBlock block;
    instance->getStateInformation(block);
    return PluginProcessor::encodeState(block);
}

}  // namespace

TEST_CASE("juce host: loads on the message thread, reports latency, processes", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    const InsertSlot slot{Uuid{1, 1}, 0};
    auto proc = host.acquire(slot, refOf(d), 48000.0, 512);
    REQUIRE(proc);
    REQUIRE(proc->latencySamples() == 32);

    std::vector<float> l(64, 0.0f), r(64, 0.0f);
    l[0] = r[0] = 1.0f;
    proc->process(l.data(), r.data(), 64);
    REQUIRE(l[32] == Catch::Approx(1.0f));
    REQUIRE(l[0] == 0.0f);
    REQUIRE(r[32] == Catch::Approx(1.0f));
}

TEST_CASE("juce host: state is applied and captured", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    const InsertSlot slot{Uuid{1, 2}, 0};
    const std::string half = stateWithGain(d, 0.5f);
    auto proc = host.acquire(slot, refOf(d, half), 48000.0, 512);
    REQUIRE(proc);
    std::vector<float> l(64, 0.0f), r(64, 0.0f);
    l[0] = r[0] = 1.0f;
    proc->process(l.data(), r.data(), 64);
    REQUIRE(l[32] == Catch::Approx(0.5f));
    REQUIRE(host.captureState(slot) == half);
}

TEST_CASE("juce host: a captured state is standard base64 the Core accepts", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    const InsertSlot slot{Uuid{1, 9}, 0};
    REQUIRE(host.acquire(slot, refOf(d), 48000.0, 512));
    const std::optional<std::string> captured = host.captureState(slot);
    REQUIRE(captured);
    const std::string& state = *captured;
    REQUIRE_FALSE(state.empty());
    REQUIRE(validBase64(state));  // JUCE's own MemoryBlock encoding is not base64: the Core rejected it
}

TEST_CASE("juce host: the same insert reuses the instance, another state replaces it", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    const InsertSlot slot{Uuid{1, 3}, 0};
    auto a = host.acquire(slot, refOf(d), 48000.0, 512);
    auto b = host.acquire(slot, refOf(d), 48000.0, 512);
    REQUIRE(a == b);
    auto c = host.acquire(slot, refOf(d, stateWithGain(d, 0.25f)), 48000.0, 512);
    REQUIRE(c);
    REQUIRE(c != a);
}

TEST_CASE("juce host: captured state counts as the current state", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    const InsertSlot slot{Uuid{1, 4}, 0};
    auto a = host.acquire(slot, refOf(d), 48000.0, 512);
    const std::optional<std::string> captured = host.captureState(slot);
    REQUIRE(captured);
    REQUIRE_FALSE(captured->empty());
    auto b = host.acquire(slot, refOf(d, *captured), 48000.0, 512);  // the model now holds what was captured
    REQUIRE(a == b);
}

TEST_CASE("juce host: an unknown id gives nothing and a pruned slot is dropped", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    ProcessorRef unknown;
    unknown.processorId = "vst3:ffffffffffffffffffffffffffffffff";
    REQUIRE(host.acquire(InsertSlot{Uuid{1, 5}, 0}, unknown, 48000.0, 512) == nullptr);

    const InsertSlot slot{Uuid{1, 6}, 0};
    auto a = host.acquire(slot, refOf(d), 48000.0, 512);
    host.prune({});
    auto b = host.acquire(slot, refOf(d), 48000.0, 512);
    REQUIRE(a != b);
}

TEST_CASE("juce host: from another thread acquire is non-blocking and the listener fires", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    std::atomic<int> ready{0};
    host.setReadyListener([&](const InsertSlot&) { ++ready; });
    const InsertSlot slot{Uuid{1, 7}, 0};

    std::shared_ptr<audio::IProcessor> first;
    std::thread worker([&] { first = host.acquire(slot, refOf(d), 48000.0, 512); });
    worker.join();  // must return without the message thread running
    REQUIRE(first == nullptr);

    for (int i = 0; i < 200 && ready == 0; ++i) pump(20);
    REQUIRE(ready == 1);

    std::shared_ptr<audio::IProcessor> second;
    std::thread worker2([&] { second = host.acquire(slot, refOf(d), 48000.0, 512); });
    worker2.join();
    REQUIRE(second != nullptr);
    host.setReadyListener({});
}

TEST_CASE("juce host: dropping the listener and the host while a load is queued is safe", "[juce][plugin]") {
    const PluginDescriptor d = testPlugin();
    {
        JucePluginHost host;
        host.setCatalogue({d});
        host.setReadyListener([](const InsertSlot&) {});
        std::thread worker([&] { host.acquire(InsertSlot{Uuid{1, 8}, 0}, refOf(d), 48000.0, 512); });
        worker.join();
    }  // destroyed with the creation still queued on the message thread
    pump(300);  // the queued work must find nothing to do and must not crash
    SUCCEED();
}

TEST_CASE("juce host: the editor opens and closes", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    const InsertSlot slot{Uuid{1, 9}, 0};
    REQUIRE_FALSE(host.openEditor(slot));  // nothing live yet
    auto proc = host.acquire(slot, refOf(d), 48000.0, 512);
    REQUIRE(host.openEditor(slot));
    REQUIRE(host.editorOpen(slot));
    host.closeAllEditors();
    REQUIRE_FALSE(host.editorOpen(slot));
}

TEST_CASE("juce host: an insert that moves to another index keeps its live instance", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    const Uuid track{2, 1};
    auto a = host.acquire(InsertSlot{track, 0}, refOf(d), 48000.0, 512);
    REQUIRE(a);
    // an insert was added in front of it: the same plug-in is now at index 1
    auto b = host.acquire(InsertSlot{track, 1}, refOf(d), 48000.0, 512);
    REQUIRE(b == a);
    host.prune({{InsertSlot{track, 1}, refOf(d)}});
    REQUIRE(host.acquire(InsertSlot{track, 1}, refOf(d), 48000.0, 512) == a);
}

TEST_CASE("juce host: two plug-ins that swap places keep their instances", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    const Uuid track{2, 2};
    const std::string s1 = stateWithGain(d, 0.5f), s2 = stateWithGain(d, 0.25f);
    auto a0 = host.acquire(InsertSlot{track, 0}, refOf(d, s1), 48000.0, 512);
    auto a1 = host.acquire(InsertSlot{track, 1}, refOf(d, s2), 48000.0, 512);
    REQUIRE(a0);
    REQUIRE(a1);
    REQUIRE(a0 != a1);
    auto b0 = host.acquire(InsertSlot{track, 0}, refOf(d, s2), 48000.0, 512);
    auto b1 = host.acquire(InsertSlot{track, 1}, refOf(d, s1), 48000.0, 512);
    REQUIRE(b0 == a1);
    REQUIRE(b1 == a0);
}

TEST_CASE("juce host: dropping the last reference of a plug-in with an open editor is safe", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    const InsertSlot slot{Uuid{2, 3}, 0};
    host.acquire(slot, refOf(d), 48000.0, 512);  // the host's entry is the only reference
    REQUIRE(host.openEditor(slot));
    host.prune({});  // the editor must close before the processor it shows is destroyed
    pump(200);
    REQUIRE_FALSE(host.editorOpen(slot));
}

TEST_CASE("juce host: replacing the state closes the old editor before the old instance goes", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    const InsertSlot slot{Uuid{2, 4}, 0};
    host.acquire(slot, refOf(d), 48000.0, 512);
    REQUIRE(host.openEditor(slot));
    auto next = host.acquire(slot, refOf(d, stateWithGain(d, 0.5f)), 48000.0, 512);  // an undo changed the state
    REQUIRE(next);
    REQUIRE_FALSE(host.editorOpen(slot));
}

TEST_CASE("juce host: an insert moved to another track keeps its live instance", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    const std::string state = stateWithGain(d, 0.5f);
    const InsertSlot from{Uuid{3, 1}, 0}, to{Uuid{3, 2}, 0};
    host.setWanted({{from, refOf(d, state)}});
    auto a = host.acquire(from, refOf(d, state), 48000.0, 512);
    REQUIRE(a);
    host.setWanted({{to, refOf(d, state)}});  // the insert now lives on the other track, the old slot is not asked for any more
    auto b = host.acquire(to, refOf(d, state), 48000.0, 512);
    REQUIRE(b == a);
}

TEST_CASE("juce host: identical inserts on two tracks never share an instance", "[juce][plugin]") {
    JucePluginHost host;
    const PluginDescriptor d = testPlugin();
    host.setCatalogue({d});
    const InsertSlot one{Uuid{4, 1}, 0}, two{Uuid{4, 2}, 0};
    host.setWanted({{one, refOf(d)}, {two, refOf(d)}});  // both are still wanted where they are
    auto a = host.acquire(one, refOf(d), 48000.0, 512);
    auto b = host.acquire(two, refOf(d), 48000.0, 512);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(a != b);
    REQUIRE(host.acquire(one, refOf(d), 48000.0, 512) == a);  // and they stay where they are
}

TEST_CASE("juce host: a VST3 instrument sounds the note events at their offsets", "[juce][plugin][instrument]") {
    JucePluginHost host;
    const PluginDescriptor d = testSynth();
    host.setCatalogue({d});
    const InsertSlot slot{Uuid{3, 1}, kInstrumentSlot};
    auto inst = host.acquireInstrument(slot, refOf(d), 48000.0, 512);
    REQUIRE(inst);
    std::vector<float> l(256, 1.0f), r(256, 1.0f);
    const audio::MidiEvent on{100, 0x90, 69, 127};
    inst->render(l.data(), r.data(), 256, &on, 1);
    for (int i = 0; i < 100; ++i) REQUIRE(l[static_cast<size_t>(i)] == 0.0f);                    // silence before the note
    float peak = 0.0f;
    for (int i = 100; i < 256; ++i) peak = std::max(peak, std::abs(l[static_cast<size_t>(i)]));
    REQUIRE(peak > 0.05f);                                                                         // then the tone, on both channels
    REQUIRE(r[200] == l[200]);

    const audio::MidiEvent off{20, 0x80, 69, 0};
    inst->render(l.data(), r.data(), 256, &off, 1);
    REQUIRE(std::abs(l[5]) > 0.0f);                                                                // still sounding until the note-off
    for (int i = 21; i < 256; ++i) REQUIRE(l[static_cast<size_t>(i)] == 0.0f);                    // silent after it
    const auto state = host.captureState(slot);                                                    // its state is saved like an insert's
    REQUIRE(state);
    REQUIRE(validBase64(*state));
}

TEST_CASE("juce host: the instrument slot keeps its instance while id and state stay", "[juce][plugin][instrument]") {
    JucePluginHost host;
    const PluginDescriptor synth = testSynth();
    host.setCatalogue({synth});
    const Uuid track{3, 2};
    auto inst = host.acquireInstrument(InsertSlot{track, kInstrumentSlot}, refOf(synth), 48000.0, 512);
    REQUIRE(inst);
    REQUIRE(host.acquireInstrument(InsertSlot{track, kInstrumentSlot}, refOf(synth), 48000.0, 512) == inst);  // reused while id and state stay
}

TEST_CASE("juce host: an offline render (the bounce) plays a VST3 instrument", "[juce][plugin][instrument][bounce]") {
    JucePluginHost host;
    const PluginDescriptor d = testSynth();
    host.setCatalogue({d});
    std::mt19937_64 rng(2);
    Project project;
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Instrument;
    t.name = "Synth";
    t.instrument = refOf(d);
    Region region;
    region.id = Uuid::random(rng);
    region.timeBase = TimeBase::Musical;
    region.start = 0;
    region.length = 4 * kPPQ;
    region.notes.push_back(MidiNote{kPPQ, kPPQ, 69, 127, false});   // an A from the second beat for one beat
    t.regions.push_back(region);
    REQUIRE(makeAddTrack(t)->apply(project).ok());

    MediaStore media(std::filesystem::temp_directory_path(), false);
    RenderOptions options;
    options.plugins = &host;
    options.tailSeconds = 0.0;
    const RenderResult result = renderOffline(project, media, options);
    const std::int64_t beat = 24000;                                  // 120 bpm at 48 kHz
    REQUIRE(result.frames >= 4 * beat);
    float before = 0.0f, during = 0.0f, after = 0.0f;
    for (std::int64_t i = 0; i < beat - 64; ++i) before = std::max(before, std::abs(result.interleaved[static_cast<std::size_t>(i * 2)]));
    for (std::int64_t i = beat + 64; i < 2 * beat - 64; ++i) during = std::max(during, std::abs(result.interleaved[static_cast<std::size_t>(i * 2)]));
    for (std::int64_t i = 2 * beat + 64; i < 3 * beat; ++i) after = std::max(after, std::abs(result.interleaved[static_cast<std::size_t>(i * 2)]));
    REQUIRE(before == 0.0f);
    REQUIRE(during > 0.05f);
    REQUIRE(after == 0.0f);
}
