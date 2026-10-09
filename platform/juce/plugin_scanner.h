#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "lpc/plugin_catalogue.h"

namespace lpc {

// Scans VST3 files one by one in lpc-plugin-scanner child processes and keeps the result in a JSON cache.
class PluginScanner {
public:
    struct Options {
        std::filesystem::path scannerExe, cacheFile;
        std::vector<std::filesystem::path> folders;
        int timeoutMs = 30000;
    };
    explicit PluginScanner(Options options);
    ~PluginScanner();
    PluginScanner(const PluginScanner&) = delete;
    PluginScanner& operator=(const PluginScanner&) = delete;

    void start(ScanMode mode);  // no-op while a scan is running
    void wait();                // blocks until the running scan has ended
    bool running() const { return running_.load(); }
    int done() const { return done_.load(); }
    int total() const { return total_.load(); }
    PluginCatalogue snapshot() const;
    void setChangedListener(std::function<void()> listener);  // called from the scan thread

    static std::vector<std::filesystem::path> defaultFolders();

private:
    void run(ScanMode mode);
    ScanEntry scanFile(const FileInfo& file);
    void notify();

    Options options_;
    mutable std::mutex mutex_;  // catalogue_ and listener_
    PluginCatalogue catalogue_;
    std::function<void()> listener_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<int> done_{0}, total_{0};
};

}  // namespace lpc
