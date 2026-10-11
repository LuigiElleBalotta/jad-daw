#include "lpc/session_player.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <sstream>

namespace lpc {

namespace {

struct Quality {
    const char* name;
    std::vector<int> tones;
};

const std::vector<Quality>& qualities() {
    static const std::vector<Quality> q = {
        {"", {4, 7}},        {"maj", {4, 7}},     {"m", {3, 7}},        {"min", {3, 7}},     {"dim", {3, 6}},      {"aug", {4, 8}},
        {"sus2", {2, 7}},    {"sus4", {5, 7}},    {"sus", {5, 7}},      {"7", {4, 7, 10}},   {"maj7", {4, 7, 11}},  {"M7", {4, 7, 11}},
        {"m7", {3, 7, 10}},  {"min7", {3, 7, 10}}, {"m7b5", {3, 6, 10}}, {"dim7", {3, 6, 9}}, {"6", {4, 7, 9}},     {"m6", {3, 7, 9}},
        {"9", {4, 7, 10, 14}}, {"m9", {3, 7, 10, 14}}, {"add9", {4, 7, 14}}, {"maj9", {4, 7, 11, 14}}, {"mmaj7", {3, 7, 11}}, {"7sus4", {5, 7, 10}}};
    return q;
}

// a note name (letter and accidentals) at the start of `s`: the pitch class and how many characters it took; -1 when there is none
int pitchClass(const std::string& s, std::size_t& used) {
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    used = 0;
    if (s.empty()) return -1;
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    if (c < 'A' || c > 'G') return -1;
    int pc = base[c - 'A'];
    used = 1;
    if (used < s.size() && s[used] == 'b') { --pc; ++used; }                       // flat (a 'b' that follows a quality, as in m7b5, is not read here)
    else while (used < s.size() && s[used] == '#') { ++pc; ++used; }             // sharps
    return ((pc % 12) + 12) % 12;
}

bool parseOne(const std::string& text, SessionChord& out, std::string& error) {
    std::size_t used = 0;
    const int root = pitchClass(text, used);
    if (root < 0) {
        error = "Unknown chord \"" + text + "\"";
        return false;
    }
    std::string rest = text.substr(used);
    int bass = -1;
    if (const std::size_t slash = rest.find('/'); slash != std::string::npos) {
        std::size_t u2 = 0;
        bass = pitchClass(rest.substr(slash + 1), u2);
        if (bass < 0 || slash + 1 + u2 != rest.size()) {
            error = "Unknown bass note in \"" + text + "\"";
            return false;
        }
        rest = rest.substr(0, slash);
    }
    for (const Quality& q : qualities())
        if (rest == q.name) {
            out.root = root;
            out.tones = q.tones;
            out.bass = bass;
            out.name = text;
            return true;
        }
    error = "Unknown chord \"" + text + "\"";
    return false;
}

struct Rng {
    unsigned s;
    explicit Rng(unsigned seed) : s(seed * 2654435761u + 12345u) {}
    int jitter(int range) {  // -range..range
        s = s * 1664525u + 1013904223u;
        return static_cast<int>((s >> 16) % static_cast<unsigned>(2 * range + 1)) - range;
    }
};

struct Builder {
    std::vector<MidiNote> notes;
    double swing = 0.0;
    Rng rng;
    explicit Builder(unsigned seed) : rng(seed) {}
    void add(double beat, double length, int note, int velocity, bool humanize = true) {
        if (note < 0 || note > 127) return;
        const double frac = beat - std::floor(beat);
        if (std::abs(frac - 0.5) < 1e-6) beat += swing * (1.0 / 6.0);   // the off-beat eighth late: a triplet feel at 1
        if (std::abs(frac - 0.75) < 1e-6) beat += swing * (1.0 / 12.0); // and the sixteenth after it
        if (humanize) velocity += rng.jitter(6);
        MidiNote n;
        n.start = static_cast<Ticks>(std::llround(beat * kPPQ));
        n.length = std::max<Ticks>(1, static_cast<Ticks>(std::llround(length * kPPQ)));
        n.note = static_cast<std::uint8_t>(note);
        n.velocity = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
        notes.push_back(n);
    }
};

// ---------------------------------------------------------------- Drummer

constexpr int kKick = 36, kSnare = 38, kClap = 39, kHatClosed = 42, kHatOpen = 46, kCrash = 49, kRide = 51, kTomHigh = 50, kTomMid = 47, kTomLow = 43;

void drumBar(Builder& b, double t0, int bar, int bpb, const SessionParams& p) {
    const double in = p.intensity;
    const int base = static_cast<int>(50 + 55 * in);
    const bool fill = p.fills && in > 0.25 && bar % 4 == 3 && bpb >= 3;
    const int fillBeat = bpb - 1;  // the last beat of the bar holds the fill
    auto beatIsFill = [&](double beat) { return fill && beat >= fillBeat; };
    auto at = [&](double beat, int note, int vel, double len = 0.12) {
        if (!beatIsFill(beat)) b.add(t0 + beat, len, note, vel);
    };
    const std::string& s = p.style;
    if (s == "funk") {
        for (int i = 0; i < bpb * 4; ++i) at(i * 0.25, kHatClosed, base - 25 + (i % 4 == 0 ? 18 : (i % 2 == 0 ? 8 : 0)), 0.1);
        at(0.0, kKick, base + 10);
        at(1.5, kKick, base);
        if (bpb >= 4) { at(2.75, kKick, base - 5); at(3.5, kKick, base - 10); }
        at(1.0, kSnare, base + 15);
        if (bpb >= 4) at(3.0, kSnare, base + 15);
        if (in > 0.35) for (double g : {0.75, 1.75, 2.5, 3.25}) if (g < bpb) at(g, kSnare, 30 + static_cast<int>(20 * in));  // ghost notes
    } else if (s == "ballad") {
        for (int i = 0; i < bpb; ++i) at(i, kHatClosed, base - 20 + (i == 0 ? 10 : 0), 0.3);
        at(0.0, kKick, base);
        if (bpb >= 4) { at(2.5, kKick, base - 8); at(2.0, kSnare, base + 8); } else at(bpb - 1, kSnare, base + 5);
    } else if (s == "shuffle") {
        for (int i = 0; i < bpb; ++i) { at(i, kRide, base - 10 + (i % 2 == 0 ? 10 : 0), 0.3); at(i + 0.5, kRide, base - 22, 0.2); }
        at(0.0, kKick, base);
        if (bpb >= 4) { at(2.0, kKick, base - 5); at(1.0, kSnare, base + 10); at(3.0, kSnare, base + 10); } else at(bpb - 1, kSnare, base + 10);
    } else if (s == "pop") {
        for (int i = 0; i < bpb * 2; ++i) at(i * 0.5, kHatClosed, base - 25 + (i % 2 == 0 ? 12 : 0), 0.1);
        at(0.0, kKick, base + 8);
        if (bpb >= 4) { at(2.0, kKick, base); if (in > 0.5) at(2.5, kKick, base - 12); at(1.0, kSnare, base + 12); at(3.0, kSnare, base + 12); if (in > 0.7) at(3.0, kClap, base - 8); }
        else at(bpb - 1, kSnare, base + 12);
    } else {  // rock
        for (int i = 0; i < bpb * 2; ++i) at(i * 0.5, i % 2 == 0 ? kHatClosed : kHatClosed, base - 22 + (i % 2 == 0 ? 14 : 0), 0.1);
        at(0.0, kKick, base + 12);
        if (bpb >= 4) { at(2.0, kKick, base + 5); if (in > 0.6) at(2.5, kKick, base - 5); at(1.0, kSnare, base + 15); at(3.0, kSnare, base + 15); }
        else at(bpb - 1, kSnare, base + 15);
    }
    if (bar % 4 == 0 && in > 0.4) b.add(t0, 1.0, kCrash, base + 5);
    if (fill) {  // sixteenths round the toms, ending on the next downbeat
        const int toms[4] = {kSnare, kTomHigh, kTomMid, kTomLow};
        for (int i = 0; i < 4; ++i) b.add(t0 + fillBeat + 0.25 * i, 0.2, toms[i], base - 5 + 4 * i);
    }
}

// ---------------------------------------------------------------- Bass

int bassPitch(int pc) {  // E1 .. Eb2 and up one octave: 28..39 or so
    int n = 24 + pc;
    if (n < 28) n += 12;
    return n;
}

void bassChord(Builder& b, double t0, const SessionChord& c, const SessionChord* next, const SessionParams& p) {
    if (c.root < 0) return;
    const int vel = static_cast<int>(60 + 45 * p.intensity);
    const int root = bassPitch(c.bass >= 0 ? c.bass : c.root);
    const double d = c.beats;
    auto tone = [&](std::size_t i) {
        int n = bassPitch(c.root) + (c.tones.empty() ? 0 : c.tones[std::min(i, c.tones.size() - 1)] % 12);
        return n > 47 ? n - 12 : n;
    };
    const std::string& s = p.style;
    if (s == "eighths") {
        for (double t = 0; t < d - 1e-6; t += 0.5) b.add(t0 + t, 0.45, (p.intensity > 0.7 && std::fmod(t, 1.0) > 0.4 && std::fmod(t, 2.0) > 1.4) ? root + 12 : root, vel + (std::fmod(t, 1.0) < 0.1 ? 8 : -6));
    } else if (s == "walking") {
        for (int i = 0; i < static_cast<int>(d); ++i) {
            int pitch = root;
            if (i == 1) pitch = tone(0);
            else if (i == 2) pitch = tone(c.tones.size() > 1 ? 1 : 0);
            else if (i >= 3) pitch = next && next->root >= 0 ? bassPitch(next->bass >= 0 ? next->bass : next->root) + (i % 2 ? -1 : 1) : tone(0);
            b.add(t0 + i, 0.9, pitch, vel + (i == 0 ? 8 : 0));
        }
    } else if (s == "funk") {
        const double pos[] = {0.0, 0.75, 1.5, 2.25, 3.0, 3.5};
        const int offs[] = {0, 0, 12, 0, 0, 7};
        for (std::size_t i = 0; i < 6; ++i)
            if (pos[i] < d) b.add(t0 + pos[i], i == 5 ? 0.3 : 0.55, root + offs[i], vel - (i == 4 ? 14 : 0));
    } else {  // root: on the first beat and every second beat after it
        for (double t = 0; t < d - 1e-6; t += 2.0) b.add(t0 + t, std::min(1.9, d - t) * 0.95, root, vel + (t == 0 ? 6 : 0));
    }
}

// ---------------------------------------------------------------- Keys

std::vector<int> voicing(const SessionChord& c, const std::vector<int>& previous) {
    std::vector<int> pcs{c.root};
    for (int t : c.tones) pcs.push_back(c.root + t);
    std::vector<int> best;
    long bestCost = -1;
    for (std::size_t inv = 0; inv < pcs.size(); ++inv) {
        std::vector<int> v;
        int last = 52;  // stacked upwards from here
        for (std::size_t k = 0; k < pcs.size(); ++k) {
            int pc = pcs[(inv + k) % pcs.size()] % 12;
            int n = 48 + pc;
            while (n < last) n += 12;
            v.push_back(n);
            last = n + 1;
        }
        double sum = 0;
        for (int n : v) sum += n;
        const double avg = sum / static_cast<double>(v.size());
        long cost = 0;
        if (previous.empty() || previous.size() != v.size()) cost = static_cast<long>(std::abs(avg - 64.0) * 10);
        else for (std::size_t k = 0; k < v.size(); ++k) cost += std::abs(v[k] - previous[k]) * 10;
        if (bestCost < 0 || cost < bestCost) { bestCost = cost; best = v; }
    }
    return best;
}

void keysChord(Builder& b, double t0, const SessionChord& c, std::vector<int>& previous, const SessionParams& p) {
    if (c.root < 0) return;
    const std::vector<int> v = voicing(c, previous);
    previous = v;
    const int vel = static_cast<int>(48 + 40 * p.intensity);
    const double d = c.beats;
    const std::string& s = p.style;
    if (s == "arpeggio") {
        std::vector<int> seq = v;
        seq.push_back(v[0] + 12);
        for (std::size_t k = seq.size() - 2; k >= 1; --k) { seq.push_back(seq[k]); if (k == 1) break; }
        std::size_t i = 0;
        for (double t = 0; t < d - 1e-6; t += 0.5, ++i) b.add(t0 + t, 0.48, seq[i % seq.size()], vel + (std::fmod(t, 1.0) < 0.1 ? 8 : -4));
    } else if (s == "comp") {
        const double hits[] = {0.0, 1.5, 3.5, 2.5};
        const int count = p.intensity > 0.7 ? 4 : (p.intensity > 0.35 ? 3 : 2);
        for (int h = 0; h < count; ++h)
            if (hits[h] < d) for (int n : v) b.add(t0 + hits[h], h == 0 ? 1.2 : 0.35, n, vel - (h == 0 ? 0 : 6));
    } else if (s == "pad") {
        for (int n : v) b.add(t0, d + 0.1, n, vel - 10);
    } else {  // block
        for (int n : v) b.add(t0, d * 0.95, n, vel);
    }
}

}  // namespace

std::vector<SessionChord> parseChords(const std::string& text, int beatsPerBar, std::string& error) {
    error.clear();
    std::vector<SessionChord> out;
    std::istringstream in(text);
    std::string token;
    std::vector<SessionChord> last;  // the chords of the previous bar, for %
    while (in >> token) {
        std::vector<SessionChord> bar;
        if (token == "%") {
            if (last.empty()) { error = "% needs a chord before it"; return {}; }
            bar = last;
        } else if (token == "-") {
            SessionChord rest;
            rest.root = -1;
            rest.name = "-";
            rest.beats = beatsPerBar;
            bar.push_back(rest);
        } else {
            std::vector<std::string> parts;
            std::string part;
            std::istringstream ps(token);
            while (std::getline(ps, part, '.')) if (!part.empty()) parts.push_back(part);
            if (parts.empty()) { error = "Unknown chord \"" + token + "\""; return {}; }
            for (const std::string& pt : parts) {
                SessionChord c;
                if (!parseOne(pt, c, error)) return {};
                c.beats = static_cast<double>(beatsPerBar) / static_cast<double>(parts.size());
                bar.push_back(c);
            }
        }
        last = bar;
        out.insert(out.end(), bar.begin(), bar.end());
        if (out.size() > 4096) { error = "That is too many bars"; return {}; }
    }
    if (out.empty()) error = "Write a chord progression, for example: Am F C G";
    return out;
}

const std::vector<std::string>& sessionStyles(SessionKind kind) {
    static const std::vector<std::string> drums = {"rock", "pop", "funk", "ballad", "shuffle"}, bass = {"root", "eighths", "walking", "funk"}, keys = {"block", "arpeggio", "comp", "pad"};
    return kind == SessionKind::Drummer ? drums : (kind == SessionKind::Bass ? bass : keys);
}

std::vector<MidiNote> generateSession(const std::vector<SessionChord>& chords, const SessionParams& params) {
    Builder b(params.seed);
    b.swing = std::clamp(params.swing, 0.0, 1.0);
    const int bpb = std::max(1, params.beatsPerBar);
    double t = 0.0;
    std::vector<int> previous;
    int bar = 0;
    for (std::size_t i = 0; i < chords.size(); ++i) {
        const SessionChord& c = chords[i];
        if (params.kind == SessionKind::Drummer) {
            // drums play whole bars: start the pattern at each bar, whatever the chords do in it
            const double inBar = std::fmod(t, static_cast<double>(bpb));
            if (inBar < 1e-6) drumBar(b, t, bar++, bpb, params);
        } else if (params.kind == SessionKind::Bass) {
            bassChord(b, t, c, i + 1 < chords.size() ? &chords[i + 1] : nullptr, params);
        } else {
            keysChord(b, t, c, previous, params);
        }
        t += c.beats;
    }
    std::sort(b.notes.begin(), b.notes.end(), [](const MidiNote& x, const MidiNote& y) { return x.start < y.start || (x.start == y.start && x.note < y.note); });
    return std::move(b.notes);
}

}  // namespace lpc
