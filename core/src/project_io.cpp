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
    discardAutosave(dir);
}

static Project loadProjectFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + pathText(file));

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(pathText(file.filename()) + " is not valid JSON: " + e.what());
    }
    doc = migrateToCurrent(std::move(doc), builtinMigrations(), kCurrentSchemaVersion);
    try {
        return projectFromJson(doc.at("project"));
    } catch (const std::exception& e) {
        throw std::runtime_error(pathText(file.filename()) + " is invalid: " + e.what());
    }
}

Project loadProject(const fs::path& dir) { return loadProjectFile(dir / "project.json"); }

void saveAutosave(const Project& project, const fs::path& dir) {
    fs::create_directories(dir);
    const fs::path target = dir / "project.autosave.json";
    const fs::path tmp = dir / "project.autosave.json.tmp";
    const nlohmann::json doc = {{"schemaVersion", kCurrentSchemaVersion}, {"project", toJson(project)}};
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + pathText(tmp));
        out << doc.dump(1);
        out.flush();
        if (!out) throw std::runtime_error("write failed for " + pathText(tmp));
    }
    fs::rename(tmp, target);
}

bool hasNewerAutosave(const fs::path& dir) {
    std::error_code ec;
    const fs::path autosave = dir / "project.autosave.json";
    if (!fs::exists(autosave, ec)) return false;
    const fs::path saved = dir / "project.json";
    if (!fs::exists(saved, ec)) return true;
    return fs::last_write_time(autosave, ec) > fs::last_write_time(saved, ec);
}

void discardAutosave(const fs::path& dir) {
    std::error_code ec;
    fs::remove(dir / "project.autosave.json", ec);
    fs::remove(dir / "project.autosave.json.tmp", ec);
}

void restoreAutosave(const fs::path& dir) {
    const fs::path autosave = dir / "project.autosave.json";
    (void)loadProjectFile(autosave);  // a damaged autosave must not replace a good project
    const fs::path target = dir / "project.json";
    if (fs::exists(target)) fs::copy_file(target, dir / "project.json.bak", fs::copy_options::overwrite_existing);
    fs::copy_file(autosave, target, fs::copy_options::overwrite_existing);
    discardAutosave(dir);
}

bool validAlternativeName(const std::string& name) {
    if (name.empty() || name.size() > 60 || name.front() == ' ' || name.back() == ' ' || name.front() == '.') return false;
    for (const unsigned char c : name)
        if (!(std::isalnum(c) || c == ' ' || c == '.' || c == '-' || c == '_')) return false;
    return true;
}

std::vector<std::string> listAlternatives(const fs::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir / "alternatives", ec))
        if (entry.is_regular_file() && entry.path().extension() == ".json") out.push_back(pathText(entry.path().stem()));
    std::sort(out.begin(), out.end());
    return out;
}

void saveAlternative(const fs::path& dir, const std::string& name) {
    if (!validAlternativeName(name)) throw std::runtime_error("an alternative name has 1 to 60 letters, digits, spaces, dots, dashes or underscores");
    fs::create_directories(dir / "alternatives");
    fs::copy_file(dir / "project.json", dir / "alternatives" / (name + ".json"), fs::copy_options::overwrite_existing);
}

void deleteAlternative(const fs::path& dir, const std::string& name) {
    if (!validAlternativeName(name)) return;
    std::error_code ec;
    fs::remove(dir / "alternatives" / (name + ".json"), ec);
}

void restoreAlternative(const fs::path& dir, const std::string& name) {
    if (!validAlternativeName(name)) throw std::runtime_error("no such alternative");
    const fs::path alternative = dir / "alternatives" / (name + ".json");
    (void)loadProjectFile(alternative);  // a damaged alternative must not replace a good project
    const fs::path target = dir / "project.json";
    if (fs::exists(target)) fs::copy_file(target, dir / "project.json.bak", fs::copy_options::overwrite_existing);
    fs::copy_file(alternative, target, fs::copy_options::overwrite_existing);
    discardAutosave(dir);
}

}  // namespace lpc
