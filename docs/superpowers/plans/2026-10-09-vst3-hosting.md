# VST3 hosting as insert effects: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load real VST3 plug-ins as insert effects on tracks and buses, with scan/cache/rescan, native editor window, undoable state, delay compensation and offline render support.

**Architecture:** Core gets a JUCE-free `IPluginHost` interface, `MissingPluginProcessor`/`SharedProcessor`, per-edge PDC and a `set_insert_state` command. `platform/juce` implements the host (non-blocking `acquire`, instances created on the JUCE message thread), a scanner child process with a JSON cache, and the editor window. The Qt UI gets a `PluginsModel`, a plug-in submenu on the insert slot, double-click to open the editor, and a Plug-in Manager dialog.

**Tech Stack:** C++20, Catch2 (Core and platform tests), JUCE 8.0.4 (`juce_audio_processors`, `juce_gui_basics`, VST3 hosting), Qt 6.8 Quick, QtTest and Qt Quick Test.

**Spec:** `docs/superpowers/specs/2026-10-09-vst3-hosting-design.md` (amended in commit `6299071`: non-blocking `acquire`, per-edge PDC, ids `vst3:<32 hex>`).

## Global Constraints

- Core (`core/`) never includes JUCE or Qt. `IPluginHost` is the only door to plug-ins.
- The audio thread never allocates, locks or waits. `DelayLine` and plug-in processors are created on the project thread.
- The project thread never waits for the JUCE message thread; the message thread (Qt main thread in the app) may wait for the project thread.
- Plug-in ids are `vst3:` plus 32 lowercase hex digits. `params` of a plug-in insert is empty. `state` is base64, at most 22369624 characters (16 MiB decoded). `label` is at most 128 bytes.
- Old `.lpc` files must load unchanged; `label` is written only when not empty.
- Windows first. Existing `build`, `build-core`, `build-ui` directories are not touched; plug-in work uses `build-plugin` (CLI + JUCE) and `build-ui` (UI).
- The CI is ignored for now (user decision). Commit messages end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- All JUCE code of the platform goes into the one existing static library target `lpc_juce_device` (a second library that also links JUCE modules would define the module sources twice).

## Review Focus

- A project saved on a machine with the plug-in, opened on one without it: the slot says "missing", audio passes through, saving again keeps the id and state byte for byte (Task 5, Task 13 tests).
- A broken or hanging `.vst3` file in a scanned folder must not stop the scan or the app and must be listed as failed with a reason (Task 9 test).
- Undo of an editor session: one undo step, and it must restore the previous sound without a click of silence longer than the reload (Task 4, Task 7 tests).
- Two tracks feeding one bus where only one has a latent plug-in must stay time aligned, including through sends (Task 6 tests).
- Opening the same project twice or closing it while a plug-in is still loading must not crash or leak a callback (Task 7 and Task 8 tests).

---

## File structure

Create:
- `core/include/lpc/plugin_host.h`, `core/src/plugin_host.cpp`: `PluginDescriptor`, `InsertSlot`, `IPluginHost`, `makeInsert`.
- `core/include/lpc/plugin_catalogue.h`, `core/src/plugin_catalogue.cpp`: cache data model, `filesToScan`, `appConfigDir`.
- `tests/fake_plugin_host.h`: `FakePluginHost`.
- `tests/test_plugin_refs.cpp`, `tests/test_insert_state.cpp`, `tests/test_plugin_insert.cpp`, `tests/test_pdc.cpp`, `tests/test_plugin_host_integration.cpp`, `tests/test_plugin_catalogue.cpp`.
- `tools/test-plugin/CMakeLists.txt`, `tools/test-plugin/TestGainPlugin.cpp`: the test VST3.
- `tools/plugin-spike/CMakeLists.txt`, `tools/plugin-spike/main.cpp`: throwaway spike (deleted in Task 2).
- `tools/plugin-scanner/CMakeLists.txt`, `tools/plugin-scanner/main.cpp`: `lpc-plugin-scanner`.
- `platform/juce/plugin_processor.h/.cpp`, `platform/juce/juce_plugin_host.h/.cpp`, `platform/juce/editor_window.h/.cpp`, `platform/juce/plugin_scanner.h/.cpp`.
- `platform/juce/tests/CMakeLists.txt`, `platform/juce/tests/test_plugin_host.cpp`, `platform/juce/tests/test_scanner.cpp`.
- `ui/bridge/plugins_model.h/.cpp`, `ui/qml/PluginManager.qml`, `ui/tests/tst_plugins.cpp`.

Modify: `core/include/lpc/model.h`, `model_json.h`, `model_json.cpp`, `processor_ids.h`, `validation.h/.cpp`, `commands.h`, `commands.cpp`, `commands_strip.cpp`, `audio/processors.h/.cpp`, `audio/render_graph.h/.cpp`, `graph_builder.h/.cpp`, `offline_render.h/.cpp`, `project_host.h/.cpp`; `CMakeLists.txt`, `platform/juce/CMakeLists.txt`, `tools/lpc-cli/*`, `ui/CMakeLists.txt`, `ui/bridge/project_controller.h/.cpp`, `snapshot.h/.cpp`, `row_maps.h`, `ui/qml/ChannelStrip.qml`, `StripSlot.qml`, `ProjectStrip.qml`, `Main.qml`, `ui/actions/actions.json`, `ui/tests/CMakeLists.txt`, `ui/tests/tst_channelstrip.qml`, `README.md`, `THIRD_PARTY.md`.

Build commands used below (run from the repo root, Git Bash):

```bash
# Core tests (existing build dir, no JUCE needed for Tasks 3-7)
cmake -S . -B build-core -G "Visual Studio 17 2022" -A x64 -DLPC_BUILD_CLI=OFF -DLPC_WITH_JUCE=OFF
cmake --build build-core --config Debug --target lpc_tests
ctest --test-dir build-core -C Debug --output-on-failure -R "<filter>"
# JUCE + plug-in tests
cmake -S . -B build-plugin -G "Visual Studio 17 2022" -A x64 -DLPC_BUILD_PLUGIN_TESTS=ON
cmake --build build-plugin --config Debug
ctest --test-dir build-plugin -C Debug --output-on-failure
# UI
cmake -S . -B build-ui -A x64 -DLPC_BUILD_UI=ON -DLPC_BUILD_PLUGIN_TESTS=ON "-DCMAKE_PREFIX_PATH=$(pwd)/.qt/6.8.3/msvc2022_64"
cmake --build build-ui --config Debug
ctest --test-dir build-ui -C Debug --output-on-failure
```

---

### Task 1: Test VST3 and the build switch

**Files:**
- Create: `tools/test-plugin/CMakeLists.txt`, `tools/test-plugin/TestGainPlugin.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: CMake target `lpc_test_gain`; the bundle `LPC Test Gain.vst3` under `${CMAKE_BINARY_DIR}/tools/test-plugin/lpc_test_gain_artefacts/<config>/VST3/`. Plug-in behaviour: stereo in/out only, one parameter `gain` (0..2, default 1), reports a latency of exactly 32 samples (output is the input delayed by 32 samples, times gain), saves state as 4 bytes (a little-endian `float` gain), has a generic editor. Option `LPC_BUILD_PLUGIN_TESTS` (default OFF).

- [ ] **Step 1: Add the option and subdirectory to the root `CMakeLists.txt`**

After the existing `option(LPC_WITH_JUCE ...)` line add:

```cmake
option(LPC_BUILD_PLUGIN_TESTS "Build the test VST3, the scanner tests and the platform plug-in tests (needs JUCE)" OFF)
```

Inside the existing `if(LPC_WITH_JUCE AND (LPC_BUILD_CLI OR LPC_BUILD_UI))` block, after `add_subdirectory(platform/juce)`, add:

```cmake
    if(LPC_BUILD_PLUGIN_TESTS)
        add_subdirectory(tools/test-plugin)
    endif()
```

JUCE is only fetched when the CLI or the UI is built, so `LPC_BUILD_PLUGIN_TESTS` needs the default `LPC_BUILD_CLI=ON`.

- [ ] **Step 2: Write `tools/test-plugin/CMakeLists.txt`**

```cmake
juce_add_plugin(lpc_test_gain
    PRODUCT_NAME "LPC Test Gain"
    COMPANY_NAME "JAD"
    PLUGIN_MANUFACTURER_CODE JadD
    PLUGIN_CODE TgnA
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(lpc_test_gain PRIVATE TestGainPlugin.cpp)
target_compile_definitions(lpc_test_gain PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(lpc_test_gain PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)
set(LPC_TEST_VST3_DIR "${CMAKE_CURRENT_BINARY_DIR}/lpc_test_gain_artefacts/$<CONFIG>/VST3" PARENT_SCOPE)
```

- [ ] **Step 3: Write `tools/test-plugin/TestGainPlugin.cpp`**

```cpp
#include <array>
#include <cstring>

#include <juce_audio_processors/juce_audio_processors.h>

namespace {

constexpr int kLatency = 32;

class TestGainProcessor final : public juce::AudioProcessor {
public:
    TestGainProcessor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        addParameter(gain_ = new juce::AudioParameterFloat(juce::ParameterID{"gain", 1}, "Gain",
                                                           juce::NormalisableRange<float>(0.0f, 2.0f), 1.0f));
        setLatencySamples(kLatency);
    }

    const juce::String getName() const override { return "LPC Test Gain"; }
    void prepareToPlay(double, int) override { releaseResources(); }
    void releaseResources() override {
        for (auto& d : delay_) d.fill(0.0f);
        pos_ = 0;
    }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
               layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        const float g = gain_->get();
        const int n = buffer.getNumSamples();
        for (int ch = 0; ch < 2 && ch < buffer.getNumChannels(); ++ch) {
            float* data = buffer.getWritePointer(ch);
            int p = pos_;
            for (int i = 0; i < n; ++i) {
                const float in = data[i];
                data[i] = delay_[static_cast<size_t>(ch)][static_cast<size_t>(p)] * g;
                delay_[static_cast<size_t>(ch)][static_cast<size_t>(p)] = in;
                p = (p + 1) % kLatency;
            }
        }
        pos_ = (pos_ + n) % kLatency;
    }

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override {
        const float g = gain_->get();
        dest.setSize(sizeof(float));
        std::memcpy(dest.getData(), &g, sizeof(float));
    }
    void setStateInformation(const void* data, int size) override {
        if (size != static_cast<int>(sizeof(float))) return;
        float g = 1.0f;
        std::memcpy(&g, data, sizeof(float));
        *gain_ = g;
    }

private:
    juce::AudioParameterFloat* gain_ = nullptr;
    std::array<std::array<float, kLatency>, 2> delay_{};
    int pos_ = 0;
};

}  // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestGainProcessor(); }
```

- [ ] **Step 4: Build it**

Run: `cmake -S . -B build-plugin -G "Visual Studio 17 2022" -A x64 -DLPC_BUILD_PLUGIN_TESTS=ON && cmake --build build-plugin --config Debug --target lpc_test_gain`
Expected: build succeeds (the first run downloads JUCE, several minutes).

- [ ] **Step 5: Check the bundle exists**

Run: `ls "build-plugin/tools/test-plugin/lpc_test_gain_artefacts/Debug/VST3/LPC Test Gain.vst3/Contents"`
Expected: a `x86_64-win` folder containing `LPC Test Gain.vst3`.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tools/test-plugin
git commit -m "test: a tiny VST3 (gain, 32 samples latency, state) behind LPC_BUILD_PLUGIN_TESTS"
```

---

### Task 2: Spike, JUCE message loop inside Qt (gate for the rest)

**Files:**
- Create (throwaway): `tools/plugin-spike/CMakeLists.txt`, `tools/plugin-spike/main.cpp`
- Modify (throwaway): `ui/CMakeLists.txt`
- Modify: `docs/superpowers/specs/2026-10-09-vst3-hosting-design.md` (record the result in section 9)

**Interfaces:**
- Consumes: the test VST3 of Task 1.
- Produces: a go/no-go answer. **If the result is no-go, stop the plan and report to the user; do not continue.**

- [ ] **Step 1: Write `tools/plugin-spike/CMakeLists.txt`**

```cmake
add_executable(plugin-spike main.cpp)
target_compile_definitions(plugin-spike PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(plugin-spike PRIVATE
    Qt6::Gui juce::juce_audio_processors juce::juce_gui_basics juce::juce_events juce::juce_recommended_config_flags)
if(WIN32)
    jad_deploy_qt(plugin-spike)
endif()
```

(`jad_deploy_qt` is the existing helper used in `ui/tests/CMakeLists.txt`; if it is defined in `ui/CMakeLists.txt` after the point where the spike is added, add the spike at the end of `ui/CMakeLists.txt`.)

- [ ] **Step 2: Add it to the UI build**

Append to `ui/CMakeLists.txt`:

```cmake
if(LPC_BUILD_PLUGIN_TESTS AND TARGET juce::juce_audio_processors AND EXISTS ${CMAKE_SOURCE_DIR}/tools/plugin-spike)
    add_subdirectory(${CMAKE_SOURCE_DIR}/tools/plugin-spike ${CMAKE_BINARY_DIR}/tools/plugin-spike)
endif()
```

- [ ] **Step 3: Write `tools/plugin-spike/main.cpp`**

```cpp
#include <QGuiApplication>
#include <QTimer>
#include <QWindow>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

class EditorWindow final : public juce::DocumentWindow {
public:
    explicit EditorWindow(juce::AudioProcessorEditor* editor)
        : DocumentWindow("spike editor", juce::Colours::darkgrey, DocumentWindow::closeButton) {
        setUsingNativeTitleBar(true);
        setContentOwned(editor, true);
        setResizable(editor->isResizable(), false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    void closeButtonPressed() override { setVisible(false); }
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc < 2) {
        qWarning("usage: plugin-spike <file.vst3>");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::AudioPluginFormatManager formats;
    formats.addFormat(new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> found;
    formats.getFormat(0)->findAllTypesForFile(found, argv[1]);
    if (found.isEmpty()) {
        qWarning("no plug-in found in the file");
        return 3;
    }
    juce::String error;
    std::unique_ptr<juce::AudioPluginInstance> instance = formats.createPluginInstance(*found[0], 48000.0, 512, error);
    if (!instance) {
        qWarning("load failed: %s", error.toRawUTF8());
        return 4;
    }
    std::unique_ptr<EditorWindow> window(new EditorWindow(instance->createEditor()));

    QWindow qtWindow;
    qtWindow.resize(320, 80);
    qtWindow.show();
    int ticks = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] { qtWindow.setTitle(QString("Qt alive: %1").arg(++ticks)); });
    timer.start(100);
    return app.exec();
}
```

- [ ] **Step 4: Build and run**

Run:
```bash
cmake -S . -B build-ui -A x64 -DLPC_BUILD_UI=ON -DLPC_BUILD_PLUGIN_TESTS=ON "-DCMAKE_PREFIX_PATH=$(pwd)/.qt/6.8.3/msvc2022_64"
cmake --build build-ui --config Debug --target plugin-spike lpc_test_gain
build-ui/tools/plugin-spike/Debug/plugin-spike.exe "build-ui/tools/test-plugin/lpc_test_gain_artefacts/Debug/VST3/LPC Test Gain.vst3"
```
Expected: two windows appear, the "Qt alive" window's title counter keeps counting and the plug-in window shows a Gain slider.

- [ ] **Step 5: Judge it by hand (this is the gate)**

All of these must hold: the Qt title keeps counting while you drag the plug-in slider; the plug-in slider follows the mouse smoothly; closing the plug-in window with its X does not crash; moving, resizing and covering/uncovering either window repaints both. Run the spike for one minute while clicking in both windows.

If any fails: stop, write the symptom into section 9 of the spec, and tell the user. The fallback in the spec (JUCE on its own message thread) becomes a new design question; do not improvise it.

- [ ] **Step 6: Record the result and remove the spike**

Edit the first bullet of section 9 of the spec: append `Result of the spike (2026-10-09 or later): works, JUCE and Qt share the Win32 message loop, no change needed.` (or the failure text).

```bash
git rm -r tools/plugin-spike
git checkout ui/CMakeLists.txt
git add docs/superpowers/specs/2026-10-09-vst3-hosting-design.md
git commit -m "docs: result of the Qt and JUCE message loop spike"
```

---

### Task 3: Plug-in ids, `label` and validation (Core)

**Files:**
- Modify: `core/include/lpc/model.h`, `core/include/lpc/model_json.h`, `core/src/model_json.cpp`, `core/include/lpc/processor_ids.h`, `core/include/lpc/validation.h`, `core/src/validation.cpp`
- Test: `tests/test_plugin_refs.cpp`

**Interfaces:**
- Produces: `ProcessorRef::label` (`std::string`, default empty); `bool isVst3Id(std::string_view)` in `processor_ids.h`; `inline constexpr std::size_t kMaxPluginStateChars = 22369624;` and `bool validBase64(const std::string&)` in `validation.h`; `checkInsert` accepts valid `vst3:` refs.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_plugin_refs.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <random>
#include <nlohmann/json.hpp>
#include "lpc/commands.h"
#include "lpc/model_json.h"
#include "lpc/processor_ids.h"
#include "lpc/validation.h"

using namespace lpc;

namespace {
const char* kId = "vst3:00112233445566778899aabbccddeeff";

ProcessorRef plugin(const std::string& state = "") {
    ProcessorRef r;
    r.processorId = kId;
    r.state = state;
    r.label = "Test Reverb";
    return r;
}
}  // namespace

TEST_CASE("plugin refs: id format", "[plugin]") {
    REQUIRE(isVst3Id(kId));
    REQUIRE_FALSE(isVst3Id("vst3:00112233445566778899AABBCCDDEEFF"));  // upper case
    REQUIRE_FALSE(isVst3Id("vst3:0011"));
    REQUIRE_FALSE(isVst3Id("vst3:00112233445566778899aabbccddeeffaa"));
    REQUIRE_FALSE(isVst3Id("builtin.gain"));
    REQUIRE_FALSE(isVst3Id(""));
}

TEST_CASE("plugin refs: base64 check", "[plugin]") {
    REQUIRE(validBase64(""));
    REQUIRE(validBase64("AAAA"));
    REQUIRE(validBase64("AAA="));
    REQUIRE(validBase64("AA=="));
    REQUIRE_FALSE(validBase64("AAA"));      // length not a multiple of 4
    REQUIRE_FALSE(validBase64("AA=A"));     // padding in the middle
    REQUIRE_FALSE(validBase64("AAA*"));
}

TEST_CASE("plugin refs: label round trip and old files", "[plugin][json]") {
    const nlohmann::json j = plugin("AAAA");
    REQUIRE(j.at("label") == "Test Reverb");
    REQUIRE(j.at("processorId") == kId);
    REQUIRE(j.get<ProcessorRef>() == plugin("AAAA"));

    ProcessorRef gain;
    gain.processorId = "builtin.gain";
    const nlohmann::json g = gain;
    REQUIRE_FALSE(g.contains("label"));  // not written when empty: old files stay byte-identical

    const nlohmann::json old = {{"processorId", "builtin.gain"}, {"params", {{"gainDb", 3.0}}}, {"state", ""}};
    const ProcessorRef loaded = old.get<ProcessorRef>();
    REQUIRE(loaded.label.empty());
    REQUIRE(loaded.params.at("gainDb") == 3.0);
}

TEST_CASE("plugin refs: checkInsert", "[plugin]") {
    REQUIRE_FALSE(checkInsert(plugin("AAAA")).has_value());
    REQUIRE_FALSE(checkInsert(plugin("")).has_value());

    ProcessorRef withParams = plugin();
    withParams.params["x"] = 1.0;
    REQUIRE(checkInsert(withParams).has_value());

    REQUIRE(checkInsert(plugin("not base64!")).has_value());

    ProcessorRef badId = plugin();
    badId.processorId = "vst3:XYZ";
    REQUIRE(checkInsert(badId).has_value());

    ProcessorRef longLabel = plugin();
    longLabel.label = std::string(129, 'a');
    REQUIRE(checkInsert(longLabel).has_value());

    ProcessorRef huge = plugin(std::string(kMaxPluginStateChars + 4, 'A'));
    REQUIRE(checkInsert(huge).has_value());

    // built-ins are unchanged
    ProcessorRef gain;
    gain.processorId = "builtin.gain";
    gain.params["gainDb"] = 6.0;
    REQUIRE_FALSE(checkInsert(gain).has_value());
}

TEST_CASE("plugin refs: a plug-in insert is accepted by add_insert", "[plugin]") {
    std::mt19937_64 rng(5);
    Project p;
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Audio;
    t.name = "A";
    REQUIRE(makeAddTrack(t)->apply(p).ok());
    REQUIRE(makeAddInsert(t.id, plugin("AAAA"))->apply(p).ok());
    REQUIRE(p.findTrack(t.id)->strip.inserts.size() == 1);
    REQUIRE(checkProject(p) == std::nullopt);
}
```

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake --build build-core --config Debug --target lpc_tests` 
Expected: compile errors (`isVst3Id`, `validBase64`, `label` not declared).

- [ ] **Step 3: Implement**

`core/include/lpc/model.h`, in `ProcessorRef`, add after `state`:

```cpp
    std::string label;  // display name of a plug-in (informational; empty for built-ins)
```

`core/include/lpc/model_json.h`: replace the line `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProcessorRef, processorId, params, state)` by

```cpp
void to_json(nlohmann::json& j, const ProcessorRef& r);
void from_json(const nlohmann::json& j, ProcessorRef& r);  // `label` is optional
```

`core/src/model_json.cpp` (inside `namespace lpc`):

```cpp
void to_json(nlohmann::json& j, const ProcessorRef& r) {
    j = {{"processorId", r.processorId}, {"params", r.params}, {"state", r.state}};
    if (!r.label.empty()) j["label"] = r.label;
}

void from_json(const nlohmann::json& j, ProcessorRef& r) {
    j.at("processorId").get_to(r.processorId);
    j.at("params").get_to(r.params);
    j.at("state").get_to(r.state);
    r.label = j.value("label", std::string());
}
```

`core/include/lpc/processor_ids.h`: add `#include <string_view>` is already there; add inside the namespace:

```cpp
// A hosted plug-in: "vst3:" and 32 lowercase hex digits.
inline bool isVst3Id(std::string_view id) {
    constexpr std::string_view prefix = "vst3:";
    if (id.size() != prefix.size() + 32 || id.substr(0, prefix.size()) != prefix) return false;
    for (const char c : id.substr(prefix.size()))
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
```

`core/include/lpc/validation.h`: add after `kMaxProjectTracks`:

