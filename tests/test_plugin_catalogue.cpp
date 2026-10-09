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
