#include "lpc/project_io.h"

#include <fstream>
#include <stdexcept>
#include <string>

#include "lpc/model_json.h"

namespace lpc {

namespace fs = std::filesystem;

namespace {

std::string pathText(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

}  // namespace

const std::vector<Migration>& builtinMigrations() {
    static const std::vector<Migration> none;  // schema 1 is the first version
    return none;
}

nlohmann::json migrateToCurrent(nlohmann::json doc, const std::vector<Migration>& chain, int currentVersion) {
    if (!doc.is_object() || !doc.contains("schemaVersion") || !doc["schemaVersion"].is_number_integer())
        throw std::runtime_error("project document has no schemaVersion");
    int version = doc["schemaVersion"].get<int>();
    if (version < 1) throw std::runtime_error("invalid schemaVersion " + std::to_string(version));
    if (version > currentVersion)
        throw std::runtime_error("project was saved by a newer version (schema " + std::to_string(version) +
                                 "), please update");
    if (static_cast<int>(chain.size()) < currentVersion - 1) throw std::runtime_error("migration chain is incomplete");
    while (version < currentVersion) {
        doc = chain[static_cast<std::size_t>(version - 1)](std::move(doc));
        ++version;
        doc["schemaVersion"] = version;
    }
    return doc;
}

void saveProject(const Project& project, const fs::path& dir) {
    fs::create_directories(dir / "audio");
    fs::create_directories(dir / "cache");
    const fs::path target = dir / "project.json";
    const fs::path tmp = dir / "project.json.tmp";
    const fs::path bak = dir / "project.json.bak";

    const nlohmann::json doc = {{"schemaVersion", kCurrentSchemaVersion}, {"project", toJson(project)}};
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + pathText(tmp));
        out << doc.dump(2);
        out.flush();
        if (!out) throw std::runtime_error("write failed for " + pathText(tmp));
    }
    if (fs::exists(target)) fs::copy_file(target, bak, fs::copy_options::overwrite_existing);
    fs::rename(tmp, target);  // replaces the target atomically on Windows and POSIX
}

Project loadProject(const fs::path& dir) {
    const fs::path file = dir / "project.json";
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + pathText(file));

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("project.json is not valid JSON: ") + e.what());
    }
    doc = migrateToCurrent(std::move(doc), builtinMigrations(), kCurrentSchemaVersion);
    try {
        return projectFromJson(doc.at("project"));
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("project.json is invalid: ") + e.what());
    }
}

}  // namespace lpc