```cpp
inline constexpr std::size_t kMaxPluginStateChars = 22369624;  // base64 of 16 MiB
inline constexpr std::size_t kMaxInsertLabelBytes = 128;
bool validBase64(const std::string& s);  // "" or standard base64 with padding
```

`core/src/validation.cpp`: add `#include "lpc/processor_ids.h"` (if not present) and, before `checkInsert`:

```cpp
bool validBase64(const std::string& s) {
    if (s.size() % 4 != 0) return false;
    std::size_t padding = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '=') {
            ++padding;
            if (i < s.size() - 2) return false;  // padding only in the last two places
            continue;
        }
        if (padding > 0) return false;
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/';
        if (!ok) return false;
    }
    return padding <= 2;
}
```

and at the top of `checkInsert`:

```cpp
MaybeError checkInsert(const ProcessorRef& insert) {
    if (insert.label.size() > kMaxInsertLabelBytes) return CommandError{"bad_value", "insert label is too long"};
    if (isVst3Id(insert.processorId)) {
        if (!insert.params.empty()) return CommandError{"bad_value", "plug-in inserts take no parameters"};
        if (insert.state.size() > kMaxPluginStateChars || !validBase64(insert.state))
            return CommandError{"bad_value", "plug-in state must be base64 of at most 16 MiB"};
        return std::nullopt;
    }
    if (insert.processorId.rfind("vst3:", 0) == 0) return CommandError{"bad_value", "malformed plug-in id: " + insert.processorId};
    // ... the existing built-in checks stay as they are
```

(The existing first line `if (!isKnownEffect(...))` remains right after.)

- [ ] **Step 4: Run all Core tests**

Run: `cmake --build build-core --config Debug --target lpc_tests && ctest --test-dir build-core -C Debug --output-on-failure`
Expected: all pass, including the golden JSON tests (labels are not written when empty).

- [ ] **Step 5: Commit**

```bash
git add core tests/test_plugin_refs.cpp
git commit -m "feat(core): plug-in insert ids, label and validation"
```

---

### Task 4: `set_insert_state` command (Core)

**Files:**
- Modify: `core/include/lpc/commands.h`, `core/src/commands_strip.cpp`, `core/src/commands.cpp`
- Test: `tests/test_insert_state.cpp`

**Interfaces:**
- Consumes: Task 3 (`isVst3Id`, `checkInsert`).
- Produces: `CommandPtr makeSetInsertState(Uuid trackId, int index, std::string state)`; JSON `{"type":"set_insert_state","trackId":...,"index":...,"state":...}`; errors `not_found`, `bad_index`, `bad_value`.

- [ ] **Step 1: Write the failing tests**

`tests/test_insert_state.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <random>
#include "lpc/commands.h"
#include "lpc/undo_stack.h"

using namespace lpc;

namespace {
const char* kId = "vst3:00112233445566778899aabbccddeeff";

struct Fixture {
    std::mt19937_64 rng{9};
    Project p;
    Uuid trackId;
    Fixture() {
        Track t;
        t.id = Uuid::random(rng);
        t.kind = TrackKind::Audio;
        t.name = "A";
        trackId = t.id;
        REQUIRE(makeAddTrack(t)->apply(p).ok());
        ProcessorRef gain;
        gain.processorId = "builtin.gain";
        REQUIRE(makeAddInsert(trackId, gain)->apply(p).ok());
        ProcessorRef plug;
        plug.processorId = kId;
        plug.state = "AAAA";
        plug.label = "Test";
        REQUIRE(makeAddInsert(trackId, plug)->apply(p).ok());
    }
    const ProcessorRef& insert(int i) const { return p.findTrack(trackId)->strip.inserts[static_cast<std::size_t>(i)]; }
};
}  // namespace

TEST_CASE("set_insert_state: replaces the state and keeps id and label", "[plugin][commands]") {
    Fixture f;
    REQUIRE(makeSetInsertState(f.trackId, 1, "BBBB")->apply(f.p).ok());
    REQUIRE(f.insert(1).state == "BBBB");
    REQUIRE(f.insert(1).processorId == kId);
    REQUIRE(f.insert(1).label == "Test");
}

TEST_CASE("set_insert_state: undo and redo", "[plugin][commands]") {
    Fixture f;
    UndoStack undo;
    REQUIRE_FALSE(undo.execute(f.p, makeSetInsertState(f.trackId, 1, "BBBB")).has_value());
    REQUIRE(f.insert(1).state == "BBBB");
    REQUIRE_FALSE(undo.undo(f.p).has_value());
    REQUIRE(f.insert(1).state == "AAAA");
    REQUIRE_FALSE(undo.redo(f.p).has_value());
    REQUIRE(f.insert(1).state == "BBBB");
}

TEST_CASE("set_insert_state: errors", "[plugin][commands]") {
    Fixture f;
    REQUIRE(makeSetInsertState(Uuid::random(f.rng), 1, "AAAA")->apply(f.p).error.code == "not_found");
    REQUIRE(makeSetInsertState(f.trackId, 5, "AAAA")->apply(f.p).error.code == "bad_index");
    REQUIRE(makeSetInsertState(f.trackId, -1, "AAAA")->apply(f.p).error.code == "bad_index");
    REQUIRE(makeSetInsertState(f.trackId, 0, "AAAA")->apply(f.p).error.code == "bad_value");   // a built-in has no state
    REQUIRE(makeSetInsertState(f.trackId, 1, "***")->apply(f.p).error.code == "bad_value");
    REQUIRE(f.insert(1).state == "AAAA");
}

TEST_CASE("set_insert_state: JSON round trip", "[plugin][commands]") {
    Fixture f;
    const nlohmann::json j = makeSetInsertState(f.trackId, 1, "BBBB")->toJson();
    REQUIRE(j.at("type") == "set_insert_state");
    REQUIRE(commandFromJson(j)->apply(f.p).ok());
    REQUIRE(f.insert(1).state == "BBBB");
}
```

If `ApplyResult` does not expose `.error.code` the way used here, open `core/include/lpc/command.h`, read the `ApplyResult` struct and adapt the three `.error.code` accessors to what it provides (the existing tests in `tests/test_commands_strip_edit.cpp` show the idiom).

- [ ] **Step 2: Run the tests to see them fail**

Run: `cmake --build build-core --config Debug --target lpc_tests`
Expected: compile error, `makeSetInsertState` not declared.

- [ ] **Step 3: Implement**

`core/include/lpc/commands.h`, after `makeSetInsertParam`:

```cpp
CommandPtr makeSetInsertState(Uuid trackId, int index, std::string state);  // plug-in inserts only; base64 state
```

`core/src/commands_strip.cpp`, in the anonymous namespace before its closing brace (after `SetInsertParamCmd`):

```cpp
class SetInsertStateCmd final : public Command {
public:
    SetInsertStateCmd(Uuid trackId, int index, std::string state) : trackId_(trackId), index_(index), state_(std::move(state)) {}
    std::string type() const override { return "set_insert_state"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"index", index_}, {"state", state_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        auto& chain = t->strip.inserts;
        if (index_ < 0 || index_ >= static_cast<int>(chain.size())) return fail("bad_index", "insert index out of range");
        ProcessorRef next = chain[static_cast<std::size_t>(index_)];
        if (!isVst3Id(next.processorId)) return fail("bad_value", "only plug-in inserts have a state");
        std::string previous = std::move(next.state);
        next.state = state_;
        if (auto e = checkInsert(next)) return fail(*e);
        chain[static_cast<std::size_t>(index_)] = std::move(next);
        return success(makeSetInsertState(trackId_, index_, std::move(previous)));
    }

private:
    Uuid trackId_;
    int index_;
    std::string state_;
};
```

Add `#include "lpc/processor_ids.h"` at the top if missing. After the other `make...` factory definitions at the bottom of the file add:

```cpp
CommandPtr makeSetInsertState(Uuid trackId, int index, std::string state) {
    return std::make_unique<SetInsertStateCmd>(trackId, index, std::move(state));
}
```

`core/src/commands.cpp`, in `commandFromJson`, after the `set_insert_param` block:

```cpp
        if (type == "set_insert_state")
            return makeSetInsertState(j.at("trackId").get<Uuid>(), j.at("index").get<int>(), j.at("state").get<std::string>());
```

- [ ] **Step 4: Run all Core tests**

Run: `cmake --build build-core --config Debug --target lpc_tests && ctest --test-dir build-core -C Debug --output-on-failure`
Expected: all pass. If a test enumerates every command type (for example `random_commands.h` property tests), it is unaffected because it does not know the new type.

- [ ] **Step 5: Commit**

```bash
git add core tests/test_insert_state.cpp
git commit -m "feat(core): set_insert_state command"
```

---

### Task 5: `IPluginHost`, missing and shared processors, `makeInsert` (Core)

**Files:**
- Create: `core/include/lpc/plugin_host.h`, `core/src/plugin_host.cpp`, `tests/fake_plugin_host.h`
- Modify: `core/include/lpc/audio/processors.h`, `core/src/audio/processors.cpp`, `core/include/lpc/graph_builder.h`, `core/src/graph_builder.cpp`
- Test: `tests/test_plugin_insert.cpp`

**Interfaces:**
- Consumes: Task 3.
- Produces (exact):
  - `audio::IProcessor` gains `virtual void prepare(double sampleRate, int maxBlock) {}` and `virtual int latencySamples() const { return 0; }`.
  - `audio::MissingPluginProcessor`, `audio::SharedProcessor(std::shared_ptr<IProcessor>)`.
  - `struct PluginDescriptor { std::string id, name, vendor, version, path, native; }`, `struct InsertSlot { Uuid track; int index = 0; }` (with `operator==`), `class IPluginHost` exactly as in spec section 3.2.
  - `std::unique_ptr<audio::IProcessor> makeInsert(const ProcessorRef&, IPluginHost*, const InsertSlot&, double sampleRate, int maxBlock)`: built-ins via `makeEffect`; `vst3:` via the host, `MissingPluginProcessor` when the host returns null or is null; `nullptr` for any other unknown id.
  - `buildConfig(project, track, media, IPluginHost* plugins = nullptr)`, `buildNode(..., IPluginHost* plugins = nullptr)`, `initialMessages(project, media, IPluginHost* plugins = nullptr)`, `diffToMessages(before, after, media, IPluginHost* plugins = nullptr)`.
  - `tests/fake_plugin_host.h`: `lpc::test::FakePluginHost` with public `std::map<std::string, FakeSpec> known`, `bool deferLoads`, `int created`, `void finishLoads()`; `FakeSpec{int latency = 0; float gain = 1.0f;}`; its processor really delays by `latency` frames and multiplies by `gain`.

- [ ] **Step 1: Write the fake host (test helper)**

`tests/fake_plugin_host.h`:

```cpp
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "lpc/plugin_host.h"

namespace lpc::test {

struct FakeSpec {
    int latency = 0;
    float gain = 1.0f;
};

// A plug-in that delays by `latency` frames and multiplies by `gain`.
class FakeProcessor final : public audio::IProcessor {
public:
    FakeProcessor(FakeSpec spec, std::string state)
        : spec_(spec), state_(std::move(state)), l_(static_cast<std::size_t>(spec.latency), 0.0f), r_(l_) {}
    void process(float* l, float* r, int frames) noexcept override {
        for (int i = 0; i < frames; ++i) {
            if (spec_.latency > 0) {
                const std::size_t p = pos_;
                const float outL = l_[p], outR = r_[p];
                l_[p] = l[i];
                r_[p] = r[i];
                pos_ = (pos_ + 1) % l_.size();
                l[i] = outL * spec_.gain;
                r[i] = outR * spec_.gain;
            } else {
                l[i] *= spec_.gain;
                r[i] *= spec_.gain;
            }
        }
    }
    int latencySamples() const override { return spec_.latency; }
    nlohmann::json describe() const override { return {{"fake", true}, {"latency", spec_.latency}, {"state", state_}}; }
    const std::string& state() const { return state_; }

private:
    FakeSpec spec_;
    std::string state_;
    std::vector<float> l_, r_;
    std::size_t pos_ = 0;
};

class FakePluginHost final : public IPluginHost {
public:
    std::map<std::string, FakeSpec> known;  // ids this host can load; any other id is "missing"
    bool deferLoads = false;                // true: acquire returns nullptr until finishLoads()
    int created = 0;

    std::vector<PluginDescriptor> catalogue() const override {
        std::vector<PluginDescriptor> out;
        for (const auto& [id, spec] : known) out.push_back(PluginDescriptor{id, "Fake " + id, "Fake", "1", "", ""});
        return out;
    }

    std::shared_ptr<audio::IProcessor> acquire(const InsertSlot& slot, const ProcessorRef& ref, double, int) override {
        std::lock_guard lock(mutex_);
        if (!known.count(ref.processorId)) return nullptr;
        const std::string key = keyOf(slot);
        if (auto it = live_.find(key); it != live_.end() && it->second.id == ref.processorId && it->second.state == ref.state)
            return it->second.proc;
        if (deferLoads) {
            pending_.push_back({slot, ref});
            return nullptr;
        }
        return create(slot, ref);
    }

    void prune(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) override {
        std::lock_guard lock(mutex_);
        for (auto it = live_.begin(); it != live_.end();) {
            bool keep = false;
            for (const auto& [slot, ref] : live)
                if (keyOf(slot) == it->first && ref.processorId == it->second.id) keep = true;
            it = keep ? std::next(it) : live_.erase(it);
        }
    }

    std::string captureState(const InsertSlot& slot) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(keyOf(slot));
        if (it == live_.end()) return {};
        it->second.state = nextCaptured;
        return nextCaptured;
    }
    std::string nextCaptured = "CAPT";

    void setReadyListener(std::function<void(const InsertSlot&)> l) override {
        std::lock_guard lock(mutex_);
        listener_ = std::move(l);
    }

    // Creates every pending instance and tells the listener (as the real host does after a background load).
    void finishLoads() {
        std::vector<InsertSlot> ready;
        std::function<void(const InsertSlot&)> listener;
        {
            std::lock_guard lock(mutex_);
            for (auto& [slot, ref] : pending_) {
                create(slot, ref);
                ready.push_back(slot);
            }
            pending_.clear();
            listener = listener_;
        }
        if (listener)
            for (const InsertSlot& s : ready) listener(s);
    }

    std::size_t liveCount() const {
        std::lock_guard lock(mutex_);
        return live_.size();
    }

private:
    struct Live {
        std::string id, state;
        std::shared_ptr<audio::IProcessor> proc;
    };
    static std::string keyOf(const InsertSlot& s) { return s.track.toString() + "/" + std::to_string(s.index); }
    std::shared_ptr<audio::IProcessor> create(const InsertSlot& slot, const ProcessorRef& ref) {
        ++created;
        auto proc = std::make_shared<FakeProcessor>(known.at(ref.processorId), ref.state);
        live_[keyOf(slot)] = Live{ref.processorId, ref.state, proc};
        return proc;
    }

    mutable std::mutex mutex_;
    std::map<std::string, Live> live_;
    std::vector<std::pair<InsertSlot, ProcessorRef>> pending_;
    std::function<void(const InsertSlot&)> listener_;
};

}  // namespace lpc::test
```

- [ ] **Step 2: Write the failing tests**

`tests/test_plugin_insert.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <random>
#include "fake_plugin_host.h"
#include "lpc/audio/render_graph.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"

using namespace lpc;
using namespace lpc::audio;
using lpc::test::FakePluginHost;
using lpc::test::FakeSpec;

namespace {
const char* kId = "vst3:00112233445566778899aabbccddeeff";
ProcessorRef plugin(const char* id = kId) {
    ProcessorRef r;
    r.processorId = id;
    return r;
}
InsertSlot slot() { return InsertSlot{Uuid{1, 2}, 0}; }
}  // namespace

TEST_CASE("makeInsert: built-ins are unchanged", "[plugin]") {
    ProcessorRef gain;
    gain.processorId = "builtin.gain";
    gain.params["gainDb"] = -6.0;
    auto p = makeInsert(gain, nullptr, slot(), 48000.0, 512);
    REQUIRE(p);
    float l[1] = {1.0f}, r[1] = {1.0f};
    p->process(l, r, 1);
    REQUIRE(l[0] == Catch::Approx(0.5012f).margin(0.001));
    REQUIRE(makeInsert(plugin("nope"), nullptr, slot(), 48000.0, 512) == nullptr);  // unknown non-plug-in ids are skipped
}

TEST_CASE("makeInsert: a plug-in without a host is a pass-through marked missing", "[plugin]") {
    auto p = makeInsert(plugin(), nullptr, slot(), 48000.0, 512);
    REQUIRE(p);
    REQUIRE(p->describe().at("missing") == true);
    REQUIRE(p->latencySamples() == 0);
    float l[2] = {0.25f, -0.5f}, r[2] = {1.0f, 2.0f};
    p->process(l, r, 2);
    REQUIRE(l[0] == 0.25f);
    REQUIRE(l[1] == -0.5f);
    REQUIRE(r[1] == 2.0f);
}

TEST_CASE("makeInsert: unknown to the host is missing too", "[plugin]") {
    FakePluginHost host;  // knows nothing
    auto p = makeInsert(plugin(), &host, slot(), 48000.0, 512);
    REQUIRE(p->describe().at("missing") == true);
}

TEST_CASE("makeInsert: a known plug-in is wrapped and forwards latency", "[plugin]") {
    FakePluginHost host;
    host.known[kId] = FakeSpec{4, 0.5f};
    auto p = makeInsert(plugin(), &host, slot(), 48000.0, 512);
    REQUIRE(p->latencySamples() == 4);
    float l[8] = {1, 0, 0, 0, 0, 0, 0, 0}, r[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    p->process(l, r, 8);
    REQUIRE(l[4] == 0.5f);  // delayed by 4 and scaled
    REQUIRE(l[0] == 0.0f);
    REQUIRE(p->describe().at("fake") == true);
}

TEST_CASE("makeInsert: a pending load is a pass-through, not a failure", "[plugin]") {
    FakePluginHost host;
    host.known[kId] = FakeSpec{};
    host.deferLoads = true;
    auto p = makeInsert(plugin(), &host, slot(), 48000.0, 512);
    REQUIRE(p->describe().at("missing") == true);
}

TEST_CASE("buildConfig: plug-in inserts keep their position and ask the host by slot", "[plugin][graph]") {
    std::mt19937_64 rng(3);
    Project project;
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Audio;
    t.name = "A";
    REQUIRE(makeAddTrack(t)->apply(project).ok());
    ProcessorRef gain;
    gain.processorId = "builtin.gain";
    REQUIRE(makeAddInsert(t.id, gain)->apply(project).ok());
    REQUIRE(makeAddInsert(t.id, plugin())->apply(project).ok());

    FakePluginHost host;
    host.known[kId] = FakeSpec{64, 1.0f};
    MediaStore media;
    auto cfg = buildConfig(project, *project.findTrack(t.id), media, &host);
    REQUIRE(cfg->inserts.size() == 2);
    REQUIRE(cfg->inserts[0]->latencySamples() == 0);
    REQUIRE(cfg->inserts[1]->latencySamples() == 64);
    REQUIRE(host.created == 1);

    auto again = buildConfig(project, *project.findTrack(t.id), media, &host);  // reuses the live instance
    REQUIRE(host.created == 1);
}
```

- [ ] **Step 3: Run to see failure**

Run: `cmake --build build-core --config Debug --target lpc_tests`
Expected: compile error, `lpc/plugin_host.h` not found.

- [ ] **Step 4: Implement `IProcessor` additions and the two wrappers**

`core/include/lpc/audio/processors.h`: inside `IProcessor` add before `process`:

```cpp
    // Project thread, before the processor is handed to the audio thread.
    virtual void prepare(double /*sampleRate*/, int /*maxBlock*/) {}
    // Latency in frames, read on the project thread when the graph is built.
    virtual int latencySamples() const { return 0; }
```

After `GainProcessor` add:

```cpp
// Stands in for a plug-in that is not available (missing, still loading, failed): the signal passes unchanged.
class MissingPluginProcessor final : public IProcessor {
public:
    void process(float*, float*, int) noexcept override {}
    nlohmann::json describe() const override { return {{"missing", true}}; }
};

// An insert backed by a processor that outlives the config that uses it (a live plug-in instance).
class SharedProcessor final : public IProcessor {
public:
    explicit SharedProcessor(std::shared_ptr<IProcessor> inner) : inner_(std::move(inner)) {}
    void process(float* l, float* r, int frames) noexcept override { inner_->process(l, r, frames); }
    int latencySamples() const override { return inner_->latencySamples(); }
    nlohmann::json describe() const override { return inner_->describe(); }

private:
    std::shared_ptr<IProcessor> inner_;
};
```

(`<memory>` is already included.)

- [ ] **Step 5: Implement `plugin_host.h/.cpp`**

`core/include/lpc/plugin_host.h`:

```cpp
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lpc/audio/processors.h"
#include "lpc/model.h"

namespace lpc {

struct PluginDescriptor {
    std::string id;       // "vst3:<32 hex>"
    std::string name, vendor, version, path;
    std::string native;   // opaque data of the host that found it (JUCE: the plug-in description as XML)
    bool operator==(const PluginDescriptor&) const = default;
};

struct InsertSlot {
    Uuid track;
    int index = 0;
    bool operator==(const InsertSlot&) const = default;
};

class IPluginHost {
public:
    virtual ~IPluginHost() = default;
    virtual std::vector<PluginDescriptor> catalogue() const = 0;
    // Project thread. Never blocks. The live, prepared processor behind `slot`, reused while the insert's id and state are
    // unchanged; nullptr while it is not available (loading, missing, failed). A first call starts loading in the background
    // and the ready listener fires when the instance exists.
    virtual std::shared_ptr<audio::IProcessor> acquire(const InsertSlot& slot, const ProcessorRef& ref, double sampleRate, int maxBlock) = 0;
    // Project thread. Drops every instance not listed (slot and the ref it must match).
    virtual void prune(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) = 0;
    // UI thread. Serialises the live state (base64) and remembers it as "already in the model" so the command that stores
    // it does not make the next acquire reload the instance. Empty string when there is no live instance.
    virtual std::string captureState(const InsertSlot& slot) = 0;
    virtual void setReadyListener(std::function<void(const InsertSlot&)> listener) = 0;  // any thread
};

// Builds one insert. Built-ins as before; "vst3:" ids come from the host and are a pass-through (MissingPluginProcessor)
// while the host has nothing for them. nullptr for any other unknown id. Project thread.
std::unique_ptr<audio::IProcessor> makeInsert(const ProcessorRef& ref, IPluginHost* host, const InsertSlot& slot, double sampleRate, int maxBlock);

}  // namespace lpc
```

