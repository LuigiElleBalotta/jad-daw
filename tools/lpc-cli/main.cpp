#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#include "cli_commands.h"

#ifdef LPC_CLI_HAS_PLUGINS
#include <memory>

#include <juce_events/juce_events.h>

#include "juce_plugin_host.h"
#include "lpc/plugin_catalogue.h"
#include "plugin_scanner.h"

namespace {
// JUCE is started only when a project with plug-ins is rendered; this main thread is the message thread, so plug-ins load
// synchronously and a render has every plug-in loaded.
std::unique_ptr<juce::ScopedJuceInitialiser_GUI> gJuce;

std::shared_ptr<lpc::IPluginHost> makePluginHost() {
    if (!gJuce) gJuce = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
    lpc::PluginScanner::Options o;
    o.scannerExe = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("lpc-plugin-scanner.exe").getFullPathName().toStdString();
    o.cacheFile = lpc::appConfigDir() / "plugins.json";
    o.folders = lpc::PluginScanner::defaultFolders();
    lpc::PluginScanner scanner(o);
    scanner.start(lpc::ScanMode::NewAndChanged);  // cheap when the cache is current
    scanner.wait();
    auto host = std::make_shared<lpc::JucePluginHost>();
    host->setCatalogue(scanner.snapshot().descriptors());
    return host;
}
}  // namespace
#endif

int main(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    // argv is in the system code page; the command line itself is UTF-16, so paths with any character work.
    (void)argc;
    (void)argv;
    SetConsoleOutputCP(CP_UTF8);
    int wideCount = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    if (wide) {
        for (int i = 1; i < wideCount; ++i) args.push_back(lpc::cli::wideToUtf8(wide[i]));
        LocalFree(wide);
    }
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
#ifdef LPC_CLI_HAS_PLUGINS
    lpc::cli::setPluginHostFactory(makePluginHost);
    const int code = lpc::cli::runCli(args, std::cout, std::cerr);
    gJuce.reset();
    return code;
#else
    return lpc::cli::runCli(args, std::cout, std::cerr);
#endif
}
