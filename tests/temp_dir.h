#pragma once
#include <filesystem>
#include <string>
#include "lpc/uuid.h"

namespace lpc::test {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const std::string& prefix = "lpc") {
        path = std::filesystem::temp_directory_path() / (prefix + "_" + Uuid::random().toString());
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

}  // namespace lpc::test