`core/src/plugin_host.cpp`:

```cpp
#include "lpc/plugin_host.h"

#include "lpc/processor_ids.h"

namespace lpc {

std::unique_ptr<audio::IProcessor> makeInsert(const ProcessorRef& ref, IPluginHost* host, const InsertSlot& slot, double sampleRate, int maxBlock) {
    if (!isVst3Id(ref.processorId)) return audio::makeEffect(ref);
    if (host) {
        if (std::shared_ptr<audio::IProcessor> live = host->acquire(slot, ref, sampleRate, maxBlock))
            return std::make_unique<audio::SharedProcessor>(std::move(live));
    }
    return std::make_unique<audio::MissingPluginProcessor>();
}

}  // namespace lpc
```

(`core/CMakeLists.txt` globs `src/*.cpp`, so the new file is picked up after a reconfigure; `CONFIGURE_DEPENDS` handles it.)

- [ ] **Step 6: Plumb the host through the graph builder**

`core/include/lpc/graph_builder.h`: add `#include "lpc/plugin_host.h"` and change the four declarations:

```cpp
std::unique_ptr<audio::TrackConfig> buildConfig(const Project& project, const Track& track, MediaStore& media, IPluginHost* plugins = nullptr);
std::unique_ptr<audio::TrackNode> buildNode(const Project& project, const Track& track, MediaStore& media, IPluginHost* plugins = nullptr);

std::vector<audio::AudioMsg> initialMessages(const Project& project, MediaStore& media, IPluginHost* plugins = nullptr);
std::vector<audio::AudioMsg> diffToMessages(const Project& before, const Project& after, MediaStore& media, IPluginHost* plugins = nullptr);
```

`core/src/graph_builder.cpp`:
- `addMsg`, `stripMsg` unchanged. Change `configMsg` signature to `AudioMsg configMsg(const Project& p, const Track& t, MediaStore& media, IPluginHost* plugins)` and its body to `m.obj = makeOwned(buildConfig(p, t, media, plugins).release());`.
- In `buildConfig` replace the insert loop

```cpp
    for (const ProcessorRef& ref : t.strip.inserts)
        if (auto effect = makeEffect(ref)) cfg->inserts.push_back(std::move(effect));
```
by
```cpp
    int slotIndex = 0;
    for (const ProcessorRef& ref : t.strip.inserts) {
        if (auto effect = makeInsert(ref, plugins, InsertSlot{t.id, slotIndex}, static_cast<double>(p.sampleRate), kMaxBlock))
            cfg->inserts.push_back(std::move(effect));
        ++slotIndex;
    }
```
and its signature to take `IPluginHost* plugins`.
- `buildNode`: pass `plugins` to `buildConfig`.
- `initialMessages`: `addMsg(buildNode(p, t, media, plugins))`.
- `diffToMessages`: pass `plugins` to `buildNode` and `configMsg(after, a, media, plugins)`.

- [ ] **Step 7: Run all Core tests**

Run: `cmake --build build-core --config Debug --target lpc_tests && ctest --test-dir build-core -C Debug --output-on-failure`
Expected: all pass (the new `[plugin]` tests too; existing graph tests still use the defaulted parameter).

- [ ] **Step 8: Commit**

```bash
git add core tests
git commit -m "feat(core): IPluginHost, missing and shared processors, makeInsert"
```

---

### Task 6: Plug-in delay compensation and offline alignment (Core)

**Files:**
- Modify: `core/include/lpc/audio/render_graph.h`, `core/src/audio/render_graph.cpp`, `core/include/lpc/graph_builder.h`, `core/src/graph_builder.cpp`, `core/include/lpc/offline_render.h`, `core/src/offline_render.cpp`
- Test: `tests/test_pdc.cpp`

**Interfaces:**
- Consumes: Task 5 (`IPluginHost`, `FakePluginHost`, `IProcessor::latencySamples`).
- Produces (exact):
  - `audio::DelayLine` (`explicit DelayLine(int frames = 0)`, `int frames() const`, `void process(const float* inL, const float* inR, float* outL, float* outR, int n) noexcept`); `audio::SendPlayback` gains `DelayLine delay;`; `audio::TrackConfig` gains `DelayLine outputDelay;`; constants `inline constexpr int kMaxPdcFrames = 1 << 18;`.
  - `struct EdgeDelays { int output = 0; std::vector<int> sends; bool operator==(const EdgeDelays&) const = default; }`, `struct PdcPlan { std::unordered_map<Uuid, EdgeDelays> edges; int totalLatency = 0; }`, `PdcPlan computePdc(const Project&, IPluginHost*)`.
  - `buildConfig` and `buildNode` get a trailing `const PdcPlan* pdc = nullptr` (null: no delays). `initialMessages`, `diffToMessages` compute the plan themselves.
  - `std::vector<audio::AudioMsg> refreshMessages(const Project&, MediaStore&, IPluginHost*)`: a `SetConfig` for every track.
  - `RenderOptions::plugins` (`IPluginHost*`, default null); the render drops the first `totalLatency` frames.

- [ ] **Step 1: Write the failing tests**

`tests/test_pdc.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include "fake_plugin_host.h"
#include "lpc/audio/render_graph.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "lpc/offline_render.h"

using namespace lpc;
using namespace lpc::audio;
using lpc::test::FakePluginHost;
using lpc::test::FakeSpec;

namespace {

std::string idOf(int n) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "vst3:%032x", n);
    return buf;
}

struct Rig {
    std::mt19937_64 rng{21};
    Project p;
    FakePluginHost host;

    Uuid addTrack(TrackKind kind, const char* name, int latency = 0) {
        Track t;
        t.id = Uuid::random(rng);
        t.kind = kind;
        t.name = name;
        if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{"builtin.sine", {}, ""};
        REQUIRE(makeAddTrack(t)->apply(p).ok());
        if (latency > 0) {
            const std::string id = idOf(latency);
            host.known[id] = FakeSpec{latency, 1.0f};
            ProcessorRef r;
            r.processorId = id;
            REQUIRE(makeAddInsert(t.id, r)->apply(p).ok());
        }
        return t.id;
    }
    void routeOutput(const Uuid& track, const Uuid& to) { REQUIRE(makeSetOutput(track, to)->apply(p).ok()); }
    void send(const Uuid& track, const Uuid& to) {
        Send s;
        s.id = Uuid::random(rng);
        s.targetTrackId = to;
        REQUIRE(makeAddSend(track, s)->apply(p).ok());
    }
};

}  // namespace

TEST_CASE("DelayLine: delays by N frames across block boundaries", "[pdc]") {
    DelayLine d(4);
    std::vector<float> in(10, 0.0f), inR(10, 0.0f), outL(10), outR(10);
    in[0] = 1.0f;
    inR[1] = 1.0f;
    // process in blocks of 3, 3, 4
    int pos = 0;
    for (int n : {3, 3, 4}) {
        d.process(in.data() + pos, inR.data() + pos, outL.data() + pos, outR.data() + pos, n);
        pos += n;
    }
    for (int i = 0; i < 10; ++i) {
        REQUIRE(outL[static_cast<std::size_t>(i)] == (i == 4 ? 1.0f : 0.0f));
        REQUIRE(outR[static_cast<std::size_t>(i)] == (i == 5 ? 1.0f : 0.0f));
    }
}

TEST_CASE("DelayLine: zero frames is a copy", "[pdc]") {
    DelayLine d(0);
    const float inL[3] = {1, 2, 3}, inR[3] = {4, 5, 6};
    float outL[3], outR[3];
    d.process(inL, inR, outL, outR, 3);
    REQUIRE(outL[2] == 3.0f);
    REQUIRE(outR[0] == 4.0f);
}

TEST_CASE("pdc: no plug-ins means no delays", "[pdc]") {
    Rig r;
    const Uuid a = r.addTrack(TrackKind::Audio, "A");
    const PdcPlan plan = computePdc(r.p, &r.host);
    REQUIRE(plan.totalLatency == 0);
    REQUIRE(plan.edges.at(a).output == 0);
}

TEST_CASE("pdc: two tracks to the master", "[pdc]") {
    Rig r;
    const Uuid a = r.addTrack(TrackKind::Audio, "A", 64);
    const Uuid b = r.addTrack(TrackKind::Audio, "B");
    const PdcPlan plan = computePdc(r.p, &r.host);
    REQUIRE(plan.totalLatency == 64);
    REQUIRE(plan.edges.at(a).output == 0);
    REQUIRE(plan.edges.at(b).output == 64);
}

TEST_CASE("pdc: through a bus that has its own latency", "[pdc]") {
    Rig r;
    const Uuid bus = r.addTrack(TrackKind::Bus, "Bus", 30);
    const Uuid a = r.addTrack(TrackKind::Audio, "A", 100);
    const Uuid b = r.addTrack(TrackKind::Audio, "B");
    r.routeOutput(a, bus);
    const PdcPlan plan = computePdc(r.p, &r.host);
    REQUIRE(plan.edges.at(a).output == 0);    // alone into the bus
    REQUIRE(plan.edges.at(bus).output == 0);  // 100 + 30 = 130 arrives at the master
    REQUIRE(plan.edges.at(b).output == 130);
    REQUIRE(plan.totalLatency == 130);
}

TEST_CASE("pdc: sends are aligned at the bus", "[pdc]") {
    Rig r;
    const Uuid bus = r.addTrack(TrackKind::Bus, "Bus");
    const Uuid a = r.addTrack(TrackKind::Audio, "A", 50);
    const Uuid b = r.addTrack(TrackKind::Audio, "B");
    r.send(a, bus);
    r.send(b, bus);
    const PdcPlan plan = computePdc(r.p, &r.host);
    REQUIRE(plan.edges.at(a).sends == std::vector<int>{0});
    REQUIRE(plan.edges.at(b).sends == std::vector<int>{50});
    REQUIRE(plan.edges.at(a).output == 0);
    REQUIRE(plan.edges.at(b).output == 50);  // B's direct output is aligned with the delayed signals
    REQUIRE(plan.totalLatency == 50);
}

TEST_CASE("pdc: a plug-in that is not loaded yet has no latency", "[pdc]") {
    Rig r;
    const Uuid a = r.addTrack(TrackKind::Audio, "A", 64);
    r.host.deferLoads = true;
    const PdcPlan plan = computePdc(r.p, &r.host);
    REQUIRE(plan.totalLatency == 0);
    REQUIRE(plan.edges.at(a).output == 0);
}

TEST_CASE("pdc: absurd latencies are clamped", "[pdc]") {
    Rig r;
    r.addTrack(TrackKind::Audio, "A", 10'000'000);
    const PdcPlan plan = computePdc(r.p, &r.host);
    REQUIRE(plan.totalLatency <= kMaxPdcFrames);
}

TEST_CASE("pdc: diffToMessages rebuilds the configs whose delays change", "[pdc][graph]") {
    Rig r;
    const Uuid a = r.addTrack(TrackKind::Audio, "A");
    const Uuid b = r.addTrack(TrackKind::Audio, "B");
    Project before = r.p;
    // a plug-in with latency appears on A: A's config changes (insert), and B's too (its output now needs 64 frames)
    const std::string id = idOf(64);
    r.host.known[id] = FakeSpec{64, 1.0f};
    ProcessorRef ref;
    ref.processorId = id;
    REQUIRE(makeAddInsert(a, ref)->apply(r.p).ok());
    MediaStore media;
    const auto msgs = diffToMessages(before, r.p, media, &r.host);
    int configs = 0;
    for (const AudioMsg& m : msgs) {
        if (m.kind == MsgKind::SetConfig) {
            ++configs;
            if (m.track == b) REQUIRE(static_cast<TrackConfig*>(m.obj.ptr)->outputDelay.frames() == 64);
        }
    }
    REQUIRE(configs == 2);
    for (AudioMsg m : msgs) m.obj.destroy();
}

TEST_CASE("pdc: refreshMessages sends a config for every track", "[pdc][graph]") {
    Rig r;
    r.addTrack(TrackKind::Audio, "A");
    r.addTrack(TrackKind::Audio, "B");
    MediaStore media;
    auto msgs = refreshMessages(r.p, media, &r.host);
    REQUIRE(msgs.size() == r.p.tracks.size());
    for (AudioMsg m : msgs) {
        REQUIRE(m.kind == MsgKind::SetConfig);
        m.obj.destroy();
    }
}

TEST_CASE("pdc: offline render is aligned with the timeline", "[pdc][render]") {
    Rig r;
    const Uuid a = r.addTrack(TrackKind::Audio, "A", 64);
    const Uuid b = r.addTrack(TrackKind::Audio, "B");
    // one second of audio at 48 kHz with an impulse at frame 100, on both tracks
    std::vector<float> impulse(2 * 4800, 0.0f);
    impulse[2 * 100] = impulse[2 * 100 + 1] = 1.0f;
    MediaItem item{Uuid::random(r.rng), "x.wav", "h", 48000, 2, 4800};
    REQUIRE(makeAddMedia(item)->apply(r.p).ok());
    MediaStore media;
    media.registerSource(item.id, std::make_shared<MemorySource>(48000, 2, impulse));
    for (const Uuid& t : {a, b}) {
        Region reg;
        reg.id = Uuid::random(r.rng);
        reg.timeBase = TimeBase::Absolute;
        reg.start = 0;
        reg.length = 100000;  // 0.1 s
        reg.mediaId = item.id;
        REQUIRE(makeAddRegion(t, reg)->apply(r.p).ok());
    }
    RenderOptions o;
    o.frames = 400;
    o.plugins = &r.host;
    const RenderResult res = renderOffline(r.p, media, o);
    REQUIRE(res.frames == 400);
    for (int i = 0; i < 400; ++i) {
        const float expected = i == 100 ? 2.0f : 0.0f;  // both impulses on the same frame
        REQUIRE(std::abs(res.interleaved[static_cast<std::size_t>(i) * 2] - expected) < 1e-5f);
    }
}
```

- [ ] **Step 2: Run to see failure**

Run: `cmake --build build-core --config Debug --target lpc_tests`
Expected: compile errors (`DelayLine`, `computePdc`, `PdcPlan`, `refreshMessages`, `RenderOptions::plugins` missing).

- [ ] **Step 3: Add `DelayLine` and the config fields**

`core/include/lpc/audio/render_graph.h`: add `#include <vector>` (present) and, before `struct NoteSpan`:

```cpp
inline constexpr int kMaxPdcFrames = 1 << 18;  // longest compensation delay of one edge (about 5.4 s at 48 kHz)

// A fixed delay of stereo audio. The buffers are allocated in the constructor (project thread); process() never allocates.
class DelayLine {
public:
    explicit DelayLine(int frames = 0)
        : size_(frames > 0 ? frames : 0), l_(static_cast<std::size_t>(size_), 0.0f), r_(static_cast<std::size_t>(size_), 0.0f) {}
    int frames() const { return size_; }
    // out = in delayed by frames(). `in` and `out` must not overlap.
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) noexcept {
        if (size_ == 0) {
            std::copy_n(inL, n, outL);
            std::copy_n(inR, n, outR);
            return;
        }
        std::size_t p = pos_;
        const std::size_t size = static_cast<std::size_t>(size_);
        for (int i = 0; i < n; ++i) {
            outL[i] = l_[p];
            outR[i] = r_[p];
            l_[p] = inL[i];
            r_[p] = inR[i];
            if (++p == size) p = 0;
        }
        pos_ = p;
    }

private:
    int size_;
    std::vector<float> l_, r_;
    std::size_t pos_ = 0;
};
```

(add `#include <algorithm>` at the top). Change `SendPlayback` and `TrackConfig`:

```cpp
struct SendPlayback {
    Uuid target;
    float gain = 1.0f;
    bool preFader = false;
    DelayLine delay;  // plug-in delay compensation
};
```

and in `TrackConfig` after `Uuid output;`:

```cpp
    DelayLine outputDelay;  // plug-in delay compensation of the output edge
```

- [ ] **Step 4: Use the delays when rendering**

`core/include/lpc/audio/render_graph.h`, class `RenderGraph`: add members `std::vector<float> dlyL_, dlyR_;` next to `preL_`. In `core/src/audio/render_graph.cpp` initialise them in the constructor: add `, dlyL_(kMaxBlock), dlyR_(kMaxBlock)` to the initializer list.

In `processNode`, change `const TrackConfig* cfg = t.config;` to `TrackConfig* cfg = t.config;` and replace the sends and output part (from `if (!muted) {` to the end of the function) by:

```cpp
    if (!muted) {
        for (SendPlayback& s : cfg->sends) {
            TrackNode* dst = find(s.target);
            if (!dst || dst == &t) continue;
            const float* srcL = s.preFader ? preL_.data() : l;
            const float* srcR = s.preFader ? preR_.data() : r;
            if (s.delay.frames() > 0) {
                s.delay.process(srcL, srcR, dlyL_.data(), dlyR_.data(), n);
                srcL = dlyL_.data();
                srcR = dlyR_.data();
            }
            for (int i = 0; i < n; ++i) {
                dst->l[static_cast<std::size_t>(i)] += srcL[i] * s.gain;
                dst->r[static_cast<std::size_t>(i)] += srcR[i] * s.gain;
            }
        }
    }
    TrackNode* out = cfg->output.isNull() ? master_ : find(cfg->output);
    if (out && out != &t) {
        const float* srcL = l;
        const float* srcR = r;
        if (cfg->outputDelay.frames() > 0) {
            cfg->outputDelay.process(l, r, dlyL_.data(), dlyR_.data(), n);
            srcL = dlyL_.data();
            srcR = dlyR_.data();
        }
        for (int i = 0; i < n; ++i) {
            out->l[static_cast<std::size_t>(i)] += srcL[i];
            out->r[static_cast<std::size_t>(i)] += srcR[i];
        }
    }
}
```

In `describe()` add `{"delay", s.delay.frames()}` to each send object and, after `j["output"] = ...`, `j["outputDelay"] = t.config->outputDelay.frames();`. (Existing graph tests compare descriptions built by the same code on both sides; adding fields is safe. If a golden test file stores a `describe()` output, update it in this step.)

- [ ] **Step 5: Implement `computePdc`, plumb it, add `refreshMessages`**

`core/include/lpc/graph_builder.h`: add `#include <unordered_map>` and

```cpp
struct EdgeDelays {
    int output = 0;
    std::vector<int> sends;  // in the order of the track's sends
    bool operator==(const EdgeDelays&) const = default;
};

// Plug-in delay compensation. Every edge (a track's output and each of its sends) gets the delay that aligns the signals
// arriving at its target; totalLatency is the delay between the timeline and the master output.
struct PdcPlan {
    std::unordered_map<Uuid, EdgeDelays> edges;  // every track except the master
    int totalLatency = 0;
};
PdcPlan computePdc(const Project& project, IPluginHost* plugins);
```

and change the declarations to:

```cpp
std::unique_ptr<audio::TrackConfig> buildConfig(const Project& project, const Track& track, MediaStore& media,
                                                IPluginHost* plugins = nullptr, const PdcPlan* pdc = nullptr);
std::unique_ptr<audio::TrackNode> buildNode(const Project& project, const Track& track, MediaStore& media,
                                            IPluginHost* plugins = nullptr, const PdcPlan* pdc = nullptr);
// A SetConfig for every track (used when a plug-in finishes loading).
std::vector<audio::AudioMsg> refreshMessages(const Project& project, MediaStore& media, IPluginHost* plugins);
```

(`initialMessages` and `diffToMessages` keep the signatures of Task 5.)

`core/src/graph_builder.cpp`:

Add helper in the anonymous namespace:

```cpp
int clampLatency(int v) { return std::clamp(v, 0, kMaxPdcFrames); }

int trackLatency(const Project& p, const Track& t, IPluginHost* plugins) {
    if (!plugins) return 0;
    int sum = 0;
    for (std::size_t i = 0; i < t.strip.inserts.size(); ++i) {
        const ProcessorRef& ref = t.strip.inserts[i];
        if (!isVst3Id(ref.processorId)) continue;
        if (auto live = plugins->acquire(InsertSlot{t.id, static_cast<int>(i)}, ref, static_cast<double>(p.sampleRate), kMaxBlock))
            sum += clampLatency(live->latencySamples());
    }
    return clampLatency(sum);
}
```

(include `"lpc/processor_ids.h"` at the top.) After `processingOrder` add:

```cpp
PdcPlan computePdc(const Project& p, IPluginHost* plugins) {
    PdcPlan plan;
    const Track* master = p.master();
    const std::vector<Uuid> order = processingOrder(p);
    std::unordered_map<Uuid, int> latency, in;
    for (const Track& t : p.tracks) {
        latency[t.id] = trackLatency(p, t, plugins);
        in[t.id] = 0;
    }
    auto forEachEdge = [&](const Track& t, auto&& fn) {  // fn(isOutput, sendIndex, target)
        const Uuid out = t.strip.output.isNull() && master ? master->id : t.strip.output;
        fn(true, -1, out);
        for (std::size_t i = 0; i < t.strip.sends.size(); ++i) fn(false, static_cast<int>(i), t.strip.sends[i].targetTrackId);
    };
    for (const Uuid& id : order) {  // sources before the buses they feed
        const Track* t = p.findTrack(id);
        if (!t || t->kind == TrackKind::Master) continue;
        const int out = in[id] + latency[id];
        forEachEdge(*t, [&](bool, int, const Uuid& to) {
            if (auto it = in.find(to); it != in.end() && to != id) it->second = std::max(it->second, out);
        });
    }
    for (const Track& t : p.tracks) {
        if (t.kind == TrackKind::Master) continue;
        EdgeDelays e;
        e.sends.assign(t.strip.sends.size(), 0);
        const int out = in[t.id] + latency[t.id];
        forEachEdge(t, [&](bool isOutput, int sendIndex, const Uuid& to) {
            const auto it = in.find(to);
            if (it == in.end() || to == t.id) return;
            const int delay = std::clamp(it->second - out, 0, kMaxPdcFrames);
            if (isOutput) e.output = delay;
            else e.sends[static_cast<std::size_t>(sendIndex)] = delay;
        });
        plan.edges[t.id] = std::move(e);
    }
    if (master) plan.totalLatency = std::clamp(in[master->id] + latency[master->id], 0, kMaxPdcFrames);
    return plan;
}
```

In `buildConfig`, after the sends loop, apply the plan:

