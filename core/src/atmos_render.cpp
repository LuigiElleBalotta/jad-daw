#include "lpc/atmos_render.h"

#include <algorithm>
#include <cmath>

namespace lpc {

namespace {

struct Pos {
    double x, y, z;
};
// The 7.1.4 room: container order, the LFE has no place
constexpr Pos kRoom[kSpeakerSlots] = {{-1, 1, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 0}, {-1, 0, 0}, {1, 0, 0}, {-1, -1, 0}, {1, -1, 0}, {-1, 0.5, 1}, {1, 0.5, 1}, {-1, -0.5, 1}, {1, -0.5, 1}};
constexpr float kSqrtHalf = 0.70710678f;

float lin(double db) { return static_cast<float>(std::pow(10.0, db / 20.0)); }

// Distance-based amplitude panning over the speakers in `place` (slot, position): the gain of each is 1 / distance^3 (the blur keeps a source on a speaker finite),
// scaled so that the powers add up to one.
SpeakerGains dbap(const std::vector<std::pair<int, Pos>>& place, double x, double y, double z, double size) {
    SpeakerGains g{};
    const double blur = 0.15 + 0.85 * std::clamp(size, 0.0, 1.0);
    double sum = 0.0;
    std::array<double, kSpeakerSlots> w{};
    for (const auto& [slot, p] : place) {
        const double d2 = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) + (p.z - z) * (p.z - z) + blur * blur;
        w[static_cast<std::size_t>(slot)] = 1.0 / std::pow(d2, 1.5);  // distance^-3
        sum += w[static_cast<std::size_t>(slot)] * w[static_cast<std::size_t>(slot)];
    }
    const double norm = sum > 0 ? 1.0 / std::sqrt(sum) : 0.0;
    for (const auto& [slot, p] : place) {
        (void)p;
        g[static_cast<std::size_t>(slot)] = static_cast<float>(w[static_cast<std::size_t>(slot)] * norm);
    }
    return g;
}

std::vector<std::pair<int, Pos>> room714() {
    std::vector<std::pair<int, Pos>> v;
    for (int i = 0; i < kSpeakerSlots; ++i)
        if (i != 3) v.push_back({i, kRoom[i]});
    return v;
}

// The trims and the balances on the 7.1.4 gains, before the layout is folded
void applyTrim(SpeakerGains& g, const AtmosTrim& t) {
    const float surround = lin(t.surroundDb), height = lin(t.heightDb);
    for (int i : {4, 5, 6, 7}) g[static_cast<std::size_t>(i)] *= surround;
    for (int i : {8, 9, 10, 11}) g[static_cast<std::size_t>(i)] *= height;
    const float oFront = lin(6.0 * t.overhead), oBack = lin(-6.0 * t.overhead);
    for (int i : {8, 9}) g[static_cast<std::size_t>(i)] *= oFront;
    for (int i : {10, 11}) g[static_cast<std::size_t>(i)] *= oBack;
    const float pFront = lin(6.0 * t.plane), pBack = lin(-6.0 * t.plane);
    for (int i : {0, 1, 2}) g[static_cast<std::size_t>(i)] *= pFront;
    for (int i : {6, 7}) g[static_cast<std::size_t>(i)] *= pBack;
}

// the height speakers fold into the ear-level ones
void foldHeightsToEarLevel(SpeakerGains& g) {
    g[0] += kSqrtHalf * g[8];
    g[4] += kSqrtHalf * g[8];
    g[1] += kSqrtHalf * g[9];
    g[5] += kSqrtHalf * g[9];
    g[4] += kSqrtHalf * g[10];
    g[6] += kSqrtHalf * g[10];
    g[5] += kSqrtHalf * g[11];
    g[7] += kSqrtHalf * g[11];
    for (int i : {8, 9, 10, 11}) g[static_cast<std::size_t>(i)] = 0;
}

// 7.1.4 (or 7.1) to the 5.1 surrounds
void foldSurrounds(SpeakerGains& g, int algorithm) {
    const float lss = g[4], rss = g[5], lrs = g[6], rrs = g[7];
    if (algorithm == 1) {  // Dolby Pro Logic IIx
        g[4] = lss + lin(-1.2) * lrs + lin(-6.2) * rrs;
        g[5] = rss + lin(-6.2) * lrs + lin(-1.2) * rrs;
    } else {  // Lo/Ro
        g[4] = lss + lrs;
        g[5] = rss + rrs;
    }
    g[6] = g[7] = 0;
}

void foldHeightsToTopMiddle(SpeakerGains& g) {
    const float ltm = kSqrtHalf * (g[8] + g[10]), rtm = kSqrtHalf * (g[9] + g[11]);
    g[8] = ltm;
    g[9] = rtm;
    g[10] = g[11] = 0;
}

// the 5.1.x layouts rendered directly: the surrounds sit at the back of the room (with balance, nearer its middle)
SpeakerGains direct51(MonitorFormat f, double x, double y, double z, double size, bool balance) {
    const double ys = balance ? -0.2 : -0.5;
    std::vector<std::pair<int, Pos>> place = {{0, kRoom[0]}, {1, kRoom[1]}, {2, kRoom[2]}, {4, {-1, ys, 0}}, {5, {1, ys, 0}}};
    if (f == MonitorFormat::S512) {
        place.push_back({8, {-1, 0, 1}});
        place.push_back({9, {1, 0, 1}});
    } else if (f == MonitorFormat::S514) {
        for (int i : {8, 9, 10, 11}) place.push_back({i, kRoom[i]});
    }
    return dbap(place, x, y, z, size);
}

SpeakerGains five1Of(MonitorFormat f, double x, double y, double z, double size, const AtmosDownmix& dm) {
    if (dm.to51 >= 2) {
        SpeakerGains g = direct51(f == MonitorFormat::From51To20 ? MonitorFormat::S51 : f, x, y, z, size, dm.to51 == 2);
        AtmosTrim t = dm.trim;  // the trims apply to the surround and height channels of the render
        const float surround = lin(t.surroundDb), height = lin(t.heightDb);
        for (int i : {4, 5}) g[static_cast<std::size_t>(i)] *= surround;
        for (int i : {8, 9, 10, 11}) g[static_cast<std::size_t>(i)] *= height;
        for (int i : {8, 9}) g[static_cast<std::size_t>(i)] *= lin(6.0 * t.overhead);
        for (int i : {10, 11}) g[static_cast<std::size_t>(i)] *= lin(-6.0 * t.overhead);
        for (int i : {0, 1, 2}) g[static_cast<std::size_t>(i)] *= lin(6.0 * t.plane);
        for (int i : {4, 5}) g[static_cast<std::size_t>(i)] *= lin(-6.0 * t.plane);
        return g;
    }
    SpeakerGains g = dbap(room714(), x, y, z, size);
    applyTrim(g, dm.trim);
    if (f == MonitorFormat::S51 || f == MonitorFormat::From51To20) foldHeightsToEarLevel(g);
    else if (f == MonitorFormat::S512) foldHeightsToTopMiddle(g);
    foldSurrounds(g, dm.to51);
    return g;
}

}  // namespace

