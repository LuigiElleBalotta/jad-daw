#include "lpc/adm_bwf.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>

namespace lpc {

std::int64_t AdmFile::frames() const {
    std::size_t n = 0;
    for (const auto& c : bed) n = std::max(n, c.size());
    for (const AdmObject& o : objects) n = std::max(n, o.samples.size());
    return static_cast<std::int64_t>(n);
}

namespace {

constexpr const char* kBedLabels[kAdmBedChannels] = {"RC_L", "RC_R", "RC_C", "RC_LFE", "RC_Lss", "RC_Rss", "RC_Lrs", "RC_Rrs", "RC_Lts", "RC_Rts"};
constexpr const char* kBedNames[kAdmBedChannels] = {"L", "R", "C", "LFE", "Lss", "Rss", "Lrs", "Rrs", "Lts", "Rts"};

std::string hex8(unsigned v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08x", v);
    return buf;
}

std::string hex4(unsigned v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04x", v);
    return buf;
}

std::string timeText(double seconds) {
    seconds = std::max(0.0, seconds);
    const int h = static_cast<int>(seconds / 3600);
    const int m = static_cast<int>((seconds - h * 3600) / 60);
    const double s = seconds - h * 3600 - m * 60;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%08.5f", h, m, s);
    return buf;
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6f", v);
    return buf;
}

std::string escape(const std::string& s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

unsigned objectCode(std::size_t index) { return 0x00031000u + static_cast<unsigned>(index) + 1; }
unsigned bedCode(int channel) { return 0x00011000u + static_cast<unsigned>(channel) + 1; }

void put16(std::string& out, std::uint16_t v) {
    out.push_back(static_cast<char>(v & 0xff));
    out.push_back(static_cast<char>(v >> 8));
}
void put32(std::string& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
void putChunk(std::string& out, const char* id, const std::string& body) {
    out.append(id, 4);
    put32(out, static_cast<std::uint32_t>(body.size()));
    out += body;
    if (body.size() % 2) out.push_back('\0');
}

struct Track {
    std::string uid, trackFormat, packFormat;
};

std::vector<Track> tracksOf(const AdmFile& f) {
    std::vector<Track> tracks;
    unsigned uid = 1;
    if (!f.bed.empty())
        for (int c = 0; c < kAdmBedChannels; ++c) tracks.push_back({"ATU_" + std::string(8 - std::to_string(uid).size(), '0') + std::to_string(uid++), "AT_" + hex8(bedCode(c)) + "_01", "AP_" + hex8(0x00011001u)});
    for (std::size_t i = 0; i < f.objects.size(); ++i)
        tracks.push_back({"ATU_" + std::string(8 - std::to_string(uid).size(), '0') + std::to_string(uid++), "AT_" + hex8(objectCode(i)) + "_01", "AP_" + hex8(objectCode(i))});
    return tracks;
}

}  // namespace

std::string admXml(const AdmFile& f) {
    const double total = static_cast<double>(f.frames()) / f.sampleRate;
    const std::vector<Track> tracks = tracksOf(f);
    std::string x;
    x += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    x += "<ebuCoreMain xmlns=\"urn:ebu:metadata-schema:ebuCore_2016\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" version=\"1\">\n";
    x += " <coreMetadata>\n  <format>\n   <audioFormatExtended version=\"ITU-R_BS.2076-2\">\n";
    x += "    <audioProgramme audioProgrammeID=\"APR_1001\" audioProgrammeName=\"Programme\" start=\"00:00:00.00000\" end=\"" + timeText(total) +
         "\"><audioContentIDRef>ACO_1001</audioContentIDRef></audioProgramme>\n";
    x += "    <audioContent audioContentID=\"ACO_1001\" audioContentName=\"Content\">";
    if (!f.bed.empty()) x += "<audioObjectIDRef>AO_1001</audioObjectIDRef>";
    for (std::size_t i = 0; i < f.objects.size(); ++i) x += "<audioObjectIDRef>AO_" + hex4(0x1002u + static_cast<unsigned>(i)) + "</audioObjectIDRef>";
    x += "</audioContent>\n";
    std::size_t at = 0;
    if (!f.bed.empty()) {
        x += "    <audioObject audioObjectID=\"AO_1001\" audioObjectName=\"Bed\" start=\"00:00:00.00000\" duration=\"" + timeText(total) +
             "\"><audioPackFormatIDRef>AP_" + hex8(0x00011001u) + "</audioPackFormatIDRef>";
        for (int c = 0; c < kAdmBedChannels; ++c) x += "<audioTrackUIDRef>" + tracks[at++].uid + "</audioTrackUIDRef>";
        x += "</audioObject>\n";
    }
    for (std::size_t i = 0; i < f.objects.size(); ++i)
        x += "    <audioObject audioObjectID=\"AO_" + hex4(0x1002u + static_cast<unsigned>(i)) + "\" audioObjectName=\"" + escape(f.objects[i].name) + "\" start=\"00:00:00.00000\" duration=\"" +
             timeText(total) + "\"><audioPackFormatIDRef>AP_" + hex8(objectCode(i)) + "</audioPackFormatIDRef><audioTrackUIDRef>" + tracks[at++].uid + "</audioTrackUIDRef></audioObject>\n";
    if (!f.bed.empty()) {
        x += "    <audioPackFormat audioPackFormatID=\"AP_" + hex8(0x00011001u) + "\" audioPackFormatName=\"Bed 7.1.2\" typeLabel=\"0001\" typeDefinition=\"DirectSpeakers\">";
        for (int c = 0; c < kAdmBedChannels; ++c) x += "<audioChannelFormatIDRef>AC_" + hex8(bedCode(c)) + "</audioChannelFormatIDRef>";
        x += "</audioPackFormat>\n";
    }
    for (std::size_t i = 0; i < f.objects.size(); ++i)
        x += "    <audioPackFormat audioPackFormatID=\"AP_" + hex8(objectCode(i)) + "\" audioPackFormatName=\"" + escape(f.objects[i].name) +
             "\" typeLabel=\"0003\" typeDefinition=\"Objects\"><audioChannelFormatIDRef>AC_" + hex8(objectCode(i)) + "</audioChannelFormatIDRef></audioPackFormat>\n";
    if (!f.bed.empty())
        for (int c = 0; c < kAdmBedChannels; ++c)
            x += "    <audioChannelFormat audioChannelFormatID=\"AC_" + hex8(bedCode(c)) + "\" audioChannelFormatName=\"" + kBedNames[c] +
                 "\" typeLabel=\"0001\" typeDefinition=\"DirectSpeakers\"><audioBlockFormat audioBlockFormatID=\"AB_" + hex8(bedCode(c)) + "_00000001\"><speakerLabel>" + kBedLabels[c] +
                 "</speakerLabel></audioBlockFormat></audioChannelFormat>\n";
    for (std::size_t i = 0; i < f.objects.size(); ++i) {
        const AdmObject& o = f.objects[i];
        x += "    <audioChannelFormat audioChannelFormatID=\"AC_" + hex8(objectCode(i)) + "\" audioChannelFormatName=\"" + escape(o.name) + "\" typeLabel=\"0003\" typeDefinition=\"Objects\">\n";
        std::vector<AdmPosition> pos = o.positions;
        if (pos.empty()) pos.push_back(AdmPosition{});
        for (std::size_t b = 0; b < pos.size(); ++b) {
            const double start = b == 0 ? 0.0 : pos[b].time;
            const double end = b + 1 < pos.size() ? pos[b + 1].time : total;
            char id[40];
            std::snprintf(id, sizeof id, "AB_%s_%08zx", hex8(objectCode(i)).c_str(), b + 1);
            x += std::string("     <audioBlockFormat audioBlockFormatID=\"") + id + "\" rtime=\"" + timeText(start) + "\" duration=\"" + timeText(std::max(0.0, end - start)) + "\">" +
                 "<position coordinate=\"X\">" + num(pos[b].x) + "</position><position coordinate=\"Y\">" + num(pos[b].y) + "</position><position coordinate=\"Z\">" + num(pos[b].z) + "</position>" +
                 "<width>" + num(pos[b].size * 2) + "</width><height>" + num(pos[b].size * 2) + "</height><depth>" + num(pos[b].size * 2) + "</depth></audioBlockFormat>\n";
        }
        x += "    </audioChannelFormat>\n";
    }
    const auto streamAndTrack = [&](unsigned code, const std::string& name, const std::string& type) {
        x += "    <audioStreamFormat audioStreamFormatID=\"AS_" + hex8(code) + "\" audioStreamFormatName=\"PCM_" + escape(name) +
             "\" formatLabel=\"0001\" formatDefinition=\"PCM\"><audioChannelFormatIDRef>AC_" + hex8(code) + "</audioChannelFormatIDRef><audioTrackFormatIDRef>AT_" + hex8(code) +
             "_01</audioTrackFormatIDRef></audioStreamFormat>\n";
        x += "    <audioTrackFormat audioTrackFormatID=\"AT_" + hex8(code) + "_01\" audioTrackFormatName=\"PCM_" + escape(name) +
             "\" formatLabel=\"0001\" formatDefinition=\"PCM\"><audioStreamFormatIDRef>AS_" + hex8(code) + "</audioStreamFormatIDRef></audioTrackFormat>\n";
        (void)type;
    };
    if (!f.bed.empty())
        for (int c = 0; c < kAdmBedChannels; ++c) streamAndTrack(bedCode(c), kBedNames[c], "bed");
    for (std::size_t i = 0; i < f.objects.size(); ++i) streamAndTrack(objectCode(i), f.objects[i].name, "object");
    for (const Track& t : tracks)
        x += "    <audioTrackUID UID=\"" + t.uid + "\" sampleRate=\"" + std::to_string(f.sampleRate) + "\" bitDepth=\"" + std::to_string(f.bitDepth) + "\"><audioTrackFormatIDRef>" +
             t.trackFormat + "</audioTrackFormatIDRef><audioPackFormatIDRef>" + t.packFormat + "</audioPackFormatIDRef></audioTrackUID>\n";
    x += "   </audioFormatExtended>\n  </format>\n </coreMetadata>\n</ebuCoreMain>\n";
    return x;
}

void writeAdmBwf(const std::filesystem::path& path, const AdmFile& f) {
    if (f.bitDepth != 16 && f.bitDepth != 24) throw std::runtime_error("an ADM BWF file is 16 or 24 bit");
    if (!f.bed.empty() && f.bed.size() != static_cast<std::size_t>(kAdmBedChannels)) throw std::runtime_error("the bed has 10 channels");
    const std::vector<Track> tracks = tracksOf(f);
    const int channels = static_cast<int>(tracks.size());
    if (channels == 0) throw std::runtime_error("nothing to write");
    const std::int64_t frames = f.frames();
    const int bytes = f.bitDepth / 8;
    const std::uint64_t dataBytes = static_cast<std::uint64_t>(frames) * static_cast<std::uint64_t>(channels) * static_cast<std::uint64_t>(bytes);
    if (dataBytes > 0xF0000000ull) throw std::runtime_error("the file would be over 4 GB");

    std::string fmt;
    put16(fmt, 0xFFFE);
    put16(fmt, static_cast<std::uint16_t>(channels));
    put32(fmt, static_cast<std::uint32_t>(f.sampleRate));
    put32(fmt, static_cast<std::uint32_t>(f.sampleRate * channels * bytes));
    put16(fmt, static_cast<std::uint16_t>(channels * bytes));
    put16(fmt, static_cast<std::uint16_t>(f.bitDepth));
    put16(fmt, 22);
    put16(fmt, static_cast<std::uint16_t>(f.bitDepth));
    put32(fmt, 0);  // no speaker mask: the channels are described by the ADM
    const unsigned char pcm[16] = {1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
    fmt.append(reinterpret_cast<const char*>(pcm), 16);

    std::string chna;
    put16(chna, static_cast<std::uint16_t>(channels));
    put16(chna, static_cast<std::uint16_t>(channels));
    for (int i = 0; i < channels; ++i) {
        put16(chna, static_cast<std::uint16_t>(i + 1));
        std::string uid = tracks[static_cast<std::size_t>(i)].uid, tf = tracks[static_cast<std::size_t>(i)].trackFormat, pf = tracks[static_cast<std::size_t>(i)].packFormat;
        uid.resize(12, '\0');
        tf.resize(14, '\0');
        pf.resize(11, '\0');
        chna += uid + tf + pf;
        chna.push_back('\0');
    }
    const std::string xml = admXml(f);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    std::string head;
    head += "WAVE";
    putChunk(head, "fmt ", fmt);
    out.write("RIFF", 4);
    const std::uint32_t riffSize = static_cast<std::uint32_t>(head.size() + 8 + dataBytes + (dataBytes % 2) + 8 + chna.size() + (chna.size() % 2) + 8 + xml.size() + (xml.size() % 2));
    char size[4];
    for (int i = 0; i < 4; ++i) size[i] = static_cast<char>((riffSize >> (8 * i)) & 0xff);
    out.write(size, 4);
    out.write(head.data(), static_cast<std::streamsize>(head.size()));
    out.write("data", 4);
    for (int i = 0; i < 4; ++i) size[i] = static_cast<char>((dataBytes >> (8 * i)) & 0xff);
    out.write(size, 4);
    // the samples, frame by frame: the bed channels, then the objects
    std::vector<const std::vector<float>*> sources;
    if (!f.bed.empty())
        for (const auto& c : f.bed) sources.push_back(&c);
    for (const AdmObject& o : f.objects) sources.push_back(&o.samples);
    std::string block;
    const std::int64_t chunk = 4096;
    for (std::int64_t base = 0; base < frames; base += chunk) {
        const std::int64_t n = std::min(chunk, frames - base);
        block.clear();
        for (std::int64_t i = 0; i < n; ++i)
            for (const auto* src : sources) {
                const std::size_t at = static_cast<std::size_t>(base + i);
                const float v = at < src->size() ? std::clamp((*src)[at], -1.0f, 1.0f) : 0.0f;
                if (bytes == 3) {
                    const std::int32_t s = static_cast<std::int32_t>(std::lround(v * 8388607.0f));
                    block.push_back(static_cast<char>(s & 0xff));
                    block.push_back(static_cast<char>((s >> 8) & 0xff));
                    block.push_back(static_cast<char>((s >> 16) & 0xff));
                } else {
                    const std::int32_t s = static_cast<std::int32_t>(std::lround(v * 32767.0f));
                    block.push_back(static_cast<char>(s & 0xff));
                    block.push_back(static_cast<char>((s >> 8) & 0xff));
                }
            }
        out.write(block.data(), static_cast<std::streamsize>(block.size()));
    }
    if (dataBytes % 2) out.put('\0');
    std::string tail;
    putChunk(tail, "chna", chna);
    putChunk(tail, "axml", xml);
    out.write(tail.data(), static_cast<std::streamsize>(tail.size()));
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

// ---------------------------------------------------------------------------------------------------- reading

namespace {

struct Node {
    std::string name;
    std::map<std::string, std::string> attrs;
    std::string text;
    std::vector<std::unique_ptr<Node>> children;
    const Node* child(const std::string& n) const {
        for (const auto& c : children)
            if (c->name == n) return c.get();
        return nullptr;
    }
    std::string attr(const std::string& n) const {
        const auto it = attrs.find(n);
        return it == attrs.end() ? std::string() : it->second;
    }
};

std::string unescape(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            const std::size_t e = s.find(';', i);
            if (e != std::string::npos) {
                const std::string ent = s.substr(i + 1, e - i - 1);
                if (ent == "amp") out += '&';
                else if (ent == "lt") out += '<';
                else if (ent == "gt") out += '>';
                else if (ent == "quot") out += '"';
                else if (ent == "apos") out += '\'';
                else out += s.substr(i, e - i + 1);
                i = e;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

// A small parser for the XML of an ADM: elements, attributes, text; comments, processing instructions and CDATA are skipped or kept as text.
std::unique_ptr<Node> parseXml(const std::string& xml) {
    auto root = std::make_unique<Node>();
    std::vector<Node*> stack{root.get()};
    std::size_t i = 0;
    while (i < xml.size()) {
        if (xml[i] != '<') {
            const std::size_t e = xml.find('<', i);
            const std::string t = xml.substr(i, e == std::string::npos ? std::string::npos : e - i);
            stack.back()->text += unescape(t);
            if (e == std::string::npos) break;
            i = e;
            continue;
        }
        if (xml.compare(i, 4, "<!--") == 0) {
            const std::size_t e = xml.find("-->", i);
            if (e == std::string::npos) break;
            i = e + 3;
            continue;
        }
        if (xml.compare(i, 9, "<![CDATA[") == 0) {
            const std::size_t e = xml.find("]]>", i);
            if (e == std::string::npos) break;
            stack.back()->text += xml.substr(i + 9, e - i - 9);
            i = e + 3;
            continue;
        }
        if (xml.compare(i, 2, "<?") == 0 || xml.compare(i, 2, "<!") == 0) {
            const std::size_t e = xml.find('>', i);
            if (e == std::string::npos) break;
            i = e + 1;
            continue;
        }
        if (xml.compare(i, 2, "</") == 0) {
            const std::size_t e = xml.find('>', i);
            if (e == std::string::npos) break;
            if (stack.size() > 1) stack.pop_back();
            i = e + 1;
            continue;
        }
        // an opening tag
        std::size_t j = i + 1;
        auto node = std::make_unique<Node>();
        while (j < xml.size() && !std::isspace(static_cast<unsigned char>(xml[j])) && xml[j] != '>' && xml[j] != '/') ++j;
        node->name = xml.substr(i + 1, j - i - 1);
        const std::size_t colon = node->name.find(':');
        if (colon != std::string::npos) node->name = node->name.substr(colon + 1);
        bool selfClosing = false;
        while (j < xml.size() && xml[j] != '>') {
            if (std::isspace(static_cast<unsigned char>(xml[j]))) {
                ++j;
                continue;
            }
            if (xml[j] == '/') {
                selfClosing = true;
                ++j;
                continue;
            }
            std::size_t k = j;
            while (k < xml.size() && xml[k] != '=' && xml[k] != '>' && !std::isspace(static_cast<unsigned char>(xml[k]))) ++k;
            std::string key = xml.substr(j, k - j);
            j = k;
            while (j < xml.size() && std::isspace(static_cast<unsigned char>(xml[j]))) ++j;
            if (j < xml.size() && xml[j] == '=') {
                ++j;
                while (j < xml.size() && std::isspace(static_cast<unsigned char>(xml[j]))) ++j;
                if (j < xml.size() && (xml[j] == '"' || xml[j] == '\'')) {
                    const char q = xml[j++];
                    const std::size_t e = xml.find(q, j);
                    if (e == std::string::npos) throw std::runtime_error("bad axml");
                    node->attrs[key] = unescape(xml.substr(j, e - j));
                    j = e + 1;
                }
            }
        }
        i = j < xml.size() ? j + 1 : j;
        Node* raw = node.get();
        stack.back()->children.push_back(std::move(node));
        if (!selfClosing) stack.push_back(raw);
    }
    return root;
}

void collect(const Node& n, const std::string& name, std::vector<const Node*>& out) {
    if (n.name == name) out.push_back(&n);
    for (const auto& c : n.children) collect(*c, name, out);
}

double parseTime(const std::string& s) {
    int h = 0, m = 0;
    double sec = 0;
    if (std::sscanf(s.c_str(), "%d:%d:%lf", &h, &m, &sec) == 3) return h * 3600.0 + m * 60.0 + sec;
    return 0.0;
}

std::uint32_t get32(const unsigned char* p) { return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
std::uint16_t get16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

std::string trimNul(const char* p, std::size_t n) {
    std::string s(p, n);
    const std::size_t z = s.find('\0');
    if (z != std::string::npos) s.resize(z);
    return s;
}

}  // namespace

AdmFile readAdmBwf(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    unsigned char head[12];
    in.read(reinterpret_cast<char*>(head), 12);
    if (!in || (std::memcmp(head, "RIFF", 4) != 0 && std::memcmp(head, "BW64", 4) != 0 && std::memcmp(head, "RF64", 4) != 0) || std::memcmp(head + 8, "WAVE", 4) != 0)
        throw std::runtime_error("not a wave file");
    std::uint64_t dataSize64 = 0;
    bool haveDs64 = false;
    int channels = 0, bits = 0, rate = 0, formatTag = 0;
    std::streamoff dataStart = -1;
    std::uint64_t dataBytes = 0;
    std::string chna, axml;
    while (in) {
        unsigned char ch[8];
        in.read(reinterpret_cast<char*>(ch), 8);
        if (!in) break;
        std::uint64_t size = get32(ch + 4);
        const std::string id(reinterpret_cast<char*>(ch), 4);
        const std::streamoff body = in.tellg();
        if (id == "ds64") {
            std::string d(28, '\0');
            in.read(d.data(), 28);
            dataSize64 = static_cast<std::uint64_t>(get32(reinterpret_cast<const unsigned char*>(d.data()) + 8)) | (static_cast<std::uint64_t>(get32(reinterpret_cast<const unsigned char*>(d.data()) + 12)) << 32);
            haveDs64 = true;
        } else if (id == "fmt ") {
            std::string d(size, '\0');
            in.read(d.data(), static_cast<std::streamsize>(size));
            const auto* p = reinterpret_cast<const unsigned char*>(d.data());
            formatTag = get16(p);
            channels = get16(p + 2);
            rate = static_cast<int>(get32(p + 4));
            bits = get16(p + 14);
            if (formatTag == 0xFFFE && size >= 26) formatTag = get16(p + 24);  // the first two bytes of the sub-format GUID
        } else if (id == "data") {
            if (size == 0xFFFFFFFFu && haveDs64) size = dataSize64;
            dataStart = body;
            dataBytes = size;
        } else if (id == "chna" || id == "axml") {
            std::string d(size, '\0');
            in.read(d.data(), static_cast<std::streamsize>(size));
            (id == "chna" ? chna : axml) = std::move(d);
        }
        in.clear();
        in.seekg(body + static_cast<std::streamoff>(size + (size % 2)));
    }
    if (channels <= 0 || dataStart < 0 || axml.empty()) throw std::runtime_error("not an ADM BWF file: there is no axml chunk");
    if (!(bits == 16 || bits == 24 || bits == 32)) throw std::runtime_error("unsupported sample format");
    const int bytes = bits / 8;
    const std::int64_t frames = static_cast<std::int64_t>(dataBytes / static_cast<std::uint64_t>(channels * bytes));

    // chna: audio track -> UID, track format and pack
    struct Entry {
        std::string uid, trackFormat, pack;
    };
    std::map<int, Entry> entries;
    if (chna.size() >= 4) {
        const auto* p = reinterpret_cast<const unsigned char*>(chna.data());
        const int uids = get16(p + 2);
        for (int i = 0; i < uids && 4 + (i + 1) * 40 <= static_cast<int>(chna.size()); ++i) {
            const unsigned char* e = p + 4 + i * 40;
            entries[get16(e)] = {trimNul(reinterpret_cast<const char*>(e + 2), 12), trimNul(reinterpret_cast<const char*>(e + 14), 14), trimNul(reinterpret_cast<const char*>(e + 28), 11)};
        }
    }

    const std::unique_ptr<Node> doc = parseXml(axml);
    std::vector<const Node*> v;
    std::map<std::string, const Node*> uidNodes, trackFormats, streamFormats, channelFormats, packFormats;
    collect(*doc, "audioTrackUID", v);
    for (const Node* n : v) uidNodes[n->attr("UID")] = n;
    v.clear();
    collect(*doc, "audioTrackFormat", v);
    for (const Node* n : v) trackFormats[n->attr("audioTrackFormatID")] = n;
    v.clear();
    collect(*doc, "audioStreamFormat", v);
    for (const Node* n : v) streamFormats[n->attr("audioStreamFormatID")] = n;
    v.clear();
    collect(*doc, "audioChannelFormat", v);
    for (const Node* n : v) channelFormats[n->attr("audioChannelFormatID")] = n;
    v.clear();
    collect(*doc, "audioPackFormat", v);
    for (const Node* n : v) packFormats[n->attr("audioPackFormatID")] = n;

    AdmFile file;
    file.sampleRate = rate;
    file.bitDepth = bits >= 24 ? 24 : 16;
    std::vector<std::vector<float>> audio(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(frames)));
    {
        in.clear();
        in.seekg(dataStart);
        std::vector<unsigned char> buf(static_cast<std::size_t>(4096 * channels * bytes));
        for (std::int64_t base = 0; base < frames; base += 4096) {
            const std::int64_t n = std::min<std::int64_t>(4096, frames - base);
            in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n * channels * bytes));
            const unsigned char* p = buf.data();
            for (std::int64_t i = 0; i < n; ++i)
                for (int c = 0; c < channels; ++c, p += bytes) {
                    float s = 0.0f;
                    if (bytes == 2) s = static_cast<float>(static_cast<std::int16_t>(p[0] | (p[1] << 8))) / 32768.0f;
                    else if (bytes == 3) s = static_cast<float>(static_cast<std::int32_t>(static_cast<std::uint32_t>(p[0]) << 8 | static_cast<std::uint32_t>(p[1]) << 16 | static_cast<std::uint32_t>(p[2]) << 24) >> 8) / 8388608.0f;
                    else if (formatTag == 3) {
                        std::uint32_t u = get32(p);
                        std::memcpy(&s, &u, 4);
                    } else s = static_cast<float>(static_cast<std::int32_t>(get32(p))) / 2147483648.0f;
                    audio[static_cast<std::size_t>(c)][static_cast<std::size_t>(base + i)] = s;
                }
        }
    }

    std::vector<std::vector<float>> bed;
    for (int c = 1; c <= channels; ++c) {
        const auto e = entries.find(c);
        std::string trackFormatId = e == entries.end() ? std::string() : e->second.trackFormat;
        if (trackFormatId.empty() && e != entries.end()) {
            const auto u = uidNodes.find(e->second.uid);
            if (u != uidNodes.end())
                if (const Node* r = u->second->child("audioTrackFormatIDRef")) trackFormatId = r->text;
        }
        const auto tf = trackFormats.find(trackFormatId);
        const Node* stream = nullptr;
        if (tf != trackFormats.end())
            if (const Node* r = tf->second->child("audioStreamFormatIDRef")) {
                const auto s = streamFormats.find(r->text);
                if (s != streamFormats.end()) stream = s->second;
            }
        const Node* channelFormat = nullptr;
        if (stream)
            if (const Node* r = stream->child("audioChannelFormatIDRef")) {
                const auto cf = channelFormats.find(r->text);
                if (cf != channelFormats.end()) channelFormat = cf->second;
            }
        if (!channelFormat && !trackFormatId.empty()) {  // some files name the channel format only through the pack of the track
            const auto pk = e == entries.end() ? packFormats.end() : packFormats.find(e->second.pack);
            if (pk != packFormats.end())
                if (const Node* r = pk->second->child("audioChannelFormatIDRef")) {
                    const auto cf = channelFormats.find(r->text);
                    if (cf != channelFormats.end()) channelFormat = cf->second;
                }
        }
        const std::string type = channelFormat ? channelFormat->attr("typeDefinition") : std::string("DirectSpeakers");
        if (type != "Objects") {
            if (bed.size() < static_cast<std::size_t>(kAdmBedChannels)) bed.push_back(std::move(audio[static_cast<std::size_t>(c - 1)]));
            continue;
        }
        AdmObject object;
        object.name = channelFormat->attr("audioChannelFormatName");
        object.samples = std::move(audio[static_cast<std::size_t>(c - 1)]);
        for (const auto& blockPtr : channelFormat->children) {
            const Node& block = *blockPtr;
            if (block.name != "audioBlockFormat") continue;
            AdmPosition pos;
            pos.time = parseTime(block.attr("rtime"));
            bool polar = false;
            double az = 0, el = 0, dist = 1;
            for (const auto& pp : block.children) {
                if (pp->name == "position") {
                    const std::string coord = pp->attr("coordinate");
                    const double value = std::atof(pp->text.c_str());
                    if (coord == "X") pos.x = value;
                    else if (coord == "Y") pos.y = value;
                    else if (coord == "Z") pos.z = value;
                    else if (coord == "azimuth") { az = value; polar = true; }
                    else if (coord == "elevation") { el = value; polar = true; }
                    else if (coord == "distance") dist = value;
                } else if (pp->name == "width") pos.size = std::clamp(std::atof(pp->text.c_str()) / 2.0, 0.0, 1.0);
            }
            if (polar) {
                const double d2r = 3.14159265358979323846 / 180.0;
                pos.x = -dist * std::sin(az * d2r) * std::cos(el * d2r);
                pos.y = dist * std::cos(az * d2r) * std::cos(el * d2r);
                pos.z = dist * std::sin(el * d2r);
            }
            pos.x = std::clamp(pos.x, -1.0, 1.0);
            pos.y = std::clamp(pos.y, -1.0, 1.0);
            pos.z = std::clamp(pos.z, 0.0, 1.0);
            object.positions.push_back(pos);
        }
        std::stable_sort(object.positions.begin(), object.positions.end(), [](const AdmPosition& a, const AdmPosition& b) { return a.time < b.time; });
        file.objects.push_back(std::move(object));
    }
    if (!bed.empty()) {
        while (bed.size() < static_cast<std::size_t>(kAdmBedChannels)) bed.emplace_back(static_cast<std::size_t>(frames), 0.0f);
        file.bed = std::move(bed);
    }
    return file;
}

}  // namespace lpc