```cpp
    for (const Send& s : t.strip.sends) cfg->sends.push_back(SendPlayback{s.targetTrackId, dbToLinear(s.levelDb), s.preFader, {}});
    cfg->output = t.strip.output;
    if (pdc) {
        if (const auto it = pdc->edges.find(t.id); it != pdc->edges.end()) {
            cfg->outputDelay = DelayLine(it->second.output);
            for (std::size_t i = 0; i < cfg->sends.size() && i < it->second.sends.size(); ++i)
                cfg->sends[i].delay = DelayLine(it->second.sends[i]);
        }
    }
    return cfg;
```

(replacing the existing two lines for sends and output; the signature gets `IPluginHost* plugins, const PdcPlan* pdc`.) `buildNode` passes both through. Update `configMsg` to take `const PdcPlan* pdc` and pass it. Then:

```cpp
std::vector<AudioMsg> initialMessages(const Project& p, MediaStore& media, IPluginHost* plugins) {
    const PdcPlan pdc = computePdc(p, plugins);
    std::vector<AudioMsg> out;
    for (const Track& t : p.tracks) out.push_back(addMsg(buildNode(p, t, media, plugins, &pdc)));
    out.push_back(reorderMsg(p));
    return out;
}

std::vector<AudioMsg> refreshMessages(const Project& p, MediaStore& media, IPluginHost* plugins) {
    const PdcPlan pdc = computePdc(p, plugins);
    std::vector<AudioMsg> out;
    for (const Track& t : p.tracks) out.push_back(configMsg(p, t, media, plugins, &pdc));
    return out;
}
```

In `diffToMessages`, at the top: `const PdcPlan planBefore = computePdc(before, plugins); const PdcPlan planAfter = computePdc(after, plugins);` and replace the `buildNode(after, a, media)` call with `buildNode(after, a, media, plugins, &planAfter)`, and the config condition with:

```cpp
        const auto eb = planBefore.edges.find(a.id);
        const auto ea = planAfter.edges.find(a.id);
        const bool delaysChanged = (eb == planBefore.edges.end()) != (ea == planAfter.edges.end()) ||
                                   (eb != planBefore.edges.end() && !(eb->second == ea->second));
        if (timingChanged || delaysChanged || configChanged(*b, a)) out.push_back(configMsg(after, a, media, plugins, &planAfter));
```

- [ ] **Step 6: Offline render alignment**

`core/include/lpc/offline_render.h`: add `#include "lpc/plugin_host.h"` and in `RenderOptions`:

```cpp
    IPluginHost* plugins = nullptr;  // hosts the "vst3:" inserts; the output is aligned for their latency
```

`core/src/offline_render.cpp`: include `"lpc/graph_builder.h"` (present), then change the body:

```cpp
    audio::AudioEngine engine(static_cast<double>(p.sampleRate));
    for (const audio::AudioMsg& m : initialMessages(p, media, options.plugins)) engine.applyDirect(m);
    const std::int64_t skip = computePdc(p, options.plugins).totalLatency;  // frames the plug-ins delay everything by
```

and replace the render loop with

```cpp
    const int block = std::clamp(options.blockSize, 1, 65536);
    std::vector<float> l(static_cast<std::size_t>(block)), r(static_cast<std::size_t>(block));
    for (std::int64_t pos = 0; pos < total + skip; pos += block) {
        const int n = static_cast<int>(std::min<std::int64_t>(block, total + skip - pos));
        engine.processBlock(l.data(), r.data(), n);
        for (int i = 0; i < n; ++i) {
            const std::int64_t at = pos + i - skip;
            if (at < 0) continue;
            out.interleaved[static_cast<std::size_t>(at * 2)] = l[static_cast<std::size_t>(i)];
            out.interleaved[static_cast<std::size_t>(at * 2 + 1)] = r[static_cast<std::size_t>(i)];
        }
    }
```

Note: with `skip == 0` the loop is identical to the old one, so golden renders are unchanged. `processBlock` with `n > kMaxBlock` already behaves as before (existing code path).

- [ ] **Step 7: Run all Core tests**

Run: `cmake --build build-core --config Debug --target lpc_tests && ctest --test-dir build-core -C Debug --output-on-failure`
Expected: all pass, including `[pdc]` and the unchanged golden tests. If the offline-render test is off by one frame, check `skip` is applied to `at` and not to `locate`.

- [ ] **Step 8: Commit**

```bash
git add core tests/test_pdc.cpp
git commit -m "feat(core): plug-in delay compensation per edge, aligned offline render"
```

---

### Task 7: `ProjectHost` integration (Core)

**Files:**
- Modify: `core/include/lpc/project_host.h`, `core/src/project_host.cpp`
- Test: `tests/test_plugin_host_integration.cpp`

**Interfaces:**
- Consumes: Tasks 5 and 6.
- Produces: `ProjectHost(Project initial, audio::AudioEngine&, MediaStore&, IPluginHost* plugins = nullptr)`. With a host: every graph build passes it; after every publish and resync the host is pruned to the plug-in inserts of the model; when the host's ready listener fires, all track configs are rebuilt. The destructor clears the listener first.

- [ ] **Step 1: Write the failing tests**

`tests/test_plugin_host_integration.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <random>
#include <thread>
#include "fake_plugin_host.h"
#include "lpc/audio/engine.h"
#include "lpc/commands.h"
#include "lpc/project_host.h"

using namespace lpc;
using namespace lpc::audio;
using lpc::test::FakePluginHost;
using lpc::test::FakeSpec;

namespace {
const char* kId = "vst3:00112233445566778899aabbccddeeff";

// The audio thread is the test thread: it drains the messages the host posted.
void drain(AudioEngine& engine, ProjectHost& host) {
    host.read([](const Project&) { return 0; }).get();  // everything queued on the project thread has run
    float l[64], r[64];
    for (int i = 0; i < 3000 && engine.appliedSeq() < host.lastPostedSeq(); ++i) {
        engine.processBlock(l, r, 64);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    engine.processBlock(l, r, 64);
}

nlohmann::json insertOf(AudioEngine& engine, const Uuid& track) {
    for (const auto& t : engine.describeForTest().at("tracks"))
        if (t.at("id") == track.toString()) return t.at("inserts").at(0);
    return {};
}

struct Rig {
    std::mt19937_64 rng{17};
    Project initial;
    Uuid track;
    FakePluginHost plugins;
    MediaStore media;
    AudioEngine engine{48000.0};
    Rig() {
        Track t;
        t.id = Uuid::random(rng);
        t.kind = TrackKind::Audio;
        t.name = "A";
        track = t.id;
        REQUIRE(makeAddTrack(t)->apply(initial).ok());
        ProcessorRef r;
        r.processorId = kId;
        r.label = "Fake";
        REQUIRE(makeAddInsert(track, r)->apply(initial).ok());
        plugins.known[kId] = FakeSpec{8, 1.0f};
    }
};
}  // namespace

TEST_CASE("host+plugins: a plug-in that is not ready is replaced when it arrives", "[host][plugin][threads]") {
    Rig rig;
    rig.plugins.deferLoads = true;
    ProjectHost host(rig.initial, rig.engine, rig.media, &rig.plugins);
    drain(rig.engine, host);
    REQUIRE(insertOf(rig.engine, rig.track).at("missing") == true);

    rig.plugins.finishLoads();  // fires the ready listener from this thread
    drain(rig.engine, host);
    const auto after = insertOf(rig.engine, rig.track);
    REQUIRE(after.at("fake") == true);
    REQUIRE(after.at("latency") == 8);
}

TEST_CASE("host+plugins: removing the insert prunes the instance", "[host][plugin][threads]") {
    Rig rig;
    ProjectHost host(rig.initial, rig.engine, rig.media, &rig.plugins);
    drain(rig.engine, host);
    REQUIRE(rig.plugins.liveCount() == 1);
    REQUIRE_FALSE(host.submit(makeRemoveInsert(rig.track, 0)).get().has_value());
    drain(rig.engine, host);
    REQUIRE(rig.plugins.liveCount() == 0);
}

TEST_CASE("host+plugins: committing a captured state does not reload the instance", "[host][plugin][threads]") {
    Rig rig;
    ProjectHost host(rig.initial, rig.engine, rig.media, &rig.plugins);
    drain(rig.engine, host);
    REQUIRE(rig.plugins.created == 1);
    const std::string captured = rig.plugins.captureState(InsertSlot{rig.track, 0});  // the editor closed
    REQUIRE_FALSE(host.submit(makeSetInsertState(rig.track, 0, captured)).get().has_value());
    drain(rig.engine, host);
    REQUIRE(rig.plugins.created == 1);

    // undo puts the old state back in the model: the instance must be reloaded with it
    REQUIRE_FALSE(host.undo().get().has_value());
    drain(rig.engine, host);
    REQUIRE(rig.plugins.created == 2);
}

TEST_CASE("host+plugins: closing the host while a load is pending is safe", "[host][plugin][threads]") {
    Rig rig;
    rig.plugins.deferLoads = true;
    {
        ProjectHost host(rig.initial, rig.engine, rig.media, &rig.plugins);
        drain(rig.engine, host);
    }  // destructor clears the listener
    rig.plugins.finishLoads();  // must not call into the destroyed host
    SUCCEED();
}
```

- [ ] **Step 2: Run to see failure**

Run: `cmake --build build-core --config Debug --target lpc_tests`
Expected: compile error (the 4-argument constructor does not exist).

- [ ] **Step 3: Implement**

`core/include/lpc/project_host.h`: add `#include "lpc/plugin_host.h"`; change the constructor declaration to `ProjectHost(Project initial, audio::AudioEngine& engine, MediaStore& media, IPluginHost* plugins = nullptr);`; add private methods `void pruneInstances();` and `void rebuildAllConfigs();`; add the member `IPluginHost* plugins_;` after `MediaStore& media_;` (before `seq_`; the thread member stays last).

`core/src/project_host.cpp`:

```cpp
ProjectHost::ProjectHost(Project initial, audio::AudioEngine& engine, MediaStore& media, IPluginHost* plugins)
    : project_(std::move(initial)), engine_(engine), media_(media), plugins_(plugins), thread_([this] { run(); }) {
    if (plugins_) plugins_->setReadyListener([this](const InsertSlot&) { enqueue([this] { rebuildAllConfigs(); }); });
    enqueue([this] {
        auto messages = initialMessages(project_, media_, plugins_);
        postAll(messages);
        pruneInstances();
    });
}

ProjectHost::~ProjectHost() {
    if (plugins_) plugins_->setReadyListener({});  // nothing may enqueue into a host that is going away
    stopping_.store(true, std::memory_order_release);
    ...  // the rest is unchanged
```

Careful: the constructor's member initialiser order: `thread_` starts running before the body; the existing code already enqueues after construction, so enqueuing the listener first is fine.

In `resync()` replace `initialMessages(project_, media_)` with `initialMessages(project_, media_, plugins_)`. In `publish()`:

```cpp
void ProjectHost::publish(const Project& before) {
    if (degraded_) return;  // run() rebuilds the graph once the engine drains its queue again
    auto messages = diffToMessages(before, project_, media_, plugins_);
    postAll(messages);
    pruneInstances();
}
```

Add:

```cpp
void ProjectHost::rebuildAllConfigs() {
    if (degraded_) return;  // the rebuild that follows will pick the live instances up
    auto messages = refreshMessages(project_, media_, plugins_);
    postAll(messages);
}

void ProjectHost::pruneInstances() {
    if (!plugins_) return;
    std::vector<std::pair<InsertSlot, ProcessorRef>> live;
    for (const Track& t : project_.tracks)
        for (std::size_t i = 0; i < t.strip.inserts.size(); ++i)
            if (isVst3Id(t.strip.inserts[i].processorId)) live.push_back({InsertSlot{t.id, static_cast<int>(i)}, t.strip.inserts[i]});
    plugins_->prune(live);
}
```

(add `#include "lpc/processor_ids.h"`). In `resync()` call `pruneInstances();` after `postAll(messages)`.

One detail for the undo test: `publish` after `undo` runs `diffToMessages(before, after, ...)` which calls `computePdc(after)` → `acquire` with the old state → the fake creates a new instance (`created == 2`). `prune` afterwards keeps it.

- [ ] **Step 4: Run all Core tests**

Run: `cmake --build build-core --config Debug --target lpc_tests && ctest --test-dir build-core -C Debug --output-on-failure`
Expected: all pass, including the existing `[host]` tests (they use the 3-argument constructor).

- [ ] **Step 5: Commit**

```bash
git add core tests/test_plugin_host_integration.cpp
git commit -m "feat(core): ProjectHost hosts plug-ins, rebuilds when a load finishes, prunes dead instances"
```

---

### Task 8: JUCE host: processor, instances, editor window (platform)

**Files:**
- Modify: `platform/juce/CMakeLists.txt`, `CMakeLists.txt`
- Create: `platform/juce/plugin_processor.h/.cpp`, `platform/juce/editor_window.h/.cpp`, `platform/juce/juce_plugin_host.h/.cpp`, `platform/juce/tests/CMakeLists.txt`, `platform/juce/tests/test_plugin_host.cpp`

**Interfaces:**
- Consumes: Tasks 1 and 5 (`IPluginHost`, `PluginDescriptor`), the test VST3.
- Produces (exact, namespace `lpc`):

```cpp
class JucePluginHost final : public IPluginHost {
public:
    JucePluginHost();
    ~JucePluginHost() override;
    void setCatalogue(std::vector<PluginDescriptor> descriptors);   // any thread
    std::vector<PluginDescriptor> catalogue() const override;
    std::shared_ptr<audio::IProcessor> acquire(const InsertSlot&, const ProcessorRef&, double sampleRate, int maxBlock) override;
    void prune(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) override;
    std::string captureState(const InsertSlot&) override;           // message thread
    void setReadyListener(std::function<void(const InsertSlot&)>) override;
    // message thread
    bool openEditor(const InsertSlot&);                             // false when there is no live instance or no editor
    bool editorOpen(const InsertSlot&) const;
    void closeAllEditors();
    void setEditorClosedListener(std::function<void(const InsertSlot&)>);
    void releaseAll();                                              // drops every instance (project closed)
};
```

`PluginDescriptor::native` holds `juce::PluginDescription::createXml()->toString()`. Behaviour: `acquire` on the message thread creates synchronously; on another thread it returns `nullptr`, posts the creation to the message thread and calls the ready listener when done; a failed load is remembered (no retry for the same id and state); instances are destroyed on the message thread.

- [ ] **Step 1: Extend the platform CMake**

`platform/juce/CMakeLists.txt` (replace the whole file):

```cmake
add_library(lpc_juce_device STATIC
    juce_device.cpp
    plugin_processor.cpp
    editor_window.cpp
    juce_plugin_host.cpp)
target_include_directories(lpc_juce_device PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lpc_juce_device PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(lpc_juce_device
    PUBLIC lpc_core juce::juce_audio_devices juce::juce_audio_basics juce::juce_audio_processors juce::juce_gui_basics
           juce::juce_events juce::juce_core
    PRIVATE juce::juce_recommended_config_flags)

if(LPC_BUILD_PLUGIN_TESTS)
    add_subdirectory(tests)
endif()
```

(The scanner sources are added to this list in Task 9.) The root `CMakeLists.txt` already does `add_subdirectory(platform/juce)` inside the JUCE block and `add_subdirectory(tools/test-plugin)` after it (Task 1); `platform/juce/tests` needs `LPC_TEST_VST3_DIR`, which `tools/test-plugin` sets in its own scope only, so move the `add_subdirectory(tools/test-plugin)` line **before** `add_subdirectory(platform/juce)` and also set `set(LPC_TEST_VST3_DIR ...)` from the root: in `tools/test-plugin/CMakeLists.txt` the `PARENT_SCOPE` set makes it visible to the root scope, and `add_subdirectory(platform/juce)` inherits root variables. Order in the JUCE block of the root file becomes:

```cmake
    FetchContent_MakeAvailable(JUCE)
    if(LPC_BUILD_PLUGIN_TESTS)
        add_subdirectory(tools/test-plugin)
    endif()
    add_subdirectory(platform/juce)
```

- [ ] **Step 2: Write `plugin_processor.h/.cpp`**

`platform/juce/plugin_processor.h`:

```cpp
#pragma once
#include <memory>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "lpc/audio/processors.h"

namespace lpc {

// Adapter from a JUCE plug-in instance to the engine's IProcessor. process() and latencySamples() are for the audio and
// project threads; everything else is for the message thread.
class PluginProcessor final : public audio::IProcessor {
public:
    // Applies the stereo layout and the state, prepares the instance. nullptr when the plug-in refuses stereo in and out.
    // The returned object is destroyed on the message thread, whichever thread drops the last reference.
    static std::shared_ptr<PluginProcessor> create(std::unique_ptr<juce::AudioPluginInstance> instance, const std::string& stateBase64,
                                                   double sampleRate, int maxBlock);

    void process(float* l, float* r, int frames) noexcept override;
    int latencySamples() const override { return latency_; }
    nlohmann::json describe() const override { return {{"plugin", name_}, {"latency", latency_}}; }

    std::string captureState();                     // base64 of the plug-in's saved state
    bool hasEditor() const { return instance_->hasEditor(); }
    juce::AudioProcessorEditor* createEditor();     // the caller owns it and must delete it before this object goes
    const std::string& name() const { return name_; }

private:
    PluginProcessor(std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlock);
    ~PluginProcessor() override;

    std::unique_ptr<juce::AudioPluginInstance> instance_;
    juce::MidiBuffer midi_;
    int maxBlock_;
    int latency_ = 0;
    std::string name_;
};

}  // namespace lpc
```

`platform/juce/plugin_processor.cpp`:

```cpp
#include "plugin_processor.h"

#include <algorithm>

namespace lpc {

PluginProcessor::PluginProcessor(std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlock)
    : instance_(std::move(instance)), maxBlock_(maxBlock), name_(instance_->getName().toStdString()) {}

PluginProcessor::~PluginProcessor() { instance_->releaseResources(); }

std::shared_ptr<PluginProcessor> PluginProcessor::create(std::unique_ptr<juce::AudioPluginInstance> instance, const std::string& stateBase64,
                                                         double sampleRate, int maxBlock) {
    if (!instance) return nullptr;
    juce::AudioProcessor::BusesLayout layout;
    for (int i = 0; i < instance->getBusCount(true); ++i)
        layout.inputBuses.add(i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
    for (int i = 0; i < instance->getBusCount(false); ++i)
        layout.outputBuses.add(i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
    if (instance->getBusCount(true) < 1 || instance->getBusCount(false) < 1 || !instance->setBusesLayout(layout)) return nullptr;

    if (!stateBase64.empty()) {
        juce::MemoryBlock block;
        if (block.fromBase64Encoding(juce::String(stateBase64)) && block.getSize() > 0)
            instance->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    }
    instance->setPlayConfigDetails(2, 2, sampleRate, maxBlock);
    instance->prepareToPlay(sampleRate, maxBlock);

    auto* raw = new PluginProcessor(std::move(instance), maxBlock);
    raw->latency_ = std::max(0, raw->instance_->getLatencySamples());
    return std::shared_ptr<PluginProcessor>(raw, [](PluginProcessor* p) {
        auto* manager = juce::MessageManager::getInstanceWithoutCreating();
        if (!manager || manager->isThisTheMessageThread()) delete p;
        else juce::MessageManager::callAsync([p] { delete p; });
    });
}

void PluginProcessor::process(float* l, float* r, int frames) noexcept {
    juce::ScopedNoDenormals noDenormals;
    for (int done = 0; done < frames; done += maxBlock_) {
        const int n = std::min(maxBlock_, frames - done);
        float* channels[2] = {l + done, r + done};
        juce::AudioBuffer<float> buffer(channels, 2, n);  // wraps the caller's memory: no allocation
        midi_.clear();
        instance_->processBlock(buffer, midi_);
    }
}

std::string PluginProcessor::captureState() {
    juce::MemoryBlock block;
    instance_->getStateInformation(block);
    return block.toBase64Encoding().toStdString();
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return instance_->hasEditor() ? instance_->createEditor() : nullptr; }

}  // namespace lpc
```

- [ ] **Step 3: Write `editor_window.h/.cpp`**

`platform/juce/editor_window.h`:

```cpp
#pragma once
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace lpc {

// A native window around a plug-in's editor. Message thread only. `onClose` runs when the user closes the window; the owner
// must destroy the window afterwards, not from inside the callback.
class EditorWindow final : public juce::DocumentWindow {
public:
    EditorWindow(juce::AudioProcessorEditor* editor, const juce::String& title, std::function<void()> onClose);
    void closeButtonPressed() override { if (onClose_) onClose_(); }

private:
    std::function<void()> onClose_;
};

}  // namespace lpc
```

`platform/juce/editor_window.cpp`:

```cpp
#include "editor_window.h"

namespace lpc {

EditorWindow::EditorWindow(juce::AudioProcessorEditor* editor, const juce::String& title, std::function<void()> onClose)
    : DocumentWindow(title, juce::Colours::darkgrey, DocumentWindow::closeButton), onClose_(std::move(onClose)) {
    setUsingNativeTitleBar(true);
    setContentOwned(editor, true);
    setResizable(editor->isResizable(), false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

}  // namespace lpc
```

- [ ] **Step 4: Write the failing platform tests**

`platform/juce/tests/CMakeLists.txt`:

```cmake
add_executable(lpc_juce_tests test_plugin_host.cpp)
target_link_libraries(lpc_juce_tests PRIVATE lpc_juce_device Catch2::Catch2WithMain juce::juce_recommended_config_flags)
target_compile_definitions(lpc_juce_tests PRIVATE LPC_TEST_VST3_DIR="${LPC_TEST_VST3_DIR}")
add_dependencies(lpc_juce_tests lpc_test_gain)
add_test(NAME lpc_juce_tests COMMAND lpc_juce_tests)
```

`platform/juce/tests/test_plugin_host.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>

#include <juce_audio_processors/juce_audio_processors.h>

#include "juce_plugin_host.h"

using namespace lpc;

namespace {

juce::ScopedJuceInitialiser_GUI gJuce;  // creates the message manager on the main thread, which is the message thread

// Finds the test plug-in the way the scanner will: JUCE reads the .vst3 and describes it.
PluginDescriptor testPlugin() {
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

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

ProcessorRef refOf(const PluginDescriptor& d, const std::string& state = "") {
    ProcessorRef r;
    r.processorId = d.id;
    r.state = state;
    return r;
}

std::string base64Float(float v) {
    juce::MemoryBlock block(&v, sizeof(float));
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
    const std::string half = base64Float(0.5f);
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
    auto c = host.acquire(slot, refOf(d, base64Float(0.25f)), 48000.0, 512);
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
```