int monitorChannels(MonitorFormat f) {
    switch (f) {
        case MonitorFormat::S51: return 6;
        case MonitorFormat::S71:
        case MonitorFormat::S512: return 8;
        case MonitorFormat::S514: return 10;
        case MonitorFormat::S714: return 12;
        default: return 2;
    }
}

const char* monitorName(MonitorFormat f) {
    static const char* names[kMonitorFormats] = {"Stereo pan", "Binaural", "2.0 Direct", "2.0 from 5.1 Downmix", "5.1", "7.1", "5.1.2", "5.1.4", "7.1.4"};
    return names[static_cast<int>(f)];
}

const std::vector<std::string>& monitorChannelNames(MonitorFormat f) {
    static const std::vector<std::string> stereo = {"L", "R"}, s51 = {"L", "R", "C", "LFE", "Ls", "Rs"}, s71 = {"L", "R", "C", "LFE", "Lss", "Rss", "Lrs", "Rrs"},
                                          s512 = {"L", "R", "C", "LFE", "Ls", "Rs", "Ltm", "Rtm"}, s514 = {"L", "R", "C", "LFE", "Ls", "Rs", "Ltf", "Rtf", "Ltr", "Rtr"},
                                          s714 = {"L", "R", "C", "LFE", "Lss", "Rss", "Lrs", "Rrs", "Ltf", "Rtf", "Ltr", "Rtr"};
    switch (f) {
        case MonitorFormat::S51: return s51;
        case MonitorFormat::S71: return s71;
        case MonitorFormat::S512: return s512;
        case MonitorFormat::S514: return s514;
        case MonitorFormat::S714: return s714;
        default: return stereo;
    }
}

