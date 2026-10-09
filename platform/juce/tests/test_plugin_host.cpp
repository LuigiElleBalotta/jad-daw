#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <atomic>
#include <chrono>
#include <thread>

#include <windows.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include "juce_plugin_host.h"

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
    return block.toBase64Encoding().toStdString();
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
    const std::string captured = host.captureState(slot);
    REQUIRE_FALSE(captured.empty());
    auto b = host.acquire(slot, refOf(d, captured), 48000.0, 512);  // the model now holds what was captured
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
