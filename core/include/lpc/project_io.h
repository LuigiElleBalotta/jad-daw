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

}  // namespace lpc