int trimLayoutOf(MonitorFormat f) {
    switch (f) {
        case MonitorFormat::Direct20: return 0;
        case MonitorFormat::From51To20:
        case MonitorFormat::S51: return 1;
        case MonitorFormat::S512: return 2;
        case MonitorFormat::S514: return 3;
        case MonitorFormat::S71: return 4;
        default: return -1;
    }
}

// Automatic: what a smaller layout does to the room information that has no speaker of its own (ours; Dolby's values are not published)
AtmosTrim automaticTrim(int layout) {
    AtmosTrim t;
    switch (layout) {
        case 0: t.surroundDb = -3.0; t.heightDb = -3.0; break;  // 2.0 Direct
        case 1: t.surroundDb = 0.0; t.heightDb = -3.0; break;   // 5.1 & 2.0
        case 2: t.surroundDb = 0.0; t.heightDb = 0.0; break;    // 5.1.2
        case 3: t.surroundDb = 0.0; t.heightDb = 0.0; break;    // 5.1.4
        case 4: t.surroundDb = 0.0; t.heightDb = -3.0; break;   // 7.1
        default: break;
    }
    return t;
}

SpeakerGains objectGains(MonitorFormat f, double x, double y, double z, double size, const AtmosDownmix& dm) {
    x = std::clamp(x, -1.0, 1.0);
    y = std::clamp(y, -1.0, 1.0);
    z = std::clamp(z, 0.0, 1.0);
    switch (f) {
        case MonitorFormat::S714: return dbap(room714(), x, y, z, size);
        case MonitorFormat::S71: {
            SpeakerGains g = dbap(room714(), x, y, z, size);
            applyTrim(g, dm.trim);
            foldHeightsToEarLevel(g);
            return g;
        }
        case MonitorFormat::S51:
        case MonitorFormat::S512:
        case MonitorFormat::S514: return five1Of(f, x, y, z, size, dm);
        default: return SpeakerGains{};
    }
}

StereoGains stereoGains(MonitorFormat f, double x, double y, double z, double size, const AtmosDownmix& dm) {
    x = std::clamp(x, -1.0, 1.0);
    y = std::clamp(y, -1.0, 1.0);
    z = std::clamp(z, 0.0, 1.0);
    StereoGains out;
    if (f == MonitorFormat::From51To20) {
        const SpeakerGains g = five1Of(MonitorFormat::S51, x, y, z, size, dm);
        const float L = g[0], R = g[1], C = g[2], Ls = g[4], Rs = g[5];
        if (dm.to20 == 0) {  // Lo/Ro
            out.l = L + lin(-3.0) * C + lin(-3.0) * Ls;
            out.r = R + lin(-3.0) * C + lin(-3.0) * Rs;
        } else {  // Lt/Rt: the surrounds go in with opposite signs, with phase 90 through the shifted copy
            const float a = lin(-1.2), b = lin(-6.2);
            const float ltS = -(a * Ls + b * Rs), rtS = b * Ls + a * Rs;
            out.l = L + lin(-3.0) * C;
            out.r = R + lin(-3.0) * C;
            if (dm.to20 == 2) {
                out.lPhase = ltS;
                out.rPhase = rtS;
            } else {
                out.l += ltS;
                out.r += rtS;
            }
        }
        return out;
    }
    // 2.0 Direct: the 7.1.4 render folded to two channels, the centre into both
    SpeakerGains g = dbap(room714(), x, y, z, size);
    applyTrim(g, dm.trim);
    out.l = g[0] + kSqrtHalf * g[2] + g[4] + g[6] + g[8] + g[10];
    out.r = g[1] + kSqrtHalf * g[2] + g[5] + g[7] + g[9] + g[11];
    // keep the power of a source that is folded from a place with many speakers near it: the sum of its gains is at most the sum of the squares' root
    const float power = std::sqrt(out.l * out.l + out.r * out.r);
    if (power > 1.0f) {
        out.l /= power;
        out.r /= power;
    }
    return out;
}

