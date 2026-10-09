// lpc-plugin-scanner <file.vst3>: loads one plug-in file in its own process, prints one JSON line and exits.
// A plug-in that crashes or hangs takes only this process with it; the parent sees a non-zero exit code or a timeout.
#include <cstdio>
#include <cstdlib>
#include <iostream>

#ifdef _WIN32
#include <crtdbg.h>
#include <windows.h>
#endif

#include <juce_audio_processors/juce_audio_processors.h>
#include <nlohmann/json.hpp>

namespace {

std::string md5Id(const juce::PluginDescription& d) {
    const juce::String key = d.name + "|" + d.manufacturerName + "|" + juce::String(d.uniqueId) + "|" + juce::String(d.deprecatedUid);
    // 32 hex digits from two 64-bit hashes of the description: stable across runs and independent of where the file is
    char buf[40];
    std::snprintf(buf, sizeof buf, "%016llx%016llx", static_cast<unsigned long long>(key.hashCode64()),
                  static_cast<unsigned long long>((key + "|jad").hashCode64()));
    return std::string("vst3:") + buf;
}

int fail(const std::string& reason) {
    std::cout << nlohmann::json{{"ok", false}, {"reason", reason}}.dump() << std::endl;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
#ifdef _WIN32
    // A crashing plug-in must end this process quietly: no Windows error box and no CRT assertion or abort dialog, which would
    // hold the child until the parent's timeout.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) _CrtSetReportMode(type, 0);
#endif
#endif
    if (const char* crash = std::getenv("LPC_SCANNER_TEST_CRASH"); crash && *crash == '1') std::abort();  // for the scanner tests
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::AudioPluginFormatManager formats;
    formats.addFormat(new juce::VST3PluginFormat());
    juce::AudioPluginFormat& format = *formats.getFormat(0);
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
        std::unique_ptr<juce::AudioPluginInstance> instance = formats.createPluginInstance(*desc, 48000.0, 512, error);
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