- [ ] **Step 5: Run to see failure**

Run: `cmake -S . -B build-plugin -G "Visual Studio 17 2022" -A x64 -DLPC_BUILD_PLUGIN_TESTS=ON && cmake --build build-plugin --config Debug --target lpc_juce_tests`
Expected: compile error (`juce_plugin_host.h` is missing).

- [ ] **Step 6: Write `juce_plugin_host.h`**

```cpp
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lpc/plugin_host.h"

namespace lpc {

class JucePluginHost final : public IPluginHost {
public:
    JucePluginHost();
    ~JucePluginHost() override;

    void setCatalogue(std::vector<PluginDescriptor> descriptors);  // any thread
    std::vector<PluginDescriptor> catalogue() const override;

    std::shared_ptr<audio::IProcessor> acquire(const InsertSlot& slot, const ProcessorRef& ref, double sampleRate, int maxBlock) override;
    void prune(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) override;
    std::string captureState(const InsertSlot& slot) override;
    void setReadyListener(std::function<void(const InsertSlot&)> listener) override;

    // Message thread only.
    bool openEditor(const InsertSlot& slot);
    bool editorOpen(const InsertSlot& slot) const;
    void closeAllEditors();
    void setEditorClosedListener(std::function<void(const InsertSlot&)> listener);
    void releaseAll();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;  // shared with the callbacks posted to the message thread, so they can outlive the host safely
};

}  // namespace lpc
```

- [ ] **Step 7: Write `juce_plugin_host.cpp`**

```cpp
#include "juce_plugin_host.h"

#include <map>
#include <mutex>

#include <juce_audio_processors/juce_audio_processors.h>

#include "editor_window.h"
#include "plugin_processor.h"

namespace lpc {

namespace {
std::string keyOf(const InsertSlot& s) { return s.track.toString() + "/" + std::to_string(s.index); }
}  // namespace

struct JucePluginHost::Impl : std::enable_shared_from_this<JucePluginHost::Impl> {
    struct Entry {
        std::string id, state;
        std::shared_ptr<PluginProcessor> proc;
        bool pending = false;
        bool failed = false;
        std::uint64_t generation = 0;
        InsertSlot slot;
        std::string label;
    };

    mutable std::mutex mutex;  // entries, catalogue, listener
    std::map<std::string, Entry> entries;
    std::vector<PluginDescriptor> catalogue;
    std::function<void(const InsertSlot&)> ready;
    std::uint64_t nextGeneration = 0;
    bool alive = true;

    // message thread only
    juce::AudioPluginFormatManager formats;
    std::map<std::string, std::unique_ptr<EditorWindow>> editors;
    std::map<std::string, InsertSlot> editorSlots;
    std::function<void(const InsertSlot&)> editorClosed;

    Impl() { formats.addFormat(new juce::VST3PluginFormat()); }

    std::optional<juce::PluginDescription> describe(const std::string& id) const {
        std::lock_guard lock(mutex);
        for (const PluginDescriptor& d : catalogue) {
            if (d.id != id) continue;
            if (auto xml = juce::XmlDocument::parse(juce::String(d.native))) {
                juce::PluginDescription desc;
                if (desc.loadFromXml(*xml)) return desc;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }

    void notifyReady(const InsertSlot& slot) {
        std::function<void(const InsertSlot&)> listener;
        {
            std::lock_guard lock(mutex);
            listener = ready;
        }
        if (listener) listener(slot);
    }

    void closeEditor(const std::string& key) {  // message thread
        editors.erase(key);
        editorSlots.erase(key);
    }

    // Message thread. Loads one instance and publishes it unless a newer request replaced this one.
    void load(const InsertSlot& slot, const ProcessorRef& ref, const juce::PluginDescription& desc, double sampleRate, int maxBlock,
              std::uint64_t generation) {
        juce::String error;
        std::unique_ptr<juce::AudioPluginInstance> instance = formats.createPluginInstance(desc, sampleRate, maxBlock, error);
        std::shared_ptr<PluginProcessor> proc = PluginProcessor::create(std::move(instance), ref.state, sampleRate, maxBlock);
        bool published = false;
        {
            std::lock_guard lock(mutex);
            auto it = entries.find(keyOf(slot));
            if (it != entries.end() && it->second.generation == generation) {
                it->second.pending = false;
                it->second.failed = !proc;
                it->second.proc = proc;
                published = true;
            }
        }
        if (published && proc) notifyReady(slot);
    }
};

JucePluginHost::JucePluginHost() : impl_(std::make_shared<Impl>()) {}

JucePluginHost::~JucePluginHost() {
    // Callbacks posted earlier hold a shared_ptr to Impl; they see `alive == false` and do nothing.
    {
        std::lock_guard lock(impl_->mutex);
        impl_->alive = false;
        impl_->ready = nullptr;
    }
    if (auto* manager = juce::MessageManager::getInstanceWithoutCreating(); manager && manager->isThisTheMessageThread()) {
        impl_->editors.clear();
    }
}

void JucePluginHost::setCatalogue(std::vector<PluginDescriptor> descriptors) {
    std::lock_guard lock(impl_->mutex);
    impl_->catalogue = std::move(descriptors);
}

std::vector<PluginDescriptor> JucePluginHost::catalogue() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->catalogue;
}

void JucePluginHost::setReadyListener(std::function<void(const InsertSlot&)> listener) {
    std::lock_guard lock(impl_->mutex);
    impl_->ready = std::move(listener);
}

void JucePluginHost::setEditorClosedListener(std::function<void(const InsertSlot&)> listener) { impl_->editorClosed = std::move(listener); }

std::shared_ptr<audio::IProcessor> JucePluginHost::acquire(const InsertSlot& slot, const ProcessorRef& ref, double sampleRate, int maxBlock) {
    const auto desc = impl_->describe(ref.processorId);
    if (!desc) return nullptr;
    const std::string key = keyOf(slot);
    std::uint64_t generation = 0;
    bool closeOldEditor = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->alive) return nullptr;
        Impl::Entry& e = impl_->entries[key];
        const bool same = e.id == ref.processorId && e.state == ref.state;
        if (same && e.proc) return e.proc;
        if (same && (e.pending || e.failed)) return nullptr;
        closeOldEditor = e.proc != nullptr;
        e.id = ref.processorId;
        e.state = ref.state;
        e.slot = slot;
        e.label = ref.label;
        e.proc.reset();
        e.pending = true;
        e.failed = false;
        generation = e.generation = ++impl_->nextGeneration;
    }

    auto impl = impl_;
    auto* manager = juce::MessageManager::getInstance();
    if (manager->isThisTheMessageThread()) {
        if (closeOldEditor) impl->closeEditor(key);
        impl->load(slot, ref, *desc, sampleRate, maxBlock, generation);
        std::lock_guard lock(impl->mutex);
        const auto it = impl->entries.find(key);
        return it != impl->entries.end() && it->second.generation == generation ? it->second.proc : nullptr;
    }
    juce::MessageManager::callAsync([impl, slot, ref, d = *desc, sampleRate, maxBlock, generation, key, closeOldEditor] {
        {
            std::lock_guard lock(impl->mutex);
            if (!impl->alive) return;
        }
        if (closeOldEditor) impl->closeEditor(key);
        impl->load(slot, ref, d, sampleRate, maxBlock, generation);
    });
    return nullptr;
}

void JucePluginHost::prune(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) {
    std::vector<std::string> dropped;
    {
        std::lock_guard lock(impl_->mutex);
        for (auto it = impl_->entries.begin(); it != impl_->entries.end();) {
            bool keep = false;
            for (const auto& [slot, ref] : live)
                if (keyOf(slot) == it->first && ref.processorId == it->second.id) keep = true;
            if (keep) {
                ++it;
            } else {
                dropped.push_back(it->first);
                it = impl_->entries.erase(it);
            }
        }
    }
    if (dropped.empty()) return;
    auto impl = impl_;
    juce::MessageManager::callAsync([impl, dropped] {
        for (const std::string& key : dropped) impl->closeEditor(key);
    });
}

std::string JucePluginHost::captureState(const InsertSlot& slot) {
    std::shared_ptr<PluginProcessor> proc;
    {
        std::lock_guard lock(impl_->mutex);
        const auto it = impl_->entries.find(keyOf(slot));
        if (it == impl_->entries.end() || !it->second.proc) return {};
        proc = it->second.proc;
    }
    const std::string state = proc->captureState();
    std::lock_guard lock(impl_->mutex);
    if (const auto it = impl_->entries.find(keyOf(slot)); it != impl_->entries.end() && it->second.proc == proc) it->second.state = state;
    return state;
}

bool JucePluginHost::openEditor(const InsertSlot& slot) {
    const std::string key = keyOf(slot);
    if (auto it = impl_->editors.find(key); it != impl_->editors.end()) {
        it->second->toFront(true);
        return true;
    }
    std::shared_ptr<PluginProcessor> proc;
    std::string label;
    {
        std::lock_guard lock(impl_->mutex);
        const auto it = impl_->entries.find(key);
        if (it == impl_->entries.end() || !it->second.proc) return false;
        proc = it->second.proc;
        label = it->second.label;
    }
    juce::AudioProcessorEditor* editor = proc->createEditor();
    if (!editor) return false;
    const juce::String title = label.empty() ? juce::String(proc->name()) : juce::String(label);
    auto weak = std::weak_ptr<Impl>(impl_);
    impl_->editors[key] = std::make_unique<EditorWindow>(editor, title, [weak, key, slot] {
        // never delete a window from inside its own callback: close it on the next message-loop turn
        juce::MessageManager::callAsync([weak, key, slot] {
            if (auto impl = weak.lock()) {
                impl->closeEditor(key);
                if (impl->editorClosed) impl->editorClosed(slot);
            }
        });
    });
    impl_->editorSlots[key] = slot;
    return true;
}

bool JucePluginHost::editorOpen(const InsertSlot& slot) const { return impl_->editors.count(keyOf(slot)) > 0; }

void JucePluginHost::closeAllEditors() {
    impl_->editors.clear();
    impl_->editorSlots.clear();
}

void JucePluginHost::releaseAll() {
    closeAllEditors();
    std::lock_guard lock(impl_->mutex);
    impl_->entries.clear();
}

}  // namespace lpc
```

Note the lifetime rule the code relies on: an editor window is always destroyed before the `PluginProcessor` it shows (`prune` and `acquire` close it first; `releaseAll` closes editors before clearing entries; the `EditorWindow` destructor deletes the editor, which must happen while the processor is alive). The processor's `shared_ptr` may also be held by a `TrackConfig` in the engine, which only extends the processor's life.

- [ ] **Step 8: Build and run the platform tests**

Run: `cmake --build build-plugin --config Debug && ctest --test-dir build-plugin -C Debug --output-on-failure -R lpc_juce_tests`
Expected: all `[juce]` tests pass. If the VST3 fails to scan with "not found", check the `LPC_TEST_VST3_DIR` compile definition points at the `Debug/VST3` folder and that `lpc_test_gain` was built in the same configuration.

- [ ] **Step 9: Build everything that links `lpc_juce_device`, to catch duplicate-symbol link errors**

Run: `cmake --build build-plugin --config Debug --target lpc-cli && cmake --build build-ui --config Debug`
Expected: both link. (If `build-ui` fails with duplicate JUCE module symbols, a JUCE module is linked from two targets: remove the extra link from the consumer, keep it only on `lpc_juce_device`.)

- [ ] **Step 10: Commit**

```bash
git add CMakeLists.txt platform
git commit -m "feat(platform): JUCE plug-in host with non-blocking loads, state capture and editor windows"
```

---

### Task 9: Scan cache (Core) and scanner (platform)

**Files:**
- Create: `core/include/lpc/plugin_catalogue.h`, `core/src/plugin_catalogue.cpp`, `tests/test_plugin_catalogue.cpp`, `tools/plugin-scanner/CMakeLists.txt`, `tools/plugin-scanner/main.cpp`, `platform/juce/plugin_scanner.h/.cpp`, `platform/juce/tests/test_scanner.cpp`
- Modify: `CMakeLists.txt`, `platform/juce/CMakeLists.txt`, `platform/juce/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 5 (`PluginDescriptor`), Task 8 (`JucePluginHost::setCatalogue`).
- Produces (Core, namespace `lpc`):

```cpp
enum class ScanStatus { Ok, Failed };
struct ScanEntry { std::string path; std::int64_t mtime = 0, size = 0; ScanStatus status = ScanStatus::Ok; std::string reason;
                   std::vector<PluginDescriptor> descriptors; };
struct FileInfo { std::string path; std::int64_t mtime = 0, size = 0; };
class PluginCatalogue {
public:
    static PluginCatalogue load(const std::filesystem::path& file);   // missing or corrupt file: empty
    void save(const std::filesystem::path& file) const;               // writes a temp file, then renames; throws on I/O error
    const std::vector<ScanEntry>& entries() const;
    const ScanEntry* find(const std::string& path) const;
    void set(ScanEntry entry);
    void remove(const std::string& path);
    void clear();
    std::vector<PluginDescriptor> descriptors() const;                // of the Ok entries
};
enum class ScanMode { NewAndChanged, Failed, All };
// Which of `present` need scanning. Entries of files that are gone are removed from `catalogue`.
std::vector<FileInfo> filesToScan(PluginCatalogue& catalogue, const std::vector<FileInfo>& present, ScanMode mode);
std::filesystem::path appConfigDir();
```

Platform (namespace `lpc`):

```cpp
class PluginScanner {
public:
    struct Options { std::filesystem::path scannerExe, cacheFile; std::vector<std::filesystem::path> folders; int timeoutMs = 30000; };
    explicit PluginScanner(Options options);
    ~PluginScanner();                                   // stops and joins
    void start(ScanMode mode);                          // no-op while running
    void wait();                                        // blocks until the running scan ends (CLI and tests)
    bool running() const;
    int done() const; int total() const;
    PluginCatalogue snapshot() const;                   // thread-safe copy
    void setChangedListener(std::function<void()>);     // from the scan thread, after each file and at the end
    static std::vector<std::filesystem::path> defaultFolders();
};
```

The scanner executable `lpc-plugin-scanner <file.vst3>` prints one JSON line to stdout and exits 0: `{"ok":true,"descriptors":[{"id","name","vendor","version","native"}...]}` or `{"ok":false,"reason":"..."}`; it exits non-zero or dies when the plug-in crashes it.

- [ ] **Step 1: Write the failing Core tests**

`tests/test_plugin_catalogue.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include "lpc/plugin_catalogue.h"
#include "temp_dir.h"

using namespace lpc;

namespace {
ScanEntry ok(const std::string& path, std::int64_t mtime = 10, std::int64_t size = 100) {
    ScanEntry e;
    e.path = path;
    e.mtime = mtime;
    e.size = size;
    e.descriptors.push_back(PluginDescriptor{"vst3:00000000000000000000000000000001", "Gain", "Acme", "1.0", path, "<xml/>"});
    return e;
}
ScanEntry failed(const std::string& path, const std::string& reason = "timeout") {
    ScanEntry e;
    e.path = path;
    e.mtime = 10;
    e.size = 100;
    e.status = ScanStatus::Failed;
    e.reason = reason;
    return e;
}
}  // namespace

TEST_CASE("catalogue: save and load round trip", "[plugin][catalogue]") {
    test::TempDir tmp;
    PluginCatalogue c;
    c.set(ok("a.vst3"));
    c.set(failed("b.vst3", "unsupported layout"));
    c.save(tmp.path / "plugins.json");
    const PluginCatalogue back = PluginCatalogue::load(tmp.path / "plugins.json");
    REQUIRE(back.entries().size() == 2);
    REQUIRE(back.find("a.vst3")->descriptors[0].name == "Gain");
    REQUIRE(back.find("b.vst3")->status == ScanStatus::Failed);
    REQUIRE(back.find("b.vst3")->reason == "unsupported layout");
    REQUIRE(back.descriptors().size() == 1);
}

TEST_CASE("catalogue: a missing or corrupt cache is empty, not an error", "[plugin][catalogue]") {
    test::TempDir tmp;
    REQUIRE(PluginCatalogue::load(tmp.path / "nope.json").entries().empty());
    std::ofstream(tmp.path / "bad.json") << "{ not json";
    REQUIRE(PluginCatalogue::load(tmp.path / "bad.json").entries().empty());
    std::ofstream(tmp.path / "odd.json") << R"({"version":1,"entries":[{"path":5}]})";
    REQUIRE(PluginCatalogue::load(tmp.path / "odd.json").entries().empty());
}

TEST_CASE("catalogue: set replaces by path, remove and clear", "[plugin][catalogue]") {
    PluginCatalogue c;
    c.set(ok("a.vst3"));
    c.set(ok("a.vst3", 20, 200));
    REQUIRE(c.entries().size() == 1);
    REQUIRE(c.find("a.vst3")->mtime == 20);
    c.remove("a.vst3");
    REQUIRE(c.entries().empty());
    c.set(ok("b.vst3"));
    c.clear();
    REQUIRE(c.entries().empty());
}

TEST_CASE("filesToScan: new, changed, failed and vanished files", "[plugin][catalogue]") {
    PluginCatalogue c;
    c.set(ok("same.vst3", 10, 100));
    c.set(ok("changed.vst3", 10, 100));
    c.set(failed("bad.vst3"));
    c.set(ok("gone.vst3"));
    const std::vector<FileInfo> present = {{"same.vst3", 10, 100}, {"changed.vst3", 11, 100}, {"bad.vst3", 10, 100}, {"new.vst3", 1, 1}};

    auto names = [](const std::vector<FileInfo>& v) {
        std::vector<std::string> n;
        for (const auto& f : v) n.push_back(f.path);
        return n;
    };
    PluginCatalogue a = c;
    REQUIRE(names(filesToScan(a, present, ScanMode::NewAndChanged)) == std::vector<std::string>{"changed.vst3", "new.vst3"});
    REQUIRE(a.find("gone.vst3") == nullptr);  // vanished files are dropped
    PluginCatalogue b = c;
    REQUIRE(names(filesToScan(b, present, ScanMode::Failed)) == std::vector<std::string>{"bad.vst3"});
    PluginCatalogue all = c;
    REQUIRE(filesToScan(all, present, ScanMode::All).size() == 4);
    REQUIRE(all.entries().empty());  // All starts from nothing
}

TEST_CASE("catalogue: a changed file that failed before is retried, an unchanged failed one is not", "[plugin][catalogue]") {
    PluginCatalogue c;
    c.set(failed("bad.vst3"));
    REQUIRE(filesToScan(c, {{"bad.vst3", 10, 100}}, ScanMode::NewAndChanged).empty());
    REQUIRE(filesToScan(c, {{"bad.vst3", 99, 100}}, ScanMode::NewAndChanged).size() == 1);  // it was updated: try again
}

TEST_CASE("appConfigDir ends with JAD Daw", "[plugin][catalogue]") {
    REQUIRE(appConfigDir().filename() == "JAD Daw");
}
```

(`tests/temp_dir.h` provides `test::TempDir` with `.path`, as used by `test_offline_render.cpp`.)

- [ ] **Step 2: Run to see failure**

Run: `cmake --build build-core --config Debug --target lpc_tests`
Expected: `lpc/plugin_catalogue.h` not found.

- [ ] **Step 3: Implement the catalogue**

`core/include/lpc/plugin_catalogue.h`:

```cpp
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "lpc/plugin_host.h"

namespace lpc {

enum class ScanStatus { Ok, Failed };

struct ScanEntry {
    std::string path;
    std::int64_t mtime = 0, size = 0;
    ScanStatus status = ScanStatus::Ok;
    std::string reason;  // why a file failed
    std::vector<PluginDescriptor> descriptors;
};

struct FileInfo {
    std::string path;
    std::int64_t mtime = 0, size = 0;
};

enum class ScanMode { NewAndChanged, Failed, All };

// The result of scanning plug-in files, kept in a JSON file between runs.
class PluginCatalogue {
public:
    static PluginCatalogue load(const std::filesystem::path& file);  // a missing or unreadable file gives an empty catalogue
    void save(const std::filesystem::path& file) const;              // temp file then rename; throws std::runtime_error on I/O errors
    const std::vector<ScanEntry>& entries() const { return entries_; }
    const ScanEntry* find(const std::string& path) const;
    void set(ScanEntry entry);
    void remove(const std::string& path);
    void clear() { entries_.clear(); }
    std::vector<PluginDescriptor> descriptors() const;

private:
    std::vector<ScanEntry> entries_;
};

// Which of `present` need scanning. Entries of files that are gone are removed from the catalogue.
//   NewAndChanged: files not in the catalogue or whose mtime or size changed (failed ones too, when changed).
//   Failed:        files whose entry failed.
//   All:           every file; the catalogue is emptied first.
std::vector<FileInfo> filesToScan(PluginCatalogue& catalogue, const std::vector<FileInfo>& present, ScanMode mode);

// %LOCALAPPDATA%/JAD/JAD Daw on Windows, $XDG_CONFIG_HOME or ~/.config then /JAD/JAD Daw elsewhere.
std::filesystem::path appConfigDir();

}  // namespace lpc
```

`core/src/plugin_catalogue.cpp`:

```cpp
#define _CRT_SECURE_NO_WARNINGS
#include "lpc/plugin_catalogue.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace lpc {

namespace {

nlohmann::json toJson(const PluginDescriptor& d) {
    return {{"id", d.id}, {"name", d.name}, {"vendor", d.vendor}, {"version", d.version}, {"path", d.path}, {"native", d.native}};
}
PluginDescriptor descriptorFrom(const nlohmann::json& j) {
    PluginDescriptor d;
    j.at("id").get_to(d.id);
    j.at("name").get_to(d.name);
    j.at("vendor").get_to(d.vendor);
    j.at("version").get_to(d.version);
    j.at("path").get_to(d.path);
    j.at("native").get_to(d.native);
    return d;
}

}  // namespace

const ScanEntry* PluginCatalogue::find(const std::string& path) const {
    for (const ScanEntry& e : entries_)
        if (e.path == path) return &e;
    return nullptr;
}

void PluginCatalogue::set(ScanEntry entry) {
    for (ScanEntry& e : entries_)
        if (e.path == entry.path) {
            e = std::move(entry);
            return;
        }
    entries_.push_back(std::move(entry));
}

void PluginCatalogue::remove(const std::string& path) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const ScanEntry& e) { return e.path == path; }), entries_.end());
}

