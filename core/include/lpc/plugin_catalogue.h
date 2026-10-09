#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "lpc/plugin_host.h"

namespace lpc {

enum class ScanStatus { Ok, Failed };

struct ScanEntry {
    std::string path;
    std::int64_t mtime = 0, size = 0;
    ScanStatus status = ScanStatus::Ok;
    std::string reason;  // why a file failed
    std::vector<PluginDescriptor> descriptors;
};

struct FileInfo {
    std::string path;
    std::int64_t mtime = 0, size = 0;
};

enum class ScanMode { NewAndChanged, Failed, All };

// The result of scanning plug-in files, kept in a JSON file between runs.
class PluginCatalogue {
public:
    static PluginCatalogue load(const std::filesystem::path& file);  // a missing or unreadable file gives an empty catalogue
    void save(const std::filesystem::path& file) const;              // temp file then rename; throws std::runtime_error on I/O errors
    const std::vector<ScanEntry>& entries() const { return entries_; }
    const ScanEntry* find(const std::string& path) const;
    void set(ScanEntry entry);
    void remove(const std::string& path);
    void clear() { entries_.clear(); }
    std::vector<PluginDescriptor> descriptors() const;

private:
    std::vector<ScanEntry> entries_;
};

// Which of `present` need scanning. Entries of files that are gone are removed from the catalogue.
//   NewAndChanged: files not in the catalogue or whose mtime or size changed (failed ones too, when changed).
//   Failed:        files whose entry failed.
//   All:           every file; the catalogue is emptied first.
std::vector<FileInfo> filesToScan(PluginCatalogue& catalogue, const std::vector<FileInfo>& present, ScanMode mode);

// %LOCALAPPDATA%/JAD/JAD Daw on Windows, $XDG_CONFIG_HOME or ~/.config then /JAD/JAD Daw elsewhere.
std::filesystem::path appConfigDir();

}  // namespace lpc
