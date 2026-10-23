// Project assets, as in Logic: the audio of a project is copied into it or referenced where it is (Project Settings > Assets). The files that are referenced
// can be copied in later (File > Project Management > Consolidate, and Save for the asset types that are ticked), and when a project is opened whose files are
// not where it says, they are listed and can be located one by one or searched for in a folder.
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <filesystem>
#include <set>

#include <nlohmann/json.hpp>

#include "bridge/project_controller.h"
#include "lpc/commands.h"
#include "lpc/project_host.h"
#include "lpc/sampler.h"
#include "lpc/validation.h"
#include "lpc/wav.h"

namespace jad {
namespace {

std::filesystem::path fileOf(const std::filesystem::path& dir, const std::string& path) {
    return dir / std::filesystem::path(std::u8string(path.begin(), path.end()));  // an absolute path wins over the folder
}

std::string textOf(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

// <project>/audio/<name> that is not taken yet
std::filesystem::path freeNameIn(const std::filesystem::path& dir, const std::filesystem::path& original) {
    std::filesystem::create_directories(dir);
    const std::u16string stem = original.stem().u16string(), ext = original.extension().u16string();
    for (int n = 1;; ++n) {
        const std::filesystem::path candidate = dir / (n == 1 ? stem + ext : stem + u" (" + QString::number(n).toStdU16String() + u")" + ext);
        if (!std::filesystem::exists(candidate)) return candidate;
    }
}

}  // namespace

int ProjectController::externalMediaCount() const {
    int count = 0;
    for (auto it = mediaPaths_.cbegin(); it != mediaPaths_.cend(); ++it)
        if (lpc::isExternalMediaPath(it.value().toStdString())) ++count;
    return count;
}

// Copies the files the project only references into <project>/audio and points the media at the copies, one undo step each. `audio` is for the files of regions,
// `sampler` for those the zones of a Sampler play. Files that are not there are left alone (they are listed as missing).
int ProjectController::consolidateMedia(bool audio, bool sampler) {
    if (!host_) return 0;
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    std::set<std::string> samplerFiles;
    for (const lpc::Track& t : project.tracks)
        if (t.instrument)
            for (const lpc::Uuid& id : lpc::samplerMediaIds(*t.instrument)) samplerFiles.insert(id.toString());
    int copied = 0;
    for (const lpc::MediaItem& m : project.mediaPool) {
        if (!lpc::isExternalMediaPath(m.path)) continue;
        const bool isSampler = samplerFiles.count(m.id.toString()) > 0;
        if (isSampler ? !sampler : !audio) continue;
        const std::filesystem::path source = fileOf(dir_, m.path);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(source, ec)) continue;
        try {
            const std::filesystem::path target = freeNameIn(dir_ / "audio", source.filename());
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::none);
            const std::string relative = textOf(std::filesystem::path("audio") / target.filename());
            if (host_->submit(lpc::makeSetMediaPath(m.id, relative)).get()) {
                std::filesystem::remove(target, ec);  // the project refused: no stray copy
                continue;
            }
            ++copied;
        } catch (const std::exception&) {
        }
    }
    return copied;
}

int ProjectController::consolidateOnSave() {
    if (!host_ || externalMediaCount() == 0) return 0;
    return consolidateMedia(projectSetting("copyAudioFiles", "1") != QLatin1String("0"), projectSetting("copySamplerAudio", "1") != QLatin1String("0"));
}

QVariantList ProjectController::missingMedia() const {
    QVariantList out;
    for (auto it = mediaPaths_.cbegin(); it != mediaPaths_.cend(); ++it) {
        const std::string path = it.value().toStdString();
        std::error_code ec;
        if (std::filesystem::is_regular_file(fileOf(dir_, path), ec)) continue;
        out.append(QVariantMap{{"id", it.key()}, {"name", QString::fromStdString(std::filesystem::path(std::u8string(path.begin(), path.end())).filename().string())},
                               {"path", it.value()}, {"external", lpc::isExternalMediaPath(path)}});
    }
    return out;
}

bool ProjectController::relinkMedia(const QString& mediaId, const QUrl& file) {
    if (!host_) return false;
    const auto id = lpc::Uuid::parse(mediaId.toStdString());
    if (!id) return false;
    const std::filesystem::path found = file.toLocalFile().toStdU16String();
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    const lpc::MediaItem* item = project.findMedia(*id);
    if (!item) return false;
    try {
        lpc::WavFile wav(found);
        if (wav.sampleRate() != item->sampleRate || wav.channels() != item->channels) {
            emit notice("That file does not have the sample rate and channels of the audio it should replace");
            return false;
        }
    } catch (const std::exception&) {
        emit notice("That file cannot be read as audio");
        return false;
    }
    // inside the project folder the path stays relative
    std::string path = textOf(found);
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(found, dir_, ec);
    if (!ec && !relative.empty() && relative.generic_string().rfind("..", 0) != 0) path = textOf(relative);
    sendCommand({{"type", "set_media_path"}, {"mediaId", mediaId.toStdString()}, {"path", path}});
    return true;
}

int ProjectController::searchMissingMedia(const QUrl& folder) {
    if (!host_) return 0;
    const QVariantList missing = missingMedia();
    if (missing.isEmpty()) return 0;
    QHash<QString, QStringList> byName;  // lower-case file name -> where the folder has files of that name
    int seen = 0;
    QDirIterator it(folder.toLocalFile(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext() && seen < 400000) {
        const QString path = it.next();
        ++seen;
        byName[QFileInfo(path).fileName().toLower()].append(path);
    }
    nlohmann::json commands = nlohmann::json::array();
    const lpc::Project project = host_->read([](const lpc::Project& p) { return p; }).get();
    for (const QVariant& v : missing) {
        const QVariantMap m = v.toMap();
        const QStringList candidates = byName.value(m.value("name").toString().toLower());
        const auto id = lpc::Uuid::parse(m.value("id").toString().toStdString());
        const lpc::MediaItem* item = id ? project.findMedia(*id) : nullptr;
        if (!item) continue;
        for (const QString& c : candidates) {
            try {
                lpc::WavFile wav(std::filesystem::path(c.toStdU16String()));
                if (wav.sampleRate() != item->sampleRate || wav.channels() != item->channels) continue;
            } catch (const std::exception&) {
                continue;
            }
            std::string path = textOf(std::filesystem::path(c.toStdU16String()));
            std::error_code ec;
            const std::filesystem::path relative = std::filesystem::relative(std::filesystem::path(c.toStdU16String()), dir_, ec);
            if (!ec && !relative.empty() && relative.generic_string().rfind("..", 0) != 0) path = textOf(relative);
            commands.push_back({{"type", "set_media_path"}, {"mediaId", m.value("id").toString().toStdString()}, {"path", path}});
            break;
        }
    }
    if (commands.empty()) return 0;
    const int found = static_cast<int>(commands.size());
    sendCommand(found == 1 ? commands.front() : nlohmann::json{{"type", "transaction"}, {"commands", commands}});
    return found;
}

}  // namespace jad