std::vector<PluginDescriptor> PluginCatalogue::descriptors() const {
    std::vector<PluginDescriptor> out;
    for (const ScanEntry& e : entries_)
        if (e.status == ScanStatus::Ok) out.insert(out.end(), e.descriptors.begin(), e.descriptors.end());
    return out;
}

PluginCatalogue PluginCatalogue::load(const std::filesystem::path& file) {
    PluginCatalogue c;
    try {
        std::ifstream in(file, std::ios::binary);
        if (!in) return c;
        const nlohmann::json doc = nlohmann::json::parse(in);
        if (doc.at("version").get<int>() != 1) return c;
        std::vector<ScanEntry> entries;
        for (const auto& j : doc.at("entries")) {
            ScanEntry e;
            j.at("path").get_to(e.path);
            j.at("mtime").get_to(e.mtime);
            j.at("size").get_to(e.size);
            e.status = j.at("status").get<std::string>() == "ok" ? ScanStatus::Ok : ScanStatus::Failed;
            e.reason = j.value("reason", std::string());
            for (const auto& d : j.at("descriptors")) e.descriptors.push_back(descriptorFrom(d));
            entries.push_back(std::move(e));
        }
        c.entries_ = std::move(entries);
    } catch (const std::exception&) {
        c.entries_.clear();  // a cache that cannot be read is rebuilt by the next scan
    }
    return c;
}

void PluginCatalogue::save(const std::filesystem::path& file) const {
    nlohmann::json entries = nlohmann::json::array();
    for (const ScanEntry& e : entries_) {
        nlohmann::json descriptors = nlohmann::json::array();
        for (const PluginDescriptor& d : e.descriptors) descriptors.push_back(toJson(d));
        entries.push_back({{"path", e.path},
                           {"mtime", e.mtime},
                           {"size", e.size},
                           {"status", e.status == ScanStatus::Ok ? "ok" : "failed"},
                           {"reason", e.reason},
                           {"descriptors", descriptors}});
    }
    const nlohmann::json doc = {{"version", 1}, {"entries", entries}};
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
        out << doc.dump(1);
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        throw std::runtime_error("cannot replace " + file.string());
    }
}

std::vector<FileInfo> filesToScan(PluginCatalogue& catalogue, const std::vector<FileInfo>& present, ScanMode mode) {
    if (mode == ScanMode::All) {
        catalogue.clear();
        return present;
    }
    std::unordered_set<std::string> presentPaths;
    for (const FileInfo& f : present) presentPaths.insert(f.path);
    std::vector<std::string> gone;
    for (const ScanEntry& e : catalogue.entries())
        if (!presentPaths.count(e.path)) gone.push_back(e.path);
    for (const std::string& p : gone) catalogue.remove(p);

    std::vector<FileInfo> out;
    for (const FileInfo& f : present) {
        const ScanEntry* e = catalogue.find(f.path);
        const bool changed = !e || e->mtime != f.mtime || e->size != f.size;
        if (mode == ScanMode::NewAndChanged ? changed : (e && e->status == ScanStatus::Failed)) out.push_back(f);
    }
    return out;
}

std::filesystem::path appConfigDir() {
#ifdef _WIN32
    if (const char* v = std::getenv("LOCALAPPDATA")) return std::filesystem::path(v) / "JAD" / "JAD Daw";
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME")) return std::filesystem::path(x) / "JAD" / "JAD Daw";
    if (const char* h = std::getenv("HOME")) return std::filesystem::path(h) / ".config" / "JAD" / "JAD Daw";
#endif
    return std::filesystem::path(".") / "JAD Daw";
}

}  // namespace lpc
```

- [ ] **Step 4: Run the Core tests**

Run: `cmake --build build-core --config Debug --target lpc_tests && ctest --test-dir build-core -C Debug --output-on-failure -R catalogue`
Expected: pass. (`[plugin][catalogue]` tests; the whole suite also passes.)

- [ ] **Step 5: Commit the Core part**

```bash
git add core tests/test_plugin_catalogue.cpp
git commit -m "feat(core): plug-in scan cache and rescan planning"
```

- [ ] **Step 6: Write the scanner executable**

`tools/plugin-scanner/CMakeLists.txt`:

```cmake
juce_add_console_app(lpc-plugin-scanner PRODUCT_NAME "lpc-plugin-scanner")
target_sources(lpc-plugin-scanner PRIVATE main.cpp)
target_compile_definitions(lpc-plugin-scanner PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0 JUCE_PLUGINHOST_VST3=1 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(lpc-plugin-scanner PRIVATE
    juce::juce_audio_processors juce::juce_gui_basics juce::juce_events juce::juce_core
    juce::juce_recommended_config_flags)
```

In the root `CMakeLists.txt` JUCE block, after `add_subdirectory(platform/juce)` add:

```cmake
    add_subdirectory(tools/plugin-scanner)
```

`tools/plugin-scanner/main.cpp`:

```cpp
// lpc-plugin-scanner <file.vst3>: loads one plug-in file in its own process, prints one JSON line and exits.
// A plug-in that crashes or hangs takes only this process with it; the parent sees a non-zero exit code or a timeout.
#include <iostream>

#include <juce_audio_processors/juce_audio_processors.h>
#include <nlohmann/json.hpp>

namespace {

std::string md5Id(const juce::PluginDescription& d) {
    const juce::String key = d.name + "|" + d.manufacturerName + "|" + juce::String(d.uniqueId) + "|" + juce::String(d.deprecatedUid);
    return "vst3:" + juce::MD5(key.toUTF8()).toHexString().toLowerCase().toStdString();
}

int fail(const std::string& reason) {
    std::cout << nlohmann::json{{"ok", false}, {"reason", reason}}.dump() << std::endl;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, juce::String::fromUTF8(argv[1]));
    if (found.isEmpty()) return fail("no VST3 plug-in in this file");

    nlohmann::json descriptors = nlohmann::json::array();
    std::string firstProblem;
    for (const juce::PluginDescription* desc : found) {
        if (desc->isInstrument) {
            if (firstProblem.empty()) firstProblem = "instruments are not supported yet";
            continue;
        }
        juce::String error;
        std::unique_ptr<juce::AudioPluginInstance> instance = format.createPluginInstance(*desc, 48000.0, 512, error);
        if (!instance) {
            if (firstProblem.empty()) firstProblem = "cannot load: " + error.toStdString();
            continue;
        }
        juce::AudioProcessor::BusesLayout layout;
        for (int i = 0; i < instance->getBusCount(true); ++i)
            layout.inputBuses.add(i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
        for (int i = 0; i < instance->getBusCount(false); ++i)
            layout.outputBuses.add(i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
        if (instance->getBusCount(true) < 1 || instance->getBusCount(false) < 1 || !instance->checkBusesLayoutSupported(layout)) {
            if (firstProblem.empty()) firstProblem = "unsupported layout (needs stereo in and out)";
            continue;
        }
        descriptors.push_back({{"id", md5Id(*desc)},
                               {"name", desc->name.toStdString()},
                               {"vendor", desc->manufacturerName.toStdString()},
                               {"version", desc->version.toStdString()},
                               {"native", desc->createXml()->toString().toStdString()}});
    }
    if (descriptors.empty()) return fail(firstProblem.empty() ? "no usable plug-in" : firstProblem);
    std::cout << nlohmann::json{{"ok", true}, {"descriptors", descriptors}}.dump() << std::endl;
    return 0;
}
```

`nlohmann_json` is available to the scanner because the root fetched it; add `nlohmann_json::nlohmann_json` to the scanner's `target_link_libraries` in its CMake file.

The test plug-in's id in Task 8's tests was hard coded (`...0001`); the real id is the MD5 above. That is fine: Task 8 tests build the catalogue by hand, and Task 9 tests use the scanner's ids.

- [ ] **Step 7: Write `plugin_scanner.h/.cpp`**

`platform/juce/plugin_scanner.h`:

```cpp
#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "lpc/plugin_catalogue.h"

namespace lpc {

// Scans VST3 files one by one in lpc-plugin-scanner child processes and keeps the result in a JSON cache.
class PluginScanner {
public:
    struct Options {
        std::filesystem::path scannerExe, cacheFile;
        std::vector<std::filesystem::path> folders;
        int timeoutMs = 30000;
    };
    explicit PluginScanner(Options options);
    ~PluginScanner();
    PluginScanner(const PluginScanner&) = delete;
    PluginScanner& operator=(const PluginScanner&) = delete;

    void start(ScanMode mode);  // no-op while a scan is running
    void wait();                // blocks until the running scan has ended
    bool running() const { return running_.load(); }
    int done() const { return done_.load(); }
    int total() const { return total_.load(); }
    PluginCatalogue snapshot() const;
    void setChangedListener(std::function<void()> listener);  // called from the scan thread

    static std::vector<std::filesystem::path> defaultFolders();

private:
    void run(ScanMode mode);
    ScanEntry scanFile(const FileInfo& file);
    void notify();

    Options options_;
    mutable std::mutex mutex_;  // catalogue_ and listener_
    PluginCatalogue catalogue_;
    std::function<void()> listener_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<int> done_{0}, total_{0};
};

}  // namespace lpc
```

`platform/juce/plugin_scanner.cpp`:

```cpp
#define _CRT_SECURE_NO_WARNINGS
#include "plugin_scanner.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include <juce_core/juce_core.h>
#include <nlohmann/json.hpp>

namespace lpc {

namespace {

namespace fs = std::filesystem;

std::int64_t seconds(const fs::file_time_type& t) {
    return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
}

// A VST3 is a single file or a folder (bundle); the file that changes with an update is the binary inside it.
FileInfo statPlugin(const fs::path& path) {
    FileInfo info;
    info.path = path.string();
    std::error_code ec;
    fs::path probe = path;
    if (fs::is_directory(path, ec)) {
        for (const auto& entry : fs::directory_iterator(path / "Contents" / "x86_64-win", ec)) {
            if (entry.path().extension() == ".vst3") {
                probe = entry.path();
                break;
            }
        }
    }
    info.mtime = seconds(fs::last_write_time(probe, ec));
    info.size = fs::is_regular_file(probe, ec) ? static_cast<std::int64_t>(fs::file_size(probe, ec)) : 0;
    return info;
}

std::vector<FileInfo> findPlugins(const std::vector<fs::path>& folders) {
    std::vector<FileInfo> out;
    for (const fs::path& folder : folders) {
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) continue;
        for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
             it.increment(ec)) {
            if (it->path().extension() != ".vst3") continue;
            if (it->is_directory(ec)) it.disable_recursion_pending();  // a bundle: its insides are not plug-ins of their own
            out.push_back(statPlugin(it->path()));
        }
    }
    std::sort(out.begin(), out.end(), [](const FileInfo& a, const FileInfo& b) { return a.path < b.path; });
    out.erase(std::unique(out.begin(), out.end(), [](const FileInfo& a, const FileInfo& b) { return a.path == b.path; }), out.end());
    return out;
}

}  // namespace

PluginScanner::PluginScanner(Options options) : options_(std::move(options)), catalogue_(PluginCatalogue::load(options_.cacheFile)) {}

PluginScanner::~PluginScanner() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

std::vector<fs::path> PluginScanner::defaultFolders() {
    std::vector<fs::path> out;
    if (const char* common = std::getenv("COMMONPROGRAMFILES")) out.push_back(fs::path(common) / "VST3");
    if (const char* local = std::getenv("LOCALAPPDATA")) out.push_back(fs::path(local) / "Programs" / "Common" / "VST3");
    return out;
}

PluginCatalogue PluginScanner::snapshot() const {
    std::lock_guard lock(mutex_);
    return catalogue_;
}

void PluginScanner::setChangedListener(std::function<void()> listener) {
    std::lock_guard lock(mutex_);
    listener_ = std::move(listener);
}

void PluginScanner::notify() {
    std::function<void()> listener;
    {
        std::lock_guard lock(mutex_);
        listener = listener_;
    }
    if (listener) listener();
}

void PluginScanner::start(ScanMode mode) {
    if (running_.exchange(true)) return;
    if (thread_.joinable()) thread_.join();
    cancel_ = false;
    done_ = 0;
    total_ = 0;
    thread_ = std::thread([this, mode] { run(mode); });
}

void PluginScanner::wait() {
    if (thread_.joinable()) thread_.join();
}

ScanEntry PluginScanner::scanFile(const FileInfo& file) {
    ScanEntry entry;
    entry.path = file.path;
    entry.mtime = file.mtime;
    entry.size = file.size;
    entry.status = ScanStatus::Failed;

    juce::ChildProcess child;
    if (!child.start(juce::StringArray{juce::String(options_.scannerExe.string()), juce::String::fromUTF8(file.path.c_str())},
                     juce::ChildProcess::wantStdOut)) {
        entry.reason = "cannot start the scanner";
        return entry;
    }
    if (!child.waitForProcessToFinish(options_.timeoutMs)) {
        child.kill();
        entry.reason = "timed out";
        return entry;
    }
    const std::string output = child.readAllProcessOutput().toStdString();
    const std::uint32_t exitCode = child.getExitCode();
    try {
        const nlohmann::json j = nlohmann::json::parse(output);
        if (j.at("ok").get<bool>()) {
            for (const auto& d : j.at("descriptors")) {
                PluginDescriptor pd;
                pd.id = d.at("id").get<std::string>();
                pd.name = d.at("name").get<std::string>();
                pd.vendor = d.at("vendor").get<std::string>();
                pd.version = d.at("version").get<std::string>();
                pd.native = d.at("native").get<std::string>();
                pd.path = file.path;
                entry.descriptors.push_back(std::move(pd));
            }
            entry.status = ScanStatus::Ok;
            return entry;
        }
        entry.reason = j.at("reason").get<std::string>();
    } catch (const std::exception&) {
        entry.reason = exitCode != 0 ? "crashed (exit code " + std::to_string(exitCode) + ")" : "no valid answer";
    }
    return entry;
}

void PluginScanner::run(ScanMode mode) {
    std::vector<FileInfo> todo;
    {
        PluginCatalogue working = snapshot();
        todo = filesToScan(working, findPlugins(options_.folders), mode);
        std::lock_guard lock(mutex_);
        catalogue_ = std::move(working);  // vanished files are gone, `All` starts empty
    }
    total_ = static_cast<int>(todo.size());
    notify();
    for (const FileInfo& file : todo) {
        if (cancel_) break;
        ScanEntry entry = scanFile(file);
        {
            std::lock_guard lock(mutex_);
            catalogue_.set(std::move(entry));
            try {
                catalogue_.save(options_.cacheFile);
            } catch (const std::exception&) {
                // the scan result stays valid in memory; the next run scans again
            }
        }
        ++done_;
        notify();
    }
    {
        std::lock_guard lock(mutex_);
        try {
            catalogue_.save(options_.cacheFile);
        } catch (const std::exception&) {
        }
    }
    running_ = false;
    notify();
}

}  // namespace lpc
```

`running_` becomes false at the end of `run` while `thread_` is still joinable: `start()` joins before reusing it, and `wait()` joins; the destructor joins.

Add `plugin_scanner.cpp` to the sources of `lpc_juce_device` in `platform/juce/CMakeLists.txt`.

- [ ] **Step 8: Write the failing scanner tests**

Append to `platform/juce/tests/CMakeLists.txt`: change the first line to `add_executable(lpc_juce_tests test_plugin_host.cpp test_scanner.cpp)`, add `add_dependencies(lpc_juce_tests lpc_test_gain lpc-plugin-scanner)` (replacing the previous `add_dependencies`) and

```cmake
target_compile_definitions(lpc_juce_tests PRIVATE LPC_SCANNER_EXE="$<TARGET_FILE:lpc-plugin-scanner>")
```

`platform/juce/tests/test_scanner.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <random>

#include "juce_plugin_host.h"
#include "plugin_scanner.h"

using namespace lpc;
namespace fs = std::filesystem;

namespace {

struct TempFolder {
    fs::path path;
    TempFolder() {
        std::mt19937_64 rng(std::random_device{}());
        path = fs::temp_directory_path() / ("lpc-scan-" + std::to_string(rng()));
        fs::create_directories(path);
    }
    ~TempFolder() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

PluginScanner::Options options(const fs::path& plugins, const fs::path& cache) {
    PluginScanner::Options o;
    o.scannerExe = LPC_SCANNER_EXE;
    o.cacheFile = cache;
    o.folders = {plugins};
    o.timeoutMs = 60000;
    return o;
}

void copyTestPlugin(const fs::path& to) {
    fs::copy(fs::path(LPC_TEST_VST3_DIR) / "LPC Test Gain.vst3", to / "LPC Test Gain.vst3", fs::copy_options::recursive);
}

}  // namespace

TEST_CASE("scanner: finds the test plug-in, blocklists a broken file, caches both", "[scan]") {
    TempFolder plugins, cache;
    copyTestPlugin(plugins.path);
    std::ofstream(plugins.path / "broken.vst3", std::ios::binary) << "this is not a plug-in";

    PluginScanner scanner(options(plugins.path, cache.path / "plugins.json"));
    scanner.start(ScanMode::NewAndChanged);
    scanner.wait();

    const PluginCatalogue c = scanner.snapshot();
    REQUIRE(c.entries().size() == 2);
    const auto good = c.descriptors();
    REQUIRE(good.size() == 1);
    REQUIRE(good[0].name == "LPC Test Gain");
    REQUIRE(good[0].id.size() == 5 + 32);
    const ScanEntry* broken = c.find((plugins.path / "broken.vst3").string());
    REQUIRE(broken);
    REQUIRE(broken->status == ScanStatus::Failed);
    REQUIRE_FALSE(broken->reason.empty());
    REQUIRE(fs::exists(cache.path / "plugins.json"));
}

TEST_CASE("scanner: a second scan leaves unchanged files alone and `Failed` retries only failures", "[scan]") {
    TempFolder plugins, cache;
    copyTestPlugin(plugins.path);
    std::ofstream(plugins.path / "broken.vst3", std::ios::binary) << "nope";

    PluginScanner first(options(plugins.path, cache.path / "plugins.json"));
    first.start(ScanMode::NewAndChanged);
    first.wait();

    PluginScanner second(options(plugins.path, cache.path / "plugins.json"));  // loads the cache from disk
    REQUIRE(second.snapshot().entries().size() == 2);
    second.start(ScanMode::NewAndChanged);
    second.wait();
    REQUIRE(second.total() == 0);

    second.start(ScanMode::Failed);
    second.wait();
    REQUIRE(second.total() == 1);
}

TEST_CASE("scanner: a hung or crashing scanner is reported, not fatal", "[scan]") {
    TempFolder plugins, cache;
    std::ofstream(plugins.path / "x.vst3", std::ios::binary) << "x";
    PluginScanner::Options o = options(plugins.path, cache.path / "plugins.json");
    o.scannerExe = fs::path(LPC_TEST_VST3_DIR) / "does-not-exist.exe";
    PluginScanner scanner(o);
    scanner.start(ScanMode::NewAndChanged);
    scanner.wait();
    const ScanEntry* e = scanner.snapshot().find((plugins.path / "x.vst3").string());
    REQUIRE(e);
    REQUIRE(e->status == ScanStatus::Failed);
}

TEST_CASE("scanner: a scanned plug-in loads through the host", "[scan][plugin]") {
    TempFolder plugins, cache;
    copyTestPlugin(plugins.path);
    PluginScanner scanner(options(plugins.path, cache.path / "plugins.json"));
    scanner.start(ScanMode::All);
    scanner.wait();
    JucePluginHost host;
    host.setCatalogue(scanner.snapshot().descriptors());
    ProcessorRef ref;
    ref.processorId = host.catalogue().at(0).id;
    REQUIRE(host.acquire(InsertSlot{Uuid{9, 9}, 0}, ref, 48000.0, 512) != nullptr);
}
```

- [ ] **Step 9: Build and run**

Run: `cmake -S . -B build-plugin -G "Visual Studio 17 2022" -A x64 -DLPC_BUILD_PLUGIN_TESTS=ON && cmake --build build-plugin --config Debug && ctest --test-dir build-plugin -C Debug --output-on-failure`
Expected: all tests pass (Core suite, `[juce]`, `[scan]`).

- [ ] **Step 10: Commit**

```bash
git add CMakeLists.txt platform tools/plugin-scanner
git commit -m "feat(platform): plug-in scanner child process with cache, blocklist and rescan modes"
```

---

### Task 10: `lpc-cli render` with plug-ins

**Files:**
- Modify: `tools/lpc-cli/cli_commands.h`, `tools/lpc-cli/cli_commands.cpp`, `tools/lpc-cli/main.cpp`, `tools/lpc-cli/CMakeLists.txt`
- Test: manual (needs a real scan), plus the existing `tests/test_cli.cpp` must still pass

**Interfaces:**
- Consumes: Tasks 6, 8, 9.
- Produces: `using PluginHostFactory = std::function<std::shared_ptr<lpc::IPluginHost>()>; void setPluginHostFactory(PluginHostFactory);` in `cli_commands.h` (namespace of the existing declarations). `render` creates a host through the factory only when the project has a `vst3:` insert.

- [ ] **Step 1: Read the CLI sources you are about to edit**

Read `tools/lpc-cli/cli_commands.h`, the `cmdRender` function in `cli_commands.cpp` (around line 117-150) and `tools/lpc-cli/main.cpp`; note the namespace and how `RenderOptions` is built.

- [ ] **Step 2: Add the factory hook**

`tools/lpc-cli/cli_commands.h`: add `#include <functional>`, `#include <memory>`, `#include "lpc/plugin_host.h"` and in the same namespace as the other declarations:

```cpp
// Supplies the plug-in host used by `render`. Set by builds that can host plug-ins; unset otherwise.
using PluginHostFactory = std::function<std::shared_ptr<lpc::IPluginHost>()>;
void setPluginHostFactory(PluginHostFactory factory);
```

`tools/lpc-cli/cli_commands.cpp`: near the top of the namespace add

```cpp
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
```

(include `"lpc/processor_ids.h"`). In `cmdRender`, just before `renderOffline(project, media, options)`:

```cpp
    std::shared_ptr<lpc::IPluginHost> plugins;
    if (usesPlugins(project)) {
        if (!gPluginHostFactory) err << "warning: this build cannot host plug-ins; they are skipped\n";
        else plugins = gPluginHostFactory();
        options.plugins = plugins.get();
    }
```

- [ ] **Step 3: Install the factory in the JUCE build**

`tools/lpc-cli/main.cpp` (the JUCE `main`): before dispatching commands add (adapt names to the file; it already includes the JUCE headers it needs):

```cpp
#include "juce_plugin_host.h"
#include "lpc/plugin_catalogue.h"
#include "plugin_scanner.h"

namespace {
std::shared_ptr<lpc::IPluginHost> makePluginHost() {
    const std::filesystem::path cache = lpc::appConfigDir() / "plugins.json";
    lpc::PluginScanner::Options o;
    o.scannerExe = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("lpc-plugin-scanner.exe").getFullPathName().toStdString();
    o.cacheFile = cache;
    o.folders = lpc::PluginScanner::defaultFolders();
    lpc::PluginScanner scanner(o);
    scanner.start(lpc::ScanMode::NewAndChanged);  // cheap when the cache is current
    scanner.wait();
    auto host = std::make_shared<lpc::JucePluginHost>();
    host->setCatalogue(scanner.snapshot().descriptors());
    return host;
}
}  // namespace
```

and, early in `main`, `juce::ScopedJuceInitialiser_GUI juceInit;` (if the file does not already create a JUCE initialiser) and `lpc::cli::setPluginHostFactory(makePluginHost);` (use the real namespace of `setPluginHostFactory`). Because the render runs on the main thread, which is the message thread, `acquire` loads synchronously.

`tools/lpc-cli/CMakeLists.txt`: add `add_dependencies(lpc-cli lpc-plugin-scanner)` and a post-build copy so the scanner sits next to the CLI:

```cmake
    add_dependencies(lpc-cli lpc-plugin-scanner)
    add_custom_command(TARGET lpc-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:lpc-plugin-scanner> $<TARGET_FILE_DIR:lpc-cli>)
```

(inside the `if(LPC_WITH_JUCE)` branch).

- [ ] **Step 4: Build and run the existing tests**

Run: `cmake --build build-plugin --config Debug && ctest --test-dir build-plugin -C Debug --output-on-failure`
Expected: all pass.

- [ ] **Step 5: Check by hand with the test plug-in**

Run:
```bash
build-plugin/tools/lpc-cli/Debug/lpc-cli.exe demo demo.lpc
```
then add the test plug-in to the demo (the file is JSON): copy `demo.lpc/project.json` aside, add to the first audio track's `strip.inserts` the object `{"processorId":"vst3:<id>","params":{},"state":"","label":"LPC Test Gain"}` where `<id>` is the id printed by the scanner (`build-plugin/tools/plugin-scanner/Debug/lpc-plugin-scanner.exe "<path to LPC Test Gain.vst3>"`), copy the test plug-in into `%LOCALAPPDATA%\Programs\Common\VST3\` for the scan to find it, then `lpc-cli render demo.lpc out.wav`.
Expected: the command succeeds and `out.wav` is the same length as without the plug-in (the 32-sample latency is compensated). Remove the test plug-in from `%LOCALAPPDATA%\Programs\Common\VST3\` afterwards.

- [ ] **Step 6: Commit**

```bash
git add tools/lpc-cli
git commit -m "feat(cli): render hosts plug-ins through the JUCE host"
```

---

### Task 11: UI bridge: `PluginsModel`, snapshot fields, controller wiring

**Files:**
- Create: `ui/bridge/plugins_model.h`, `ui/bridge/plugins_model.cpp`, `ui/tests/tst_plugins.cpp`
- Modify: `ui/CMakeLists.txt`, `ui/tests/CMakeLists.txt`, `ui/bridge/snapshot.h`, `ui/bridge/snapshot.cpp`, `ui/bridge/row_maps.h`, `ui/bridge/project_controller.h`, `ui/bridge/project_controller.cpp`, `ui/main.cpp`

**Interfaces:**
- Consumes: Tasks 7, 8, 9.
- Produces:
  - `jad::PluginRow { QString id, name, vendor, status, path, reason; }` (status `"ok"`, `"failed"`).
  - `jad::PluginsModel : QAbstractListModel` (QML-anonymous) with roles `id`, `name`, `vendor`, `status`, `path`, `reason`; properties `QStringList knownIds` (ids of status ok), `QVariantList menu` (`[{vendor, plugins:[{id,name}]}]` sorted by vendor then name), `bool scanning`, `QString scanText` (for example `"Scanning 3 of 12"` or `""`), `bool supported`; invokables `rescanNew()`, `rescanFailed()`, `rescanAll()`; C++ `setRows(std::vector<PluginRow>)`, `setScan(bool running, int done, int total)`, `setSupported(bool)`; signal `rescanRequested(int mode)` (0 new, 1 failed, 2 all).
  - `InsertRow` gains `QString label; bool plugin = false;`; row map keys `"label"` and `"plugin"`.
  - `ProjectController`: `Q_PROPERTY(jad::PluginsModel* plugins READ plugins CONSTANT)`, `Q_INVOKABLE void addPlugin(const QString& trackId, const QString& pluginId, const QString& label)`, `Q_INVOKABLE void openPluginEditor(const QString& trackId, int index)`, `Q_INVOKABLE void setInsertState(const QString& trackId, int index, const QString& state)`, and `commitPluginStates()` called by `saveProject()` and on editor close.

- [ ] **Step 1: Write the failing tests**

`ui/tests/tst_plugins.cpp`:

```cpp
#include <QSignalSpy>
#include <QtTest>

#include "bridge/plugins_model.h"
#include "bridge/row_maps.h"
#include "bridge/snapshot.h"

namespace {
jad::PluginRow row(const char* id, const char* name, const char* vendor, const char* status = "ok") {
    jad::PluginRow r;
    r.id = id;
    r.name = name;
    r.vendor = vendor;
    r.status = status;
    r.path = QString("C:/VST3/") + name + ".vst3";
    return r;
}
}  // namespace

class PluginsTest : public QObject {
    Q_OBJECT
private slots:
    void rowsAreExposedByRole() {
        jad::PluginsModel m;
        m.setRows({row("vst3:a", "Verb", "Acme"), row("vst3:b", "Broken", "Acme", "failed")});
        QCOMPARE(m.rowCount(), 2);
        const auto roles = m.roleNames();
        QCOMPARE(m.data(m.index(0), roles.key("name")).toString(), QString("Verb"));
        QCOMPARE(m.data(m.index(1), roles.key("status")).toString(), QString("failed"));
    }
    void onlyUsablePluginsAreKnownAndInTheMenu() {
        jad::PluginsModel m;
        m.setRows({row("vst3:a", "Verb", "Zed"), row("vst3:b", "Comp", "Acme"), row("vst3:c", "Alpha", "Acme"), row("vst3:d", "Bad", "Acme", "failed")});
        QCOMPARE(m.knownIds(), QStringList({"vst3:a", "vst3:b", "vst3:c"}));
        const QVariantList menu = m.menu();
        QCOMPARE(menu.size(), 2);
        QCOMPARE(menu[0].toMap().value("vendor").toString(), QString("Acme"));
        const QVariantList acme = menu[0].toMap().value("plugins").toList();
        QCOMPARE(acme.size(), 2);
        QCOMPARE(acme[0].toMap().value("name").toString(), QString("Alpha"));  // sorted by name
        QCOMPARE(acme[1].toMap().value("name").toString(), QString("Comp"));
        QCOMPARE(menu[1].toMap().value("vendor").toString(), QString("Zed"));
    }
    void changedIsEmittedWhenRowsChange() {
        jad::PluginsModel m;
        QSignalSpy spy(&m, &jad::PluginsModel::changed);
        m.setRows({row("vst3:a", "Verb", "Acme")});
        QCOMPARE(spy.count(), 1);
    }
    void scanStateText() {
        jad::PluginsModel m;
        QVERIFY(!m.scanning());
        QCOMPARE(m.scanText(), QString());
        m.setScan(true, 3, 12);
        QVERIFY(m.scanning());
        QCOMPARE(m.scanText(), QString("Scanning 3 of 12"));
        m.setScan(false, 12, 12);
        QCOMPARE(m.scanText(), QString());
    }
    void rescanInvokablesEmitTheMode() {
        jad::PluginsModel m;
        QSignalSpy spy(&m, &jad::PluginsModel::rescanRequested);
        m.rescanNew();
        m.rescanFailed();
        m.rescanAll();
        QCOMPARE(spy.count(), 3);
        QCOMPARE(spy.at(0).at(0).toInt(), 0);
        QCOMPARE(spy.at(1).at(0).toInt(), 1);
        QCOMPARE(spy.at(2).at(0).toInt(), 2);
    }
    void insertRowsCarryLabelAndPluginFlag() {
        jad::TrackRow t;
        t.id = "t";
        t.kind = "audio";
        t.name = "A";
        t.inserts.push_back({"builtin.gain", 2.0, "", false});
        t.inserts.push_back({"vst3:00112233445566778899aabbccddeeff", 0.0, "Verb", true});
        const QVariantMap map = jad::trackRowToMap(t);
        const QVariantList inserts = map.value("inserts").toList();
        QCOMPARE(inserts[0].toMap().value("plugin").toBool(), false);
        QCOMPARE(inserts[1].toMap().value("plugin").toBool(), true);
        QCOMPARE(inserts[1].toMap().value("label").toString(), QString("Verb"));
    }
};

QTEST_GUILESS_MAIN(PluginsTest)
#include "tst_plugins.moc"
```

Before writing the last test, open `ui/bridge/row_maps.h` and use the real name of the function that turns a `TrackRow` into a `QVariantMap` (the file's first function); replace `jad::trackRowToMap` accordingly. Note `InsertRow` initialisation order is `{processorId, gainDb, label, plugin}`.

- [ ] **Step 2: Run to see failure**

Add the test to `ui/tests/CMakeLists.txt`: `jad_add_qt_test(jad_plugins_tests tst_plugins.cpp)` and under `if(WIN32)` `jad_deploy_qt(jad_plugins_tests)`.
Run: `cmake -S . -B build-ui -A x64 -DLPC_BUILD_UI=ON "-DCMAKE_PREFIX_PATH=$(pwd)/.qt/6.8.3/msvc2022_64" && cmake --build build-ui --config Debug --target jad_plugins_tests`
Expected: `bridge/plugins_model.h` not found.

- [ ] **Step 3: Implement `PluginsModel`**

`ui/bridge/plugins_model.h`:

```cpp
#pragma once
#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <vector>

namespace jad {

struct PluginRow {
    QString id, name, vendor, status, path, reason;  // status: "ok" or "failed"
};

// The plug-in catalogue as the UI sees it: the Plug-in Manager table, the insert menu and the "is it installed" check.
class PluginsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QStringList knownIds READ knownIds NOTIFY changed)
    Q_PROPERTY(QVariantList menu READ menu NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(QString scanText READ scanText NOTIFY changed)
    Q_PROPERTY(bool supported READ supported NOTIFY changed)
public:
    explicit PluginsModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList knownIds() const { return knownIds_; }
    QVariantList menu() const { return menu_; }
    bool scanning() const { return scanning_; }
    QString scanText() const;
    bool supported() const { return supported_; }

    void setRows(std::vector<PluginRow> rows);
    void setScan(bool running, int done, int total);
    void setSupported(bool supported);

    Q_INVOKABLE void rescanNew() { emit rescanRequested(0); }
    Q_INVOKABLE void rescanFailed() { emit rescanRequested(1); }
    Q_INVOKABLE void rescanAll() { emit rescanRequested(2); }

signals:
    void changed();
    void rescanRequested(int mode);  // 0 new and changed, 1 failed, 2 all

private:
    enum Role { IdRole = Qt::UserRole + 1, NameRole, VendorRole, StatusRole, PathRole, ReasonRole };
    void rebuild();

    std::vector<PluginRow> rows_;
    QStringList knownIds_;
    QVariantList menu_;
    bool scanning_ = false;
    bool supported_ = false;
    int done_ = 0, total_ = 0;
};

}  // namespace jad
```

`ui/bridge/plugins_model.cpp`:

```cpp
#include "plugins_model.h"

#include <QMap>
#include <algorithm>

namespace jad {

QHash<int, QByteArray> PluginsModel::roleNames() const {
    return {{IdRole, "id"}, {NameRole, "name"}, {VendorRole, "vendor"}, {StatusRole, "status"}, {PathRole, "path"}, {ReasonRole, "reason"}};
}

QVariant PluginsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const PluginRow& r = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case IdRole: return r.id;
        case NameRole: return r.name;
        case VendorRole: return r.vendor;
        case StatusRole: return r.status;
        case PathRole: return r.path;
        case ReasonRole: return r.reason;
        default: return {};
    }
}