std::vector<float> renderSpeakers(const AdmFile& adm, MonitorFormat f, const AtmosDownmix& dm) {
    const int channels = monitorChannels(f);
    const std::int64_t frames = adm.frames();
    std::vector<float> out(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels), 0.0f);
    // the container slot to the channel of the format (found once: the names are compared here, not for every sample)
    int slotAt[kSpeakerSlots];
    {
        const std::vector<std::string>& names = monitorChannelNames(f);
        static const char* slotNames[kSpeakerSlots] = {"L", "R", "C", "LFE", "Ls", "Rs", "Lrs", "Rrs", "Ltf", "Rtf", "Ltr", "Rtr"};
        for (int slot = 0; slot < kSpeakerSlots; ++slot) {
            slotAt[slot] = -1;
            for (std::size_t k = 0; k < names.size(); ++k) {
                const std::string& n = names[k];
                if (n == slotNames[slot] || (slot == 4 && n == "Lss") || (slot == 5 && n == "Rss") || (slot == 8 && n == "Ltm") || (slot == 9 && n == "Rtm")) slotAt[slot] = static_cast<int>(k);
            }
        }
    }
    const auto add = [&](int slot, std::int64_t i, float v) {
        const int at = slotAt[slot];
        if (at >= 0) out[static_cast<std::size_t>(i) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(at)] += v;
    };
    // the bed: its ten channels (L R C LFE Lss Rss Lrs Rrs Lts Rts) go to the channels of the format with the same names; the ones the format lacks are folded into the front pair
    for (std::size_t c = 0; c < adm.bed.size() && c < 10; ++c) {
        static const int slotOf[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        for (std::int64_t i = 0; i < static_cast<std::int64_t>(adm.bed[c].size()); ++i) {
            const float v = adm.bed[c][static_cast<std::size_t>(i)];
            if (v == 0.0f) continue;
            if (channels == 2) {
                if (c == 0) add(0, i, v);
                else if (c == 1) add(1, i, v);
                else if (c == 2) { add(0, i, kSqrtHalf * v); add(1, i, kSqrtHalf * v); }
                else if (c == 4 || c == 6 || c == 8) add(0, i, kSqrtHalf * v);
                else if (c == 5 || c == 7 || c == 9) add(1, i, kSqrtHalf * v);
            } else {
                add(slotOf[c], i, v);
            }
        }
    }
    constexpr int hop = 16;
    for (const AdmObject& o : adm.objects) {
        std::size_t seg = 0;
        const auto positionAt = [&](double t) {
            if (o.positions.empty()) return AdmPosition{};
            while (seg + 1 < o.positions.size() && o.positions[seg + 1].time <= t) ++seg;
            if (seg + 1 >= o.positions.size() || t <= o.positions[seg].time) return o.positions[seg];
            const AdmPosition& a = o.positions[seg];
            const AdmPosition& b = o.positions[seg + 1];
            const double u = (t - a.time) / std::max(1e-9, b.time - a.time);  // the movement is interpolated between the written positions
            AdmPosition p = a;
            p.x = a.x + (b.x - a.x) * u;
            p.y = a.y + (b.y - a.y) * u;
            p.z = a.z + (b.z - a.z) * u;
            p.size = a.size + (b.size - a.size) * u;
            return p;
        };
        SpeakerGains prev{}, cur{};
        bool first = true;
        for (std::int64_t base = 0; base < static_cast<std::int64_t>(o.samples.size()); base += hop) {
            const AdmPosition p = positionAt(static_cast<double>(base) / adm.sampleRate);
            if (channels == 2) {
                const StereoGains sg = stereoGains(f, p.x, p.y, p.z, p.size, dm);
                cur = SpeakerGains{};
                cur[0] = sg.l;
                cur[1] = sg.r;
            } else {
                cur = objectGains(f, p.x, p.y, p.z, p.size, dm);
            }
            if (first) {
                prev = cur;
                first = false;
            }
            const std::int64_t n = std::min<std::int64_t>(hop, static_cast<std::int64_t>(o.samples.size()) - base);
            for (std::int64_t i = 0; i < n; ++i) {
                const float u = static_cast<float>(i + 1) / static_cast<float>(n);
                const float v = o.samples[static_cast<std::size_t>(base + i)];
                for (int s = 0; s < kSpeakerSlots; ++s) {
                    const float g = prev[static_cast<std::size_t>(s)] + (cur[static_cast<std::size_t>(s)] - prev[static_cast<std::size_t>(s)]) * u;
                    if (g != 0.0f) add(s, base + i, v * g);
                }
            }
            prev = cur;
        }
    }
    return out;
}

}  // namespace lpc
