#define _CRT_SECURE_NO_WARNINGS
#include "plugin_scanner.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include <juce_core/juce_core.h>
#include <nlohmann/json.hpp>

namespace lpc {

namespace {

namespace fs = std::filesystem;

std::int64_t seconds(const fs::file_time_type& t) {
    return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
}

// A VST3 is a single file or a folder (bundle); the file that changes with an update is the binary inside it.
FileInfo statPlugin(const fs::path& path) {
    FileInfo info;
    info.path = path.string();
    std::error_code ec;
    fs::path probe = path;
    if (fs::is_directory(path, ec)) {
        for (const auto& entry : fs::directory_iterator(path / "Contents" / "x86_64-win", ec)) {
            if (entry.path().extension() == ".vst3") {
                probe = entry.path();
                break;
            }
        }
    }
    info.mtime = seconds(fs::last_write_time(probe, ec));
    info.size = fs::is_regular_file(probe, ec) ? static_cast<std::int64_t>(fs::file_size(probe, ec)) : 0;
    return info;
}

std::vector<FileInfo> findPlugins(const std::vector<fs::path>& folders) {
    std::vector<FileInfo> out;
    for (const fs::path& folder : folders) {
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) continue;
        for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
             it.increment(ec)) {
            if (it->path().extension() != ".vst3") continue;
            if (it->is_directory(ec)) it.disable_recursion_pending();  // a bundle: its insides are not plug-ins of their own
            out.push_back(statPlugin(it->path()));
        }
    }
    std::sort(out.begin(), out.end(), [](const FileInfo& a, const FileInfo& b) { return a.path < b.path; });
    out.erase(std::unique(out.begin(), out.end(), [](const FileInfo& a, const FileInfo& b) { return a.path == b.path; }), out.end());
    return out;
}

}  // namespace

PluginScanner::PluginScanner(Options options) : options_(std::move(options)), catalogue_(PluginCatalogue::load(options_.cacheFile)) {}

PluginScanner::~PluginScanner() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

std::vector<fs::path> PluginScanner::defaultFolders() {
    std::vector<fs::path> out;
    if (const char* common = std::getenv("COMMONPROGRAMFILES")) out.push_back(fs::path(common) / "VST3");
    if (const char* local = std::getenv("LOCALAPPDATA")) out.push_back(fs::path(local) / "Programs" / "Common" / "VST3");
    return out;
}

PluginCatalogue PluginScanner::snapshot() const {
    std::lock_guard lock(mutex_);
    return catalogue_;
}

void PluginScanner::setChangedListener(std::function<void()> listener) {
    std::lock_guard lock(mutex_);
    listener_ = std::move(listener);
}

void PluginScanner::notify() {
    std::function<void()> listener;
    {
        std::lock_guard lock(mutex_);
        listener = listener_;
    }
    if (listener) listener();
}

void PluginScanner::start(ScanMode mode) {
    if (running_.exchange(true)) return;
    if (thread_.joinable()) thread_.join();
    cancel_ = false;
    done_ = 0;
    total_ = 0;
    thread_ = std::thread([this, mode] { run(mode); });
}

void PluginScanner::wait() {
    if (thread_.joinable()) thread_.join();
}

ScanEntry PluginScanner::scanFile(const FileInfo& file) {
    ScanEntry entry;
    entry.path = file.path;
    entry.mtime = file.mtime;
    entry.size = file.size;
    entry.status = ScanStatus::Failed;

    juce::StringArray command;
    command.add(juce::String(options_.scannerExe.string()));
    for (const std::string& arg : options_.scannerArgs) command.add(juce::String::fromUTF8(arg.c_str()));
    command.add(juce::String::fromUTF8(file.path.c_str()));
    juce::ChildProcess child;
    if (!child.start(command, juce::ChildProcess::wantStdOut)) {
        entry.reason = "cannot start the scanner";
        return entry;
    }
    // Wait in short steps so that a cancel (the scanner is being destroyed) does not wait for a hung plug-in.
    const auto startedAt = std::chrono::steady_clock::now();
    while (!child.waitForProcessToFinish(100)) {
        if (cancel_) {
            child.kill();
            entry.reason = "cancelled";
            return entry;
        }
        if (std::chrono::steady_clock::now() - startedAt >= std::chrono::milliseconds(options_.timeoutMs)) {
            child.kill();
            entry.reason = "timed out";
            return entry;
        }
    }
    const std::string output = child.readAllProcessOutput().toStdString();
    const std::uint32_t exitCode = child.getExitCode();
    try {
        const nlohmann::json j = nlohmann::json::parse(output);
        if (j.at("ok").get<bool>()) {
            for (const auto& d : j.at("descriptors")) {
                PluginDescriptor pd;
                pd.id = d.at("id").get<std::string>();
                pd.name = d.at("name").get<std::string>();
                pd.vendor = d.at("vendor").get<std::string>();
                pd.version = d.at("version").get<std::string>();
                pd.native = d.at("native").get<std::string>();
                pd.path = file.path;
                entry.descriptors.push_back(std::move(pd));
            }
            entry.status = ScanStatus::Ok;
            return entry;
        }
        entry.reason = j.at("reason").get<std::string>();
    } catch (const std::exception&) {
        entry.reason = exitCode != 0 ? "crashed (exit code " + std::to_string(exitCode) + ")" : "no valid answer";
    }
    return entry;
}

void PluginScanner::run(ScanMode mode) {
    std::vector<FileInfo> todo;
    {
        PluginCatalogue working = snapshot();
        todo = filesToScan(working, findPlugins(options_.folders), mode);
        std::lock_guard lock(mutex_);
        catalogue_ = std::move(working);  // vanished files are gone, `All` starts empty
    }
    total_ = static_cast<int>(todo.size());
    notify();
    for (const FileInfo& file : todo) {
        if (cancel_) break;
        ScanEntry entry = scanFile(file);
        if (cancel_) break;  // a cancelled file is not a failed file: it is simply scanned next time
        {
            std::lock_guard lock(mutex_);
            catalogue_.set(std::move(entry));
            try {
                catalogue_.save(options_.cacheFile);
            } catch (const std::exception&) {
                // the scan result stays valid in memory; the next run scans again
            }
        }
        ++done_;
        notify();
    }
    {
        std::lock_guard lock(mutex_);
        try {
            catalogue_.save(options_.cacheFile);
        } catch (const std::exception&) {
        }
    }
    running_ = false;
    notify();
}

}  // namespace lpc