QString PluginsModel::scanText() const { return scanning_ ? QString("Scanning %1 of %2").arg(done_).arg(total_) : QString(); }

void PluginsModel::setRows(std::vector<PluginRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    rebuild();
    endResetModel();
    emit changed();
}

void PluginsModel::setScan(bool running, int done, int total) {
    if (running == scanning_ && done == done_ && total == total_) return;
    scanning_ = running;
    done_ = done;
    total_ = total;
    emit changed();
}

void PluginsModel::setSupported(bool supported) {
    if (supported == supported_) return;
    supported_ = supported;
    emit changed();
}

void PluginsModel::rebuild() {
    knownIds_.clear();
    QMap<QString, QList<PluginRow>> byVendor;  // QMap keeps the vendors sorted
    for (const PluginRow& r : rows_) {
        if (r.status != "ok") continue;
        knownIds_.push_back(r.id);
        byVendor[r.vendor.isEmpty() ? QString("Other") : r.vendor].append(r);
    }
    menu_.clear();
    for (auto it = byVendor.begin(); it != byVendor.end(); ++it) {
        QList<PluginRow> list = it.value();
        std::sort(list.begin(), list.end(), [](const PluginRow& a, const PluginRow& b) { return a.name.toLower() < b.name.toLower(); });
        QVariantList plugins;
        for (const PluginRow& p : list) plugins.append(QVariantMap{{"id", p.id}, {"name", p.name}});
        menu_.append(QVariantMap{{"vendor", it.key()}, {"plugins", plugins}});
    }
}

}  // namespace jad
```

In `knownIds()` the test expects the order `vst3:a, vst3:b, vst3:c` = the order of rows; the code keeps row order. Add `bridge/plugins_model.cpp` to the `SOURCES` list of `jad_ui_lib` in `ui/CMakeLists.txt`.

- [ ] **Step 4: Snapshot fields**

`ui/bridge/snapshot.h`: change `InsertRow` to

```cpp
struct InsertRow {
    QString processorId;
    double gainDb = 0.0;  // the "gainDb" parameter (0 when absent)
    QString label;        // a plug-in's display name
    bool plugin = false;  // a hosted plug-in (its gain is not editable from the strip)
};
```

`ui/bridge/snapshot.cpp` line 77 (`tr.inserts.push_back({...})`) becomes:

```cpp
            tr.inserts.push_back({QString::fromStdString(ins.processorId), g == ins.params.end() ? 0.0 : g->second,
                                  QString::fromStdString(ins.label), lpc::isVst3Id(ins.processorId)});
```

with `#include "lpc/processor_ids.h"`. `ui/bridge/row_maps.h` line 12 becomes:

```cpp
    for (const InsertRow& i : t.inserts)
        inserts.append(QVariantMap{{"processorId", i.processorId}, {"gainDb", i.gainDb}, {"label", i.label}, {"plugin", i.plugin}});
```

- [ ] **Step 5: Run the bridge test**

Run: `cmake --build build-ui --config Debug --target jad_plugins_tests && ctest --test-dir build-ui -C Debug --output-on-failure -R jad_plugins_tests`
Expected: pass.

- [ ] **Step 6: Controller wiring: header**

`ui/bridge/project_controller.h`: add `#include "bridge/plugins_model.h"`; add the property near the `library` property:

```cpp
    Q_PROPERTY(jad::PluginsModel* plugins READ plugins CONSTANT)
```

public accessor `PluginsModel* plugins() { return &plugins_; }`; next to `removeInsert` etc.:

```cpp
    Q_INVOKABLE void addPlugin(const QString& trackId, const QString& pluginId, const QString& label);
    Q_INVOKABLE void openPluginEditor(const QString& trackId, int index);
    Q_INVOKABLE void setInsertState(const QString& trackId, int index, const QString& state);
```

private members:

```cpp
    PluginsModel plugins_;
#ifdef JAD_HAVE_JUCE
    std::shared_ptr<lpc::JucePluginHost> pluginHost_;
    std::unique_ptr<lpc::PluginScanner> scanner_;
#endif
    void setUpPlugins();       // creates the host and starts the scan; once per controller
    void refreshPluginRows();  // catalogue to the model, and to the host
    void commitPluginState(const lpc::InsertSlot& slot);
    void commitPluginStates();
```

and forward declarations/includes: `#ifdef JAD_HAVE_JUCE #include "juce_plugin_host.h" #include "plugin_scanner.h" #endif` plus `#include "lpc/plugin_host.h"`.

- [ ] **Step 7: Controller wiring: implementation**

`ui/bridge/project_controller.cpp`:

Where `JuceInit` is created (inside `openProject`, `if (!juce_) juce_ = std::make_unique<JuceInit>();`), add after it `setUpPlugins();`. Define:

```cpp
void ProjectController::setUpPlugins() {
#ifdef JAD_HAVE_JUCE
    if (pluginHost_) return;
    pluginHost_ = std::make_shared<lpc::JucePluginHost>();
    plugins_.setSupported(true);

    lpc::PluginScanner::Options o;
    o.scannerExe = std::filesystem::path(QCoreApplication::applicationDirPath().toStdU16String()) / "lpc-plugin-scanner.exe";
    o.cacheFile = lpc::appConfigDir() / "plugins.json";
    o.folders = lpc::PluginScanner::defaultFolders();
    scanner_ = std::make_unique<lpc::PluginScanner>(o);
    QPointer<ProjectController> self(this);
    scanner_->setChangedListener([self] {  // scan thread
        QMetaObject::invokeMethod(self.data(), [self] { if (self) self->refreshPluginRows(); }, Qt::QueuedConnection);
    });
    connect(&plugins_, &PluginsModel::rescanRequested, this, [this](int mode) {
        if (scanner_) scanner_->start(static_cast<lpc::ScanMode>(mode));
    });
    pluginHost_->setEditorClosedListener([this](const lpc::InsertSlot& slot) { commitPluginState(slot); });
    refreshPluginRows();                              // the cache is available at once
    scanner_->start(lpc::ScanMode::NewAndChanged);    // then new and changed files, in the background
#endif
}

void ProjectController::refreshPluginRows() {
#ifdef JAD_HAVE_JUCE
    if (!scanner_ || !pluginHost_) return;
    const lpc::PluginCatalogue c = scanner_->snapshot();
    pluginHost_->setCatalogue(c.descriptors());
    std::vector<PluginRow> rows;
    for (const lpc::ScanEntry& e : c.entries()) {
        if (e.status == lpc::ScanStatus::Ok) {
            for (const lpc::PluginDescriptor& d : e.descriptors)
                rows.push_back({QString::fromStdString(d.id), QString::fromStdString(d.name), QString::fromStdString(d.vendor), "ok",
                                QString::fromStdString(e.path), QString()});
        } else {
            const QString file = QFileInfo(QString::fromStdString(e.path)).completeBaseName();
            rows.push_back({QString(), file, QString(), "failed", QString::fromStdString(e.path), QString::fromStdString(e.reason)});
        }
    }
    plugins_.setRows(std::move(rows));
    plugins_.setScan(scanner_->running(), scanner_->done(), scanner_->total());
#endif
}
```

(add `#include <QCoreApplication>`, `<QFileInfo>`, `<QPointer>`, `"lpc/plugin_catalogue.h"` if missing; `QString::toStdU16String` gives a `std::u16string` that `std::filesystem::path` accepts.)

Pass the host to the project host: in `openProject`, change

```cpp
    host_ = std::make_unique<lpc::ProjectHost>(std::move(project), *engine_, *media_);
```
to
```cpp
#ifdef JAD_HAVE_JUCE
    lpc::IPluginHost* pluginHost = pluginHost_.get();
#else
    lpc::IPluginHost* pluginHost = nullptr;
#endif
    host_ = std::make_unique<lpc::ProjectHost>(std::move(project), *engine_, *media_, pluginHost);
```

In `teardown()`, before `host_.reset()` add `commitPluginStates();` is NOT wanted (the project may already be closing); instead after `engine_.reset();` add:

```cpp
#ifdef JAD_HAVE_JUCE
    if (pluginHost_) pluginHost_->releaseAll();  // editors first, then the instances
#endif
```

and before `host_.reset();`:

```cpp
#ifdef JAD_HAVE_JUCE
    if (pluginHost_) pluginHost_->closeAllEditors();  // the editors must go before the instances they show
#endif
```

New invokables:

```cpp
void ProjectController::addPlugin(const QString& trackId, const QString& pluginId, const QString& label) {
    sendCommand({{"type", "add_insert"}, {"trackId", trackId.toStdString()}, {"index", -1},
                 {"insert", {{"processorId", pluginId.toStdString()}, {"params", nlohmann::json::object()}, {"state", ""},
                             {"label", label.left(128).toStdString()}}}});
}

void ProjectController::setInsertState(const QString& trackId, int index, const QString& state) {
    sendCommand({{"type", "set_insert_state"}, {"trackId", trackId.toStdString()}, {"index", index}, {"state", state.toStdString()}});
}

void ProjectController::openPluginEditor(const QString& trackId, int index) {
#ifdef JAD_HAVE_JUCE
    const auto id = lpc::Uuid::parse(trackId.toStdString());
    if (pluginHost_ && id && !pluginHost_->openEditor(lpc::InsertSlot{*id, index})) announceStub(tr("This plug-in has no editor, or is not loaded"));
#else
    Q_UNUSED(trackId) Q_UNUSED(index)
#endif
}

void ProjectController::commitPluginState(const lpc::InsertSlot& slot) {
#ifdef JAD_HAVE_JUCE
    if (!pluginHost_ || !host_) return;
    const std::string state = pluginHost_->captureState(slot);  // also tells the host this is now the model's state
    if (state.empty()) return;
    const lpc::Uuid track = slot.track;
    const int index = slot.index;
    const bool changed = host_->read([track, index, &state](const lpc::Project& p) {
        const lpc::Track* t = p.findTrack(track);
        return t && index >= 0 && index < static_cast<int>(t->strip.inserts.size()) && t->strip.inserts[static_cast<std::size_t>(index)].state != state;
    }).get();
    if (changed) setInsertState(QString::fromStdString(track.toString()), index, QString::fromStdString(state));
#else
    Q_UNUSED(slot)
#endif
}

void ProjectController::commitPluginStates() {
#ifdef JAD_HAVE_JUCE
    if (!host_ || !pluginHost_) return;
    std::vector<lpc::InsertSlot> slots = host_->read([](const lpc::Project& p) {
        std::vector<lpc::InsertSlot> out;
        for (const lpc::Track& t : p.tracks)
            for (std::size_t i = 0; i < t.strip.inserts.size(); ++i)
                if (lpc::isVst3Id(t.strip.inserts[i].processorId)) out.push_back({t.id, static_cast<int>(i)});
        return out;
    }).get();
    for (const lpc::InsertSlot& s : slots) commitPluginState(s);
#endif
}
```

