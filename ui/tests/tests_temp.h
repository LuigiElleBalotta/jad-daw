#pragma once
#include <QTemporaryDir>
#include <filesystem>

// A temporary folder removed when the object goes away.
class TempDir {
public:
    TempDir() { dir_.setAutoRemove(true); }
    std::filesystem::path path() const { return std::filesystem::path(dir_.path().toStdU16String()); }

private:
    QTemporaryDir dir_;
};
