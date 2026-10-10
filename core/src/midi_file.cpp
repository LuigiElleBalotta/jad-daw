#include "lpc/midi_file.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "lpc/tempo_map.h"

namespace lpc {

namespace {

struct Reader {
    const std::vector<std::uint8_t>& b;
    std::size_t pos = 0;
    std::size_t end = 0;

    bool eof() const { return pos >= end; }
    std::uint8_t u8() {
        if (pos >= end) throw std::runtime_error("the MIDI file ends in the middle of an event");
        return b[pos++];
    }
    std::uint32_t be(int bytes) {
        std::uint32_t v = 0;
        for (int i = 0; i < bytes; ++i) v = (v << 8) | u8();
        return v;
    }
    std::uint32_t varLen() {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const std::uint8_t c = u8();
            v = (v << 7) | (c & 0x7f);
            if (!(c & 0x80)) return v;
        }
        throw std::runtime_error("a MIDI delta time is too long");
    }
};

void putBe(std::vector<std::uint8_t>& out, std::uint32_t v, int bytes) {
    for (int i = bytes - 1; i >= 0; --i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}

void putVar(std::vector<std::uint8_t>& out, std::uint32_t v) {
    std::uint8_t buf[5];
    int n = 0;
    buf[n++] = v & 0x7f;
    while (v >>= 7) buf[n++] = static_cast<std::uint8_t>((v & 0x7f) | 0x80);
    while (n > 0) out.push_back(buf[--n]);
}

void putChunk(std::vector<std::uint8_t>& out, const char* id, const std::vector<std::uint8_t>& body) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(id[i]));
    putBe(out, static_cast<std::uint32_t>(body.size()), 4);
    out.insert(out.end(), body.begin(), body.end());
}

void putMeta(std::vector<std::uint8_t>& t, std::uint32_t delta, std::uint8_t type, const std::vector<std::uint8_t>& data) {
    putVar(t, delta);
    t.push_back(0xff);
    t.push_back(type);
    putVar(t, static_cast<std::uint32_t>(data.size()));
    t.insert(t.end(), data.begin(), data.end());
}

}  // namespace

MidiFileData parseMidiFile(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 14 || bytes[0] != 'M' || bytes[1] != 'T' || bytes[2] != 'h' || bytes[3] != 'd') throw std::runtime_error("this is not a MIDI file");
    Reader head{bytes, 4, bytes.size()};
    const std::uint32_t headLen = head.be(4);
    if (headLen < 6 || 8 + static_cast<std::size_t>(headLen) > bytes.size()) throw std::runtime_error("the MIDI file header is damaged");
    head.be(2);  // format: 0, 1 and 2 are read the same way
    const int trackCount = static_cast<int>(head.be(2));
    const std::uint32_t division = head.be(2);
    if (division & 0x8000) throw std::runtime_error("MIDI files with SMPTE time are not supported");
    if (division == 0) throw std::runtime_error("the MIDI file has no time division");

    MidiFileData out;
    std::size_t pos = 8 + headLen;
    for (int ti = 0; ti < trackCount && pos + 8 <= bytes.size(); ++ti) {
        Reader chunkHead{bytes, pos, bytes.size()};
        const std::uint32_t id = chunkHead.be(4);
        const std::uint32_t len = chunkHead.be(4);
        const std::size_t bodyStart = pos + 8;
        const std::size_t bodyEnd = std::min<std::size_t>(bodyStart + len, bytes.size());
        pos = bodyStart + len;
        if (id != 0x4d54726b) continue;  // "MTrk"; other chunks are skipped

        Reader r{bytes, bodyStart, bodyEnd};
        std::uint64_t tick = 0;
        std::uint8_t running = 0;
        std::string trackName;
        struct Open {
            Ticks start;
            std::uint8_t velocity;
        };
        std::map<std::pair<int, int>, Open> open;  // (channel, note) -> the note-on
        std::map<int, std::vector<MidiNote>> byChannel;
        const auto toTicks = [&](std::uint64_t t) { return static_cast<Ticks>(std::llround(static_cast<double>(t) * kPPQ / division)); };
        const auto close = [&](int ch, int note, std::uint64_t at) {
            const auto it = open.find({ch, note});
            if (it == open.end()) return;
            MidiNote n;
            n.start = it->second.start;
            n.length = std::max<Ticks>(1, toTicks(at) - n.start);
            n.note = static_cast<std::uint8_t>(note);
            n.velocity = it->second.velocity;
            byChannel[ch].push_back(n);
            open.erase(it);
        };
        while (!r.eof()) {
            tick += r.varLen();
            const std::uint8_t first = r.u8();
            if (first == 0xff) {
                const std::uint8_t type = r.u8();
                const std::uint32_t n = r.varLen();
                if (r.pos + n > r.end) throw std::runtime_error("a MIDI meta event is cut short");
                const std::size_t at = r.pos;
                r.pos += n;
                if (type == 0x2f) break;
                if (type == 0x03 && trackName.empty()) trackName.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin() + static_cast<std::ptrdiff_t>(at + n));
                if (type == 0x51 && n == 3 && out.bpm == 0) {
                    const double us = static_cast<double>((bytes[at] << 16) | (bytes[at + 1] << 8) | bytes[at + 2]);
                    if (us > 0) out.bpm = 60000000.0 / us;
                }
                if (type == 0x58 && n >= 2 && out.numerator == 0 && bytes[at + 1] < 8) {
                    out.numerator = bytes[at];
                    out.denominator = 1 << bytes[at + 1];
                }
                continue;
            }
            if (first == 0xf0 || first == 0xf7) {
                r.pos += std::min<std::size_t>(r.varLen(), r.end - r.pos);
                continue;
            }
            std::uint8_t status;
            if (first & 0x80) {
                status = running = first;
            } else {
                if (!running) throw std::runtime_error("a MIDI event has no status byte");
                status = running;
                --r.pos;  // the byte is the first data byte (running status)
            }
            const int kind = status >> 4, ch = status & 0x0f;
            const int data1 = r.u8();
            const int data2 = (kind == 0xc || kind == 0xd) ? 0 : r.u8();
            if (kind == 0x9 && data2 > 0) {
                close(ch, data1, tick);  // a repeated note-on ends the previous one
                open[{ch, data1}] = Open{toTicks(tick), static_cast<std::uint8_t>(data2)};
            } else if (kind == 0x8 || kind == 0x9) {
                close(ch, data1, tick);
            }
        }
        const auto stillOpen = open;
        for (const auto& entry : stillOpen) close(entry.first.first, entry.first.second, tick);  // notes left open end with the track
        for (auto& [ch, notes] : byChannel) {
            std::stable_sort(notes.begin(), notes.end(), [](const MidiNote& a, const MidiNote& b) { return a.start < b.start; });
            MidiFileTrack t;
            t.name = trackName;
            t.channel = ch;
            t.notes = std::move(notes);
            out.tracks.push_back(std::move(t));
        }
    }
    return out;
}