`sendCommand` is the controller's existing helper used by `addInsert`; check that it submits and waits or posts asynchronously. If it is asynchronous (returns before the project thread ran the command), `saveProject` must wait for the queued commits: in `saveProject()` call `commitPluginStates();` and then `host_->read([](const lpc::Project&) { return 0; }).get();` as a barrier before the existing `host_->read(...)` that copies the project (the project thread runs tasks in submission order, so the barrier is enough).

In `saveProject()` insert at the top, after `if (!host_) return false;`:

```cpp
    commitPluginStates();
```

`ui/main.cpp`: no change (the JUCE initialiser is created by the controller). In `~ProjectController()` (or wherever the controller is torn down) make sure `scanner_.reset()` happens before `pluginHost_.reset()` and both before `juce_.reset()`: with the member order `juce_`, … `pluginHost_`, `scanner_` declared after `juce_`, reverse destruction order does this automatically; verify that `pluginHost_` and `scanner_` are declared **after** `juce_` in the header.

Add the deploy step for the scanner next to the UI executable in `ui/CMakeLists.txt` after `qt_add_executable(jad-daw ...)`:

```cmake
if(TARGET lpc-plugin-scanner)
    add_dependencies(jad-daw lpc-plugin-scanner)
    add_custom_command(TARGET jad-daw POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:lpc-plugin-scanner> $<TARGET_FILE_DIR:jad-daw>)
endif()
```

and link `Qt6::Core` is already there through Quick.

- [ ] **Step 8: Add a controller test**

Append to `ui/tests/tst_bridge.cpp`, inside `BridgeTest` (before the closing `};`):

```cpp
    void pluginInsertsGoThroughCommandsAndKeepTheirLabel() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio");
        c.selectTrack(audio, "replace");
        c.addPlugin(audio, "vst3:00112233445566778899aabbccddeeff", "Verb");
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 1);
        const QVariantMap insert = c.inspector()->track().value("inserts").toList().first().toMap();
        QCOMPARE(insert.value("plugin").toBool(), true);
        QCOMPARE(insert.value("label").toString(), QString("Verb"));
        c.setInsertState(audio, 0, "AAAA");  // undoable like any command
        c.undo();
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 1);
        c.removeInsert(audio, 0);
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 0);
    }
```

(Uses the existing `c.undo()` slot; if the controller names it differently, use the name from `project_controller.h`.)

- [ ] **Step 9: Build and run the UI tests**

Run: `cmake --build build-ui --config Debug && ctest --test-dir build-ui -C Debug --output-on-failure`
Expected: all pass (QML tests are unchanged until Task 12).

- [ ] **Step 10: Commit**

```bash
git add ui
git commit -m "feat(ui): plug-in catalogue model, insert row fields, controller hosts plug-ins"
```

---

### Task 12: UI: insert menu, missing look, open editor, Plug-in Manager

**Files:**
- Create: `ui/qml/PluginManager.qml`
- Modify: `ui/qml/StripSlot.qml`, `ui/qml/ChannelStrip.qml`, `ui/qml/ProjectStrip.qml`, `ui/qml/Main.qml`, `ui/actions/actions.json`, `ui/CMakeLists.txt`, `ui/tests/tst_channelstrip.qml`, `ui/tests/tst_actions.cpp` (only if it counts ready actions)
- Docs: `README.md`, `THIRD_PARTY.md`

**Interfaces:**
- Consumes: Task 11 (`project.plugins`, `addPlugin`, `openPluginEditor`, insert row `plugin` and `label`).
- Produces: `ChannelStrip` properties `pluginGroups` (`[{vendor, plugins:[{id,name}]}]`) and `knownPluginIds` (`string[]`); signals `pluginInsertRequested(string id, string pluginId, string name)`, `insertEditorRequested(string id, int index)`, `pluginManagerRequested()`; functions `requestGainInsert()`, `requestPluginInsert(pluginId, name)`; alias `insertMenu`; `StripSlot` property `missing` and signal `doubleClicked()`; action `window.pluginManager` (ready).

- [ ] **Step 1: Update the QML tests first**

In `ui/tests/tst_channelstrip.qml` replace the existing `test_the_plus_slot_requests_an_insert` with:

```qml
    function test_the_plus_slot_opens_the_insert_menu_and_gain_requests_an_insert() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.insertAddRequested.connect(function (id) { got.push(id) })
        mouseClick(s.addInsertSlot)
        verify(s.insertMenu.visible)
        s.insertMenu.close()
        s.requestGainInsert()
        compare(got, ["t"])
    }
    function test_a_plugin_is_requested_with_its_id_and_name() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.pluginInsertRequested.connect(function (id, pluginId, name) { got.push([id, pluginId, name]) })
        s.requestPluginInsert("vst3:aa", "Verb")
        compare(got, [["t", "vst3:aa", "Verb"]])
    }
    function test_a_plugin_slot_shows_its_label_and_marks_a_missing_plugin() {
        var info = JSON.parse(JSON.stringify(keys))
        info.inserts = [{ processorId: "vst3:aa", gainDb: 0, label: "Verb", plugin: true },
                        { processorId: "vst3:bb", gainDb: 0, label: "Gone", plugin: true }]
        var s = createTemporaryObject(stripC, this, { info: info, knownPluginIds: ["vst3:aa"] })
        var ok = s.insertList.itemAt(0), gone = s.insertList.itemAt(1)
        compare(ok.text, "Verb")
        compare(gone.text, "Gone")
        verify(!ok.missing)
        verify(gone.missing)
        compare(ok.value, "")  // a plug-in has no gain value on the strip
    }
    function test_double_click_on_a_plugin_slot_asks_for_its_editor_and_a_plugin_does_not_drag() {
        var info = JSON.parse(JSON.stringify(keys))
        info.inserts = [{ processorId: "vst3:aa", gainDb: 0, label: "Verb", plugin: true }]
        var s = createTemporaryObject(stripC, this, { info: info, knownPluginIds: ["vst3:aa"] })
        var opened = [], gains = []
        s.insertEditorRequested.connect(function (id, index) { opened.push([id, index]) })
        s.insertGainReleased.connect(function (id, index, db) { gains.push(db) })
        var slot = s.insertList.itemAt(0)
        mouseDoubleClickSequence(slot, 10, 10)
        compare(opened, [["t", 0]])
        mousePress(slot, 10, 10)
        mouseMove(slot, 40, 10, 0, Qt.LeftButton)
        mouseRelease(slot, 40, 10)
        compare(gains.length, 0)
    }
    function test_the_manager_entry_of_the_menu_is_requested() {
        var s = createTemporaryObject(stripC, this)
        var n = 0
        s.pluginManagerRequested.connect(function () { ++n })
        s.insertMenu.managerChosen()
        compare(n, 1)
    }
```

Run: `cmake --build build-ui --config Debug --target jad_ui_qmltests && ctest --test-dir build-ui -C Debug --output-on-failure -R qmltests`
Expected: the new tests fail (unknown properties).

- [ ] **Step 2: `StripSlot`**

In `ui/qml/StripSlot.qml` add `property bool missing: false`, `signal doubleClicked()`, make the label colour reflect it:

```qml
        color: root.missing ? Theme.stateError : (root.filled ? Theme.textPrimary : Theme.textSecondary)
```

(if `Theme.stateError` does not exist, run `grep -n "error\|Error\|red" ui/qml/Theme.qml` and use the token the ErrorBar uses), and add to the `MouseArea`:

```qml
        onDoubleClicked: root.doubleClicked()
```

- [ ] **Step 3: `ChannelStrip`**

Add properties and signals near the existing ones:

```qml
    property var pluginGroups: []      // [{vendor, plugins: [{id, name}]}], from the Plug-in catalogue
    property var knownPluginIds: []    // ids of the plug-ins that are installed
    readonly property alias insertMenu: insertMenu
    signal pluginInsertRequested(string id, string pluginId, string name)
    signal insertEditorRequested(string id, int index)
    signal pluginManagerRequested()

    function requestGainInsert() { insertAddRequested(trackId) }
    function requestPluginInsert(pluginId, name) { pluginInsertRequested(trackId, pluginId, name) }
    function isMissing(ins) { return ins.plugin === true && knownPluginIds.indexOf(ins.processorId) < 0 }
```

Replace the existing `insertLabel` function by

```qml
    function insertLabel(ins) {
        if (ins.plugin) return ins.label && ins.label !== "" ? ins.label : qsTr("Plug-in")
        return ins.processorId === "builtin.gain" ? qsTr("Gain") : ins.processorId
    }
```

Add the menu component after `sendMenu`:

```qml
    ThemedMenu {
        id: insertMenu
        signal managerChosen()
        ThemedMenuItem { text: qsTr("Gain"); onTriggered: root.requestGainInsert() }
        Instantiator {
            model: root.pluginGroups
            delegate: ThemedMenu {
                id: vendorMenu
                required property var modelData
                title: modelData.vendor
                Instantiator {
                    model: vendorMenu.modelData.plugins
                    delegate: ThemedMenuItem {
                        required property var modelData
                        text: modelData.name
                        onTriggered: root.requestPluginInsert(modelData.id, modelData.name)
                    }
                    onObjectAdded: (index, object) => vendorMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => vendorMenu.removeItem(object)
                }
            }
            onObjectAdded: (index, object) => insertMenu.insertMenu(index + 1, object)
            onObjectRemoved: (index, object) => insertMenu.removeMenu(object)
        }
        MenuSeparator {}
        ThemedMenuItem { text: qsTr("Plug-in Manager…"); onTriggered: insertMenu.managerChosen() }
    }
    Connections { target: insertMenu; function onManagerChosen() { root.pluginManagerRequested() } }
```

Change the insert delegate and the `+` slot:

```qml
            delegate: StripSlot {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                text: root.insertLabel(modelData)
                value: modelData.plugin ? "" : (root.dragIndex === index ? root.dragGain : modelData.gainDb).toFixed(1)
                missing: root.isMissing(modelData)
                filled: true
                removable: true
                onRemoveRequested: root.insertRemoveRequested(root.trackId, index)
                onDoubleClicked: { if (modelData.plugin) root.insertEditorRequested(root.trackId, index) }
                onDragged: (dx) => {
                    if (modelData.plugin) return
                    root.dragIndex = index
                    root.dragGain = Math.max(-96, Math.min(24, modelData.gainDb + dx * 0.1))
                }
                onDragReleased: {
                    if (root.dragIndex !== index) return
                    const db = root.dragGain
                    root.dragIndex = -1
                    root.insertGainReleased(root.trackId, index, db)
                }
            }
```

```qml
        StripSlot {
            id: addInsertSlot
            Layout.fillWidth: true
            visible: root.slotsVisible
            text: "+"
            onClicked: insertMenu.popup(addInsertSlot, 0, addInsertSlot.height)
        }
```

The test `test_the_plus_slot_opens_the_insert_menu...` clicks `addInsertSlot` and expects `insertMenu.visible`. In the QML tests a `Menu.popup(item, x, y)` opens a popup inside the test window; the existing output/send menus use the same call.

- [ ] **Step 4: `ProjectStrip`**

```qml
ChannelStrip {
    id: root
    required property ProjectController project
    targets: project.inspector.busTargets.filter((t) => t.id !== root.trackId)
    pluginGroups: project.plugins.menu
    knownPluginIds: project.plugins.knownIds
    ...
    onPluginInsertRequested: (id, pluginId, name) => project.addPlugin(id, pluginId, name)
    onInsertEditorRequested: (id, index) => project.openPluginEditor(id, index)
    onPluginManagerRequested: project.pluginManagerOpen = true
```

and keep the other handlers as they are. Add to `ProjectController` (header, next to `mixerVisible`): `Q_PROPERTY(bool pluginManagerOpen READ pluginManagerOpen WRITE setPluginManagerOpen NOTIFY panelsChanged)` with a `bool pluginManagerOpen_ = false;` member and trivial accessors (not saved with the panel layout).

- [ ] **Step 5: The Plug-in Manager dialog**

`ui/qml/PluginManager.qml` (follows `AboutDialog.qml` for the frame):

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

Dialog {
    id: root
    required property ProjectController project
    modal: true
    title: qsTr("Plug-in Manager")
    anchors.centerIn: parent
    width: Math.min(760, parent ? parent.width - 48 : 760)
    height: Math.min(520, parent ? parent.height - 48 : 520)
    standardButtons: Dialog.Close
    onClosed: project.pluginManagerOpen = false

    background: Rectangle {
        color: Theme.surfacePanel
        border.color: Theme.borderStrong
        radius: Theme.radiusDialog
    }
    header: Item { height: 0 }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text {
            text: qsTr("Plug-in Manager")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
            font.weight: Theme.fontTypeLabelWeight
        }
        Text {
            visible: !root.project.plugins.supported
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("This build cannot host plug-ins.")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.project.plugins
            headerPositioning: ListView.OverlayHeader
            header: Rectangle {
                width: list.width
                height: 22
                color: Theme.surfaceRaised
                z: 2
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: Theme.spacing[2]
                    spacing: Theme.spacing[2]
                    Repeater {
                        model: [qsTr("Name"), qsTr("Vendor"), qsTr("Status"), qsTr("Path / reason")]
                        Text { required property string modelData; width: [180, 140, 60, 340][index]; text: modelData; color: Theme.textSecondary
                               font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    }
                }
            }
            delegate: Rectangle {
                id: row
                required property int index
                required property string name
                required property string vendor
                required property string status
                required property string path
                required property string reason
                width: list.width
                height: 22
                color: index % 2 ? "transparent" : Theme.surfaceRaised
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: Theme.spacing[2]
                    spacing: Theme.spacing[2]
                    Text { width: 180; text: row.name; elide: Text.ElideRight; color: Theme.textPrimary
                           font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    Text { width: 140; text: row.vendor; elide: Text.ElideRight; color: Theme.textSecondary
                           font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    Text { width: 60; text: row.status === "ok" ? qsTr("OK") : qsTr("Failed"); color: row.status === "ok" ? Theme.textValue : Theme.stateError
                           font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    Text { width: 340; text: row.status === "ok" ? row.path : row.reason + " - " + row.path; elide: Text.ElideMiddle; color: Theme.textSecondary
                           font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing[2]
            Button { text: qsTr("Rescan"); enabled: root.project.plugins.supported && !root.project.plugins.scanning
                     onClicked: root.project.plugins.rescanNew() }
            Button { text: qsTr("Rescan failed"); enabled: root.project.plugins.supported && !root.project.plugins.scanning
                     onClicked: root.project.plugins.rescanFailed() }
            Button { text: qsTr("Rescan all"); enabled: root.project.plugins.supported && !root.project.plugins.scanning
                     onClicked: root.project.plugins.rescanAll() }
            Text { Layout.fillWidth: true; text: root.project.plugins.scanText; color: Theme.textSecondary
                   font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
        }
    }
}
```

Before relying on `Theme.stateError`, `Theme.surfaceRaised`, `Theme.spacing`, `Theme.radiusDialog`, open `ui/qml/Theme.qml` and `ErrorBar.qml` and use the tokens they define (those names are used by `ChannelStrip.qml`, `StripSlot.qml` and `AboutDialog.qml`, so only `stateError` needs checking).

Register it: add `qml/PluginManager.qml` to `QML_FILES` in `ui/CMakeLists.txt`. In `ui/qml/Main.qml`, next to `AboutDialog { ... }` add

```qml
    PluginManager {
        id: pluginManager
        project: controller
        visible: controller.pluginManagerOpen
        onVisibleChanged: if (!visible) controller.pluginManagerOpen = false
    }
```

(use the actual id of the controller object in `Main.qml`; the action table in that file refers to it as `controller`), and in the action handler map add after `"view.mixer"`:

```qml
        "window.pluginManager": () => { controller.pluginManagerOpen = !controller.pluginManagerOpen },
```

`ui/actions/actions.json`: in the Window group add

```json
 {"id":"window.pluginManager","label":"Plug-in Manager…","menu":"Window","shortcut":"","kind":"command","status":"ready"},
```

(before `window.minimize`). If `ui/tests/tst_actions.cpp` asserts exact counts of ready or stub actions, adjust the number by one; otherwise nothing.

- [ ] **Step 6: Run the UI tests**

Run: `cmake --build build-ui --config Debug && ctest --test-dir build-ui -C Debug --output-on-failure`
Expected: all pass, including the new `ChannelStrip` tests.

- [ ] **Step 7: Docs and licence**

`THIRD_PARTY.md`: change the JUCE row's "Used in" to `audio device and VST3 hosting (lpc-cli, the UI, lpc-plugin-scanner)` and add below the table:

```
- **VST3 SDK** (bundled with JUCE 8.0.4): GPLv3 option, compatible with AGPLv3. VST is a trademark of Steinberg Media Technologies GmbH.
```

`README.md`: in the Inspector paragraph add that inserts can be VST3 plug-ins; add a paragraph after the Mixer one:

```
- **Plug-ins (VST3 effects)**: click the `+` slot of an insert area: Gain or any scanned VST3 plug-in, grouped by vendor.
  Double click a plug-in slot to open its own window. The scan runs in the background at start (in a child process: a plug-in
  that crashes or hangs is listed as failed and skipped) and its result is cached in `plugins.json` in the application config
  folder; Window > Plug-in Manager rescans. A plug-in that is not installed shows in red and passes the sound through; the
  project keeps its state. Plug-in state is saved with the project, one undo step per editor session. Delay compensation
  aligns tracks and buses. `lpc-cli render` hosts plug-ins too.
```

and change the "Known limits" paragraph: replace "plug-in hosting is not there yet" by "plug-ins: VST3 effects only (no instruments, MIDI, sidechain or automation of plug-in parameters), stereo in and out, Windows, and a plug-in that crashes while playing takes the app down; changes made inside an editor become one undo step when the editor closes".

- [ ] **Step 8: Check by hand with the test plug-in**

Run:
```bash
cmake --build build-ui --config Debug
mkdir -p "$LOCALAPPDATA/Programs/Common/VST3" && cp -r "build-ui/tools/test-plugin/lpc_test_gain_artefacts/Debug/VST3/LPC Test Gain.vst3" "$LOCALAPPDATA/Programs/Common/VST3/"
build-ui/ui/Debug/jad-daw.exe --project demo.lpc
```
Expected: in the Mixer, `+` on the audio track lists Gain and a "JAD" submenu with LPC Test Gain; adding it shows a slot "LPC Test Gain"; double click opens a window with a Gain slider; play the demo while moving the slider (the sound changes after release); close the window; Edit > Undo restores the previous gain; Save, close and reopen the project: the slot is back and the sound matches. Window > Plug-in Manager lists the plug-in as OK and Rescan works. Remove the test plug-in from the VST3 folder, restart: the slot shows in red and the sound passes through; save and look at the project file: the id and state are still there. Then remove `%LOCALAPPDATA%\JAD\JAD Daw\plugins.json` if you want a clean state.

- [ ] **Step 9: Update the deferred list**

In `docs/superpowers/ui-b-deferred.md` change the `Next` entry for VST3 to `VST3 hosting as effects: done (spec and plan under docs/superpowers)` and add under "Not verified": `- By-hand pass of the plug-in editor window with third-party plug-ins (resize, DPI, close while playing)`.

- [ ] **Step 10: Commit**

```bash
git add ui README.md THIRD_PARTY.md docs/superpowers/ui-b-deferred.md
git commit -m "feat(ui): plug-in insert menu, editor on double click, Plug-in Manager"
```

---

## Self-review

**Spec coverage (spec section → task):** 1 scope → all; 2 architecture → 5, 8, 9; 3.1 `IProcessor` → 5; 3.2 `IPluginHost` (amended: acquire/prune/captureState/listener) → 5, 7; 3.3 ids, `label`, validation, `set_insert_state` → 3, 4; 3.4 PDC (amended: per edge) → 6; 4.1 modules and licence → 8, 12; 4.2 `PluginProcessor` → 8; 4.3 `JucePluginHost`, threading, registry → 8; 4.4 scanning (child process, cache, folders, rescan modes) → 9, 11; 4.5 editor window → 8, 11, 12; 5 state and undo → 4, 7, 11; 6 UI (menu, slot look, double click, Manager, bridge, action) → 11, 12; 7 errors → 5 (missing), 9 (scan failures), 8 (failed load stays pass-through; not marked in the UI in v1, see below); 8 testing → every task; 9 risks → Task 2 spike; 10 order of work → task order.

**Known deviations from the spec text** (to say to the user in the handoff): a plug-in that fails to instantiate is pass-through and logged only (the slot is marked red only when its id is missing from the catalogue); instrument plug-ins found by the scanner are listed as failed ("instruments are not supported yet"); the `set_insert_state` "already applied" mechanism is `captureState` recording the state (spec section 5.3 amended).

**Placeholder scan:** no TBD/TODO; every code step has code. The places that say "read the file and use the real name" (Task 4 step 1 `ApplyResult` accessor, Task 10 step 1 CLI namespace, Task 11 `trackRowToMap` and `undo` slot names, Task 12 `Theme.stateError`) name the exact file and the exact thing to look up because those identifiers were not visible while planning.

**Type consistency:** `InsertSlot{Uuid track; int index}`, `PluginDescriptor{id,name,vendor,version,path,native}`, `ScanMode` values 0/1/2 match `PluginsModel::rescanRequested`, `PdcPlan::edges[trackId].{output, sends}`, `DelayLine::frames()`, `SendPlayback::delay`, `TrackConfig::outputDelay`, `makeInsert(ref, host, slot, sr, maxBlock)`, `makeSetInsertState(trackId, index, state)`, `ChannelStrip.requestGainInsert/requestPluginInsert`, `ProjectController::addPlugin/openPluginEditor/setInsertState` are used with the same names and parameter orders in every task that mentions them.

**Review Focus coverage:** project opened without the plug-in (Tasks 5, 7; manual check in 12); broken `.vst3` (Task 9 tests); undo of an editor session (Tasks 4, 7); aligned bus with one latent plug-in, including sends (Task 6); host closed with a load pending (Tasks 7, 8).
