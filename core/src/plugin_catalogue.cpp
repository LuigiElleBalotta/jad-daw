#define _CRT_SECURE_NO_WARNINGS
#include "lpc/plugin_catalogue.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace lpc {

namespace {

nlohmann::json toJson(const PluginDescriptor& d) {
    return {{"id", d.id}, {"name", d.name}, {"vendor", d.vendor}, {"version", d.version}, {"path", d.path}, {"native", d.native}};
}
PluginDescriptor descriptorFrom(const nlohmann::json& j) {
    PluginDescriptor d;
    j.at("id").get_to(d.id);
    j.at("name").get_to(d.name);
    j.at("vendor").get_to(d.vendor);
    j.at("version").get_to(d.version);
    j.at("path").get_to(d.path);
    j.at("native").get_to(d.native);
    return d;
}

}  // namespace

const ScanEntry* PluginCatalogue::find(const std::string& path) const {
    for (const ScanEntry& e : entries_)
        if (e.path == path) return &e;
    return nullptr;
}

void PluginCatalogue::set(ScanEntry entry) {
    for (ScanEntry& e : entries_)
        if (e.path == entry.path) {
            e = std::move(entry);
            return;
        }
    entries_.push_back(std::move(entry));
}

void PluginCatalogue::remove(const std::string& path) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const ScanEntry& e) { return e.path == path; }), entries_.end());
}

std::vector<PluginDescriptor> PluginCatalogue::descriptors() const {
    std::vector<PluginDescriptor> out;
    for (const ScanEntry& e : entries_)
        if (e.status == ScanStatus::Ok) out.insert(out.end(), e.descriptors.begin(), e.descriptors.end());
    return out;
}

PluginCatalogue PluginCatalogue::load(const std::filesystem::path& file) {
    PluginCatalogue c;
    try {
        std::ifstream in(file, std::ios::binary);
        if (!in) return c;
        const nlohmann::json doc = nlohmann::json::parse(in);
        if (doc.at("version").get<int>() != 1) return c;
        std::vector<ScanEntry> entries;
        for (const auto& j : doc.at("entries")) {
            ScanEntry e;
            j.at("path").get_to(e.path);
            j.at("mtime").get_to(e.mtime);
            j.at("size").get_to(e.size);
            e.status = j.at("status").get<std::string>() == "ok" ? ScanStatus::Ok : ScanStatus::Failed;
            e.reason = j.value("reason", std::string());
            for (const auto& d : j.at("descriptors")) e.descriptors.push_back(descriptorFrom(d));
            entries.push_back(std::move(e));
        }
        c.entries_ = std::move(entries);
    } catch (const std::exception&) {
        c.entries_.clear();  // a cache that cannot be read is rebuilt by the next scan
    }
    return c;
}

void PluginCatalogue::save(const std::filesystem::path& file) const {
    nlohmann::json entries = nlohmann::json::array();
    for (const ScanEntry& e : entries_) {
        nlohmann::json descriptors = nlohmann::json::array();
        for (const PluginDescriptor& d : e.descriptors) descriptors.push_back(toJson(d));
        entries.push_back({{"path", e.path},
                           {"mtime", e.mtime},
                           {"size", e.size},
                           {"status", e.status == ScanStatus::Ok ? "ok" : "failed"},
                           {"reason", e.reason},
                           {"descriptors", descriptors}});
    }
    const nlohmann::json doc = {{"version", 1}, {"entries", entries}};
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
        out << doc.dump(1);
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        throw std::runtime_error("cannot replace " + file.string());
    }
}

std::vector<FileInfo> filesToScan(PluginCatalogue& catalogue, const std::vector<FileInfo>& present, ScanMode mode) {
    if (mode == ScanMode::All) {
        catalogue.clear();
        return present;
    }
    std::unordered_set<std::string> presentPaths;
    for (const FileInfo& f : present) presentPaths.insert(f.path);
    std::vector<std::string> gone;
    for (const ScanEntry& e : catalogue.entries())
        if (!presentPaths.count(e.path)) gone.push_back(e.path);
    for (const std::string& p : gone) catalogue.remove(p);

    std::vector<FileInfo> out;
    for (const FileInfo& f : present) {
        const ScanEntry* e = catalogue.find(f.path);
        const bool changed = !e || e->mtime != f.mtime || e->size != f.size;
        if (mode == ScanMode::NewAndChanged ? changed : (e && e->status == ScanStatus::Failed)) out.push_back(f);
    }
    return out;
}

std::filesystem::path appConfigDir() {
#ifdef _WIN32
    if (const char* v = std::getenv("LOCALAPPDATA")) return std::filesystem::path(v) / "JAD" / "JAD Daw";
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME")) return std::filesystem::path(x) / "JAD" / "JAD Daw";
    if (const char* h = std::getenv("HOME")) return std::filesystem::path(h) / ".config" / "JAD" / "JAD Daw";
#endif
    return std::filesystem::path(".") / "JAD Daw";
}

}  // namespace lpc
