#pragma once
#include <filesystem>
#include <functional>
#include <vector>

#include <nlohmann/json.hpp>

#include "lpc/model.h"

namespace lpc {

inline constexpr int kCurrentSchemaVersion = 1;

// chain[i] migrates a document from schema version i+1 to i+2.
using Migration = std::function<nlohmann::json(nlohmann::json)>;

const std::vector<Migration>& builtinMigrations();
nlohmann::json migrateToCurrent(nlohmann::json doc, const std::vector<Migration>& chain, int currentVersion);

void saveProject(const Project& project, const std::filesystem::path& dir);
Project loadProject(const std::filesystem::path& dir);

// Autosave: a copy of the project in `project.autosave.json` next to project.json, written between saves so that a crash loses little.
// A normal save removes it. hasNewerAutosave is true when it exists and is newer than project.json (the app stopped without saving).
void saveAutosave(const Project& project, const std::filesystem::path& dir);
bool hasNewerAutosave(const std::filesystem::path& dir);
void discardAutosave(const std::filesystem::path& dir);
// Puts the autosave in place of project.json (the saved file is kept as project.json.bak) and removes the autosave. Throws on a damaged autosave.
void restoreAutosave(const std::filesystem::path& dir);

// Project alternatives: named copies of project.json in <project>/alternatives/<name>.json. A name is 1 to 60 letters, digits, spaces, dots, dashes or underscores.
bool validAlternativeName(const std::string& name);
std::vector<std::string> listAlternatives(const std::filesystem::path& dir);  // sorted
void saveAlternative(const std::filesystem::path& dir, const std::string& name);  // the project.json on disk, copied; replaces one of that name
void deleteAlternative(const std::filesystem::path& dir, const std::string& name);
// The alternative becomes project.json (the saved file is kept as project.json.bak). Throws when it is missing or damaged.
void restoreAlternative(const std::filesystem::path& dir, const std::string& name);

}  // namespace lpc
