#include "lpc/media_store.h"

namespace lpc {

namespace {
std::string pathText(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
}  // namespace

MediaStore::MediaStore(std::filesystem::path projectDir, bool streaming) : dir_(std::move(projectDir)), streaming_(streaming) {}

void MediaStore::registerSource(const Uuid& mediaId, std::shared_ptr<audio::IFrameSource> source) {
    std::lock_guard lock(mutex_);
    sources_[mediaId] = std::move(source);
    failed_.erase(mediaId);
}

std::shared_ptr<audio::IFrameSource> MediaStore::open(const MediaItem& item) {
    std::lock_guard lock(mutex_);
    if (auto it = sources_.find(item.id); it != sources_.end()) return it->second;
    if (failed_.count(item.id)) return nullptr;

    auto fail = [&](const std::string& why) -> std::shared_ptr<audio::IFrameSource> {
        failed_.insert(item.id);
        warnings_.push_back(item.path + ": " + why);
        return nullptr;
    };
    if (dir_.empty()) return fail("no project folder to load media from");

    const std::filesystem::path file = dir_ / std::filesystem::path(std::u8string(item.path.begin(), item.path.end()));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return fail("file not found (" + pathText(file) + ")");
    try {
        std::shared_ptr<audio::IFrameSource> source;
        if (streaming_) {
            auto stream = std::make_shared<audio::StreamingSource>(file);
            streams_[item.id] = stream;
            source = stream;
        } else {
            const WavData d = readWav(file);
            source = std::make_shared<audio::MemorySource>(d.sampleRate, d.channels, d.samples);
        }
        if (source->sampleRate() != item.sampleRate) {
            streams_.erase(item.id);
            return fail("sample rate " + std::to_string(source->sampleRate()) + " does not match the project media entry (" +
                        std::to_string(item.sampleRate) + ")");
        }
        sources_[item.id] = source;
        return source;
    } catch (const std::exception& e) {
        return fail(std::string("cannot read file: ") + e.what());
    }
}

int MediaStore::missingCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<int>(failed_.size());
}

std::vector<std::string> MediaStore::warnings() const {
    std::lock_guard lock(mutex_);
    return warnings_;
}

int MediaStore::takeUnderruns() {
    std::lock_guard lock(mutex_);
    int total = 0;
    for (auto& [id, s] : streams_) total += s->takeUnderruns();
    return total;
}

}  // namespace lpc
