#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <fstream>
#include <thread>
#include <sstream>
#include "lpc/model_json.h"
#include "lpc/project_io.h"
#include "temp_dir.h"

using namespace lpc;
namespace fs = std::filesystem;

namespace {

Project smallProject() {
    std::mt19937_64 rng(3);
    Project p(Uuid::random(rng));
    p.name = "Small";
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Bus;
    t.name = "Bus";
    p.tracks.push_back(t);
    return p;
}

std::string readAll(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void writeAll(const fs::path& f, const std::string& s) {
    std::ofstream out(f, std::ios::binary | std::ios::trunc);
    out << s;
}

}  // namespace

TEST_CASE("io: save then load returns an equal project", "[io]") {
    test::TempDir tmp;
    const Project p = smallProject();
    saveProject(p, tmp.path / "a.lpc");
    REQUIRE(fs::exists(tmp.path / "a.lpc" / "project.json"));
    REQUIRE(fs::is_directory(tmp.path / "a.lpc" / "audio"));
    REQUIRE(fs::is_directory(tmp.path / "a.lpc" / "cache"));
    REQUIRE(loadProject(tmp.path / "a.lpc") == p);
}

TEST_CASE("io: second save keeps the previous version as a backup and leaves no temp file", "[io]") {
    test::TempDir tmp;
    const fs::path dir = tmp.path / "b.lpc";
    Project p = smallProject();
    saveProject(p, dir);
    const std::string first = readAll(dir / "project.json");
    p.name = "Renamed";
    saveProject(p, dir);
    REQUIRE(readAll(dir / "project.json.bak") == first);
    REQUIRE_FALSE(fs::exists(dir / "project.json.tmp"));
    REQUIRE(loadProject(dir).name == "Renamed");
}

TEST_CASE("io: folder names with spaces and non-ASCII characters", "[io]") {
    test::TempDir tmp;
    const fs::path dir = tmp.path / fs::path(u8"Progetto è 日本 con spazi.lpc");
    const Project p = smallProject();
    saveProject(p, dir);
    REQUIRE(loadProject(dir) == p);
}

TEST_CASE("io: truncated or garbage project.json is rejected and left untouched", "[io]") {
    test::TempDir tmp;
    const fs::path dir = tmp.path / "c.lpc";
    saveProject(smallProject(), dir);
    const std::string good = readAll(dir / "project.json");

    const std::string truncated = good.substr(0, good.size() / 2);
    writeAll(dir / "project.json", truncated);
    REQUIRE_THROWS_WITH(loadProject(dir), Catch::Matchers::ContainsSubstring("not valid JSON"));
    REQUIRE(readAll(dir / "project.json") == truncated);

    writeAll(dir / "project.json", "\x01\x02 not json at all");
    REQUIRE_THROWS_AS(loadProject(dir), std::runtime_error);

    writeAll(dir / "project.json", "");
    REQUIRE_THROWS_AS(loadProject(dir), std::runtime_error);
}

TEST_CASE("io: valid JSON with an invalid project is rejected with a clear message", "[io]") {
    test::TempDir tmp;
    const fs::path dir = tmp.path / "d.lpc";
    saveProject(smallProject(), dir);
    nlohmann::json doc = nlohmann::json::parse(readAll(dir / "project.json"));
    doc["project"]["tracks"][1]["kind"] = "banana";
    writeAll(dir / "project.json", doc.dump());
    REQUIRE_THROWS_WITH(loadProject(dir), Catch::Matchers::ContainsSubstring("project.json is invalid"));
}

TEST_CASE("io: missing folder or file", "[io]") {
    test::TempDir tmp;
    REQUIRE_THROWS_AS(loadProject(tmp.path / "nope.lpc"), std::runtime_error);
}

TEST_CASE("io: project from a newer schema is refused", "[io]") {
    test::TempDir tmp;
    const fs::path dir = tmp.path / "e.lpc";
    saveProject(smallProject(), dir);
    nlohmann::json doc = nlohmann::json::parse(readAll(dir / "project.json"));
    doc["schemaVersion"] = kCurrentSchemaVersion + 1;
    writeAll(dir / "project.json", doc.dump());
    REQUIRE_THROWS_WITH(loadProject(dir), Catch::Matchers::ContainsSubstring("newer version"));
}

TEST_CASE("io: migration chain upgrades old documents step by step", "[io]") {
    const std::vector<Migration> chain = {
        [](nlohmann::json d) {  // 1 -> 2: rename "old" to "mid"
            d["mid"] = d["old"];
            d.erase("old");
            return d;
        },
        [](nlohmann::json d) {  // 2 -> 3: rename "mid" to "new"
            d["new"] = d["mid"];
            d.erase("mid");
            return d;
        }};
    nlohmann::json doc = {{"schemaVersion", 1}, {"old", 42}};
    const nlohmann::json out = migrateToCurrent(doc, chain, 3);
    REQUIRE(out["schemaVersion"] == 3);
    REQUIRE(out["new"] == 42);
    REQUIRE_FALSE(out.contains("old"));

    REQUIRE_THROWS(migrateToCurrent({{"old", 1}}, chain, 3));                      // no schemaVersion
    REQUIRE_THROWS(migrateToCurrent({{"schemaVersion", 0}}, chain, 3));            // below 1
    REQUIRE_THROWS(migrateToCurrent({{"schemaVersion", 1}}, {chain[0]}, 3));      // chain too short
}

TEST_CASE("io: an autosave is newer than the saved file until the next save, and can be restored", "[io][autosave]") {
    test::TempDir dir;
    Project saved = smallProject();
    saveProject(saved, dir.path);
    REQUIRE_FALSE(hasNewerAutosave(dir.path));
    Project changed = saved;
    changed.name = "Changed after the save";
    std::this_thread::sleep_for(std::chrono::milliseconds(50));   // the file times must differ
    saveAutosave(changed, dir.path);
    REQUIRE(fs::exists(dir.path / "project.autosave.json"));
    REQUIRE(hasNewerAutosave(dir.path));
    REQUIRE(loadProject(dir.path).name == "Small");                // the saved file is untouched
    restoreAutosave(dir.path);
    REQUIRE_FALSE(fs::exists(dir.path / "project.autosave.json"));
    REQUIRE(loadProject(dir.path).name == "Changed after the save");
    REQUIRE(fs::exists(dir.path / "project.json.bak"));            // what was saved is kept

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    saveAutosave(saved, dir.path);
    saveProject(changed, dir.path);                                // a normal save removes the autosave
    REQUIRE_FALSE(fs::exists(dir.path / "project.autosave.json"));
    REQUIRE_FALSE(hasNewerAutosave(dir.path));

    std::ofstream(dir.path / "project.autosave.json") << "{ not json";   // a damaged autosave never replaces a good project
    REQUIRE_THROWS(restoreAutosave(dir.path));
    REQUIRE(loadProject(dir.path).name == "Changed after the save");
    discardAutosave(dir.path);
    REQUIRE_FALSE(fs::exists(dir.path / "project.autosave.json"));
}

TEST_CASE("io: alternatives are named copies that can be listed, restored and deleted", "[io][alternatives]") {
    test::TempDir tmp;
    const fs::path dir = tmp.path / "alt.lpc";
    Project p = smallProject();
    saveProject(p, dir);
    REQUIRE(listAlternatives(dir).empty());
    REQUIRE(validAlternativeName("Mix 2.1_final"));
    REQUIRE_FALSE(validAlternativeName(""));
    REQUIRE_FALSE(validAlternativeName("../evil"));
    REQUIRE_FALSE(validAlternativeName(" lead"));
    REQUIRE_THROWS(saveAlternative(dir, "a/b"));
    saveAlternative(dir, "Verse");
    p.name = "Changed";
    saveProject(p, dir);
    saveAlternative(dir, "Chorus");
    REQUIRE(listAlternatives(dir) == std::vector<std::string>{"Chorus", "Verse"});
    restoreAlternative(dir, "Verse");
    REQUIRE(loadProject(dir).name == smallProject().name);
    REQUIRE(readAll(dir / "project.json.bak").find("Changed") != std::string::npos);  // what was there is kept
    writeAll(dir / "alternatives" / "Broken.json", "{ not json");
    REQUIRE_THROWS(restoreAlternative(dir, "Broken"));
    REQUIRE(loadProject(dir).name == smallProject().name);  // a damaged one changes nothing
    deleteAlternative(dir, "Chorus");
    REQUIRE(listAlternatives(dir) == std::vector<std::string>{"Broken", "Verse"});
}
