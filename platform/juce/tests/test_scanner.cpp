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

TEST_CASE("scanner: a missing scanner program is reported, not fatal", "[scan]") {
    TempFolder plugins, cache;
    std::ofstream(plugins.path / "x.vst3", std::ios::binary) << "x";
    PluginScanner::Options o = options(plugins.path, cache.path / "plugins.json");
    o.scannerExe = fs::path(LPC_TEST_VST3_DIR) / "does-not-exist.exe";
    PluginScanner scanner(o);
    scanner.start(ScanMode::NewAndChanged);
    scanner.wait();
    const PluginCatalogue snapshot = scanner.snapshot();  // named: find() points into it
    const ScanEntry* e = snapshot.find((plugins.path / "x.vst3").string());
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
