#pragma once
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lpc/audio/frame_source.h"
#include "lpc/model.h"

namespace lpc {

// Opens and caches the media files of a project. Thread-safe. Problems (missing, unreadable or
// mismatching files) are recorded as warnings and open() returns nullptr: the project still loads and
// the affected regions are silent.
class MediaStore {
public:
    // streaming = false decodes whole files into memory (deterministic; used for offline rendering).
    explicit MediaStore(std::filesystem::path projectDir = {}, bool streaming = true);

    void registerSource(const Uuid& mediaId, std::shared_ptr<audio::IFrameSource> source);
    std::shared_ptr<audio::IFrameSource> open(const MediaItem& item);

    int missingCount() const;
    std::vector<std::string> warnings() const;
    int takeUnderruns();  // total underruns of all streaming sources since the last call

private:
    mutable std::mutex mutex_;
    std::filesystem::path dir_;
    bool streaming_;
    std::unordered_map<Uuid, std::shared_ptr<audio::IFrameSource>> sources_;
    std::unordered_map<Uuid, std::shared_ptr<audio::StreamingSource>> streams_;
    std::unordered_set<Uuid> failed_;
    std::vector<std::string> warnings_;
};

}  // namespace lpc