std::vector<std::uint8_t> writeMidiFile(const MidiFileData& data) {
    std::vector<std::uint8_t> out;
    std::vector<std::uint8_t> head;
    putBe(head, 1, 2);
    putBe(head, static_cast<std::uint32_t>(data.tracks.size() + 1), 2);
    putBe(head, static_cast<std::uint32_t>(kPPQ), 2);
    putChunk(out, "MThd", head);

    std::vector<std::uint8_t> conductor;
    const double bpm = data.bpm > 0 ? data.bpm : 120.0;
    const auto us = static_cast<std::uint32_t>(std::llround(60000000.0 / bpm));
    putMeta(conductor, 0, 0x51, {static_cast<std::uint8_t>(us >> 16), static_cast<std::uint8_t>(us >> 8), static_cast<std::uint8_t>(us)});
    const int num = data.numerator > 0 ? data.numerator : 4;
    int den = data.denominator > 0 ? data.denominator : 4, power = 0;
    while (den > 1 && power < 7) {
        den >>= 1;
        ++power;
    }
    putMeta(conductor, 0, 0x58, {static_cast<std::uint8_t>(num), static_cast<std::uint8_t>(power), 24, 8});
    putMeta(conductor, 0, 0x2f, {});
    putChunk(out, "MTrk", conductor);

    for (const MidiFileTrack& track : data.tracks) {
        struct Ev {
            Ticks at;
            int order;
            std::uint8_t note, velocity;
            bool on;
        };
        std::vector<Ev> events;
        for (const MidiNote& n : track.notes) {
            events.push_back({n.start, 1, n.note, n.velocity, true});
            events.push_back({n.start + std::max<Ticks>(1, n.length), 0, n.note, 0, false});  // an off sorts before an on at the same tick
        }
        std::stable_sort(events.begin(), events.end(), [](const Ev& a, const Ev& b) { return a.at != b.at ? a.at < b.at : a.order < b.order; });
        std::vector<std::uint8_t> body;
        putMeta(body, 0, 0x03, std::vector<std::uint8_t>(track.name.begin(), track.name.end()));
        Ticks last = 0;
        const auto ch = static_cast<std::uint8_t>(track.channel & 0x0f);
        for (const Ev& e : events) {
            putVar(body, static_cast<std::uint32_t>(e.at - last));
            last = e.at;
            body.push_back(static_cast<std::uint8_t>((e.on ? 0x90 : 0x80) | ch));
            body.push_back(e.note & 0x7f);
            body.push_back(e.on ? static_cast<std::uint8_t>(std::max<int>(1, e.velocity) & 0x7f) : 0);
        }
        putMeta(body, 0, 0x2f, {});
        putChunk(out, "MTrk", body);
    }
    return out;
}

}  // namespace lpc
