#include "lpc/audio_analysis.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

#include "lpc/fft.h"
#include "lpc/tempo_analysis.h"

namespace lpc {

namespace {

constexpr double kPi = 3.14159265358979323846;

double toDb(double power) { return power > 1e-20 ? 10.0 * std::log10(power) : -200.0; }

std::vector<double> bandCentresList() {
    std::vector<double> c;
    for (int i = 0; i < kThirdOctaveBands; ++i) c.push_back(1000.0 * std::pow(2.0, (i - 20) / 3.0));  // 20 Hz .. 20 kHz
    return c;
}

}  // namespace

AudioReport analyseAudio(const WavData& audio, const AnalysisOptions& options) {
    AudioReport r;
    r.sampleRate = audio.sampleRate;
    r.channels = audio.channels;
    r.bandCentres = bandCentresList();
    r.bandDb.assign(kThirdOctaveBands, -200.0);
    const std::int64_t frames = audio.frames();
    if (frames <= 0 || audio.channels < 1 || audio.sampleRate <= 0) return r;
    r.seconds = static_cast<double>(frames) / audio.sampleRate;
    const int ch = audio.channels;
    auto at = [&](std::int64_t i, int c) { return static_cast<double>(audio.samples[static_cast<std::size_t>(i * ch + std::min(c, ch - 1))]); };

    // level, problems, stereo
    double peak = 0.0, sq = 0.0, sum = 0.0, ll = 0.0, rr = 0.0, lr = 0.0, mid = 0.0, side = 0.0;
    for (std::int64_t i = 0; i < frames; ++i) {
        const double l = at(i, 0), rgt = at(i, ch > 1 ? 1 : 0);
        for (int c = 0; c < ch; ++c) {
            const double v = at(i, c);
            peak = std::max(peak, std::abs(v));
            sq += v * v;
            sum += v;
            if (std::abs(v) >= 0.999) ++r.clippedSamples;
        }
        ll += l * l;
        rr += rgt * rgt;
        lr += l * rgt;
        const double m = 0.5 * (l + rgt), s = 0.5 * (l - rgt);
        mid += m * m;
        side += s * s;
    }
    const double n = static_cast<double>(frames) * ch;
    r.peakDb = peak > 0 ? 20.0 * std::log10(peak) : -200.0;
    r.rmsDb = toDb(sq / n);
    r.crestDb = r.peakDb - r.rmsDb;
    r.dcOffset = sum / n;
    r.clippedPercent = 100.0 * static_cast<double>(r.clippedSamples) / n;
    if (ch > 1) {
        r.correlation = (ll > 1e-20 && rr > 1e-20) ? lr / std::sqrt(ll * rr) : 1.0;
        r.widthPercent = (mid + side) > 1e-20 ? 100.0 * side / (mid + side) : 0.0;
        r.balanceDb = std::clamp(toDb(ll) - toDb(rr), -60.0, 60.0);  // one silent side reads as 60 dB
    }
    r.loudness = measureLoudness(audio);

    // silence in blocks of 100 ms
    const std::int64_t block = std::max<std::int64_t>(1, audio.sampleRate / 10);
    std::vector<bool> quiet;
    for (std::int64_t b = 0; b < frames; b += block) {
        double e = 0.0;
        const std::int64_t end = std::min(frames, b + block);
        for (std::int64_t i = b; i < end; ++i)
            for (int c = 0; c < ch; ++c) e += at(i, c) * at(i, c);
        quiet.push_back(toDb(e / static_cast<double>((end - b) * ch)) < -60.0);
    }
    std::size_t quietCount = 0;
    for (bool q : quiet) quietCount += q ? 1 : 0;
    r.silencePercent = 100.0 * static_cast<double>(quietCount) / static_cast<double>(quiet.size());
    std::size_t lead = 0, trail = 0;
    while (lead < quiet.size() && quiet[lead]) ++lead;
    while (trail < quiet.size() - lead && quiet[quiet.size() - 1 - trail]) ++trail;
    r.leadingSilenceSeconds = static_cast<double>(lead) * 0.1;
    r.trailingSilenceSeconds = static_cast<double>(trail) * 0.1;

    // the long-term spectrum (mid channel), averaged power
    constexpr int N = 8192;
    if (frames >= N) {
        const Fft fft(N);
        std::vector<double> window(N);
        double wsq = 0.0;
        for (int i = 0; i < N; ++i) {
            window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / N);
            wsq += window[static_cast<std::size_t>(i)] * window[static_cast<std::size_t>(i)];
        }
        std::vector<double> power(N / 2, 0.0);
        std::vector<std::complex<double>> buf(N);
        int count = 0;
        const std::int64_t step = std::max<std::int64_t>(N / 2, frames / 400);  // at most about 400 frames, whatever the length
        for (std::int64_t at0 = 0; at0 + N <= frames; at0 += step) {
            for (int i = 0; i < N; ++i) {
                double v = 0.0;
                for (int c = 0; c < ch; ++c) v += at(at0 + i, c);
                buf[static_cast<std::size_t>(i)] = (v / ch) * window[static_cast<std::size_t>(i)];
            }
            fft.forward(buf.data());
            for (int k = 1; k < N / 2; ++k) power[static_cast<std::size_t>(k)] += std::norm(buf[static_cast<std::size_t>(k)]);
            ++count;
        }
        if (count > 0) {
            const double scale = 2.0 / (static_cast<double>(N) * wsq) / count;  // mean square per bin
            std::vector<double> bandPower(kThirdOctaveBands, 0.0);
            double total = 0.0, centroid = 0.0, cumulative = 0.0;
            std::vector<double> binMs(N / 2, 0.0);
            for (int k = 1; k < N / 2; ++k) {
                const double f = static_cast<double>(k) * audio.sampleRate / N;
                const double ms = power[static_cast<std::size_t>(k)] * scale;
                binMs[static_cast<std::size_t>(k)] = ms;
                total += ms;
                centroid += ms * f;
                for (int b = 0; b < kThirdOctaveBands; ++b) {
                    const double lo = r.bandCentres[static_cast<std::size_t>(b)] / std::pow(2.0, 1.0 / 6.0), hi = r.bandCentres[static_cast<std::size_t>(b)] * std::pow(2.0, 1.0 / 6.0);
                    if (f >= lo && f < hi) { bandPower[static_cast<std::size_t>(b)] += ms; break; }
                }
            }
            for (int b = 0; b < kThirdOctaveBands; ++b) r.bandDb[static_cast<std::size_t>(b)] = toDb(bandPower[static_cast<std::size_t>(b)]);
            if (total > 1e-20) {
                r.centroidHz = centroid / total;
                for (int k = 1; k < N / 2; ++k) {
                    cumulative += binMs[static_cast<std::size_t>(k)];
                    if (cumulative >= 0.85 * total) { r.rolloffHz = static_cast<double>(k) * audio.sampleRate / N; break; }
                }
                double low = 0, lowMid = 0, midE = 0, highMid = 0, high = 0;
                for (int k = 1; k < N / 2; ++k) {
                    const double f = static_cast<double>(k) * audio.sampleRate / N, ms = binMs[static_cast<std::size_t>(k)];
                    (f < 120 ? low : f < 500 ? lowMid : f < 2000 ? midE : f < 6000 ? highMid : high) += ms;
                }
                r.lowPercent = 100.0 * low / total;
                r.lowMidPercent = 100.0 * lowMid / total;
                r.midPercent = 100.0 * midE / total;
                r.highMidPercent = 100.0 * highMid / total;
                r.highPercent = 100.0 * high / total;
            }
        }
    }

    if (options.tempo && r.seconds > 4.0) {
        const TempoAnalysis t = analyseTempo(audio);
        r.bpm = t.bpm;
        r.tempoConfidence = t.confidence;
    }
    if (options.timeline) {
        // LUFS-S (3 s windows) about once a second, from the K-weighted blocks (100 ms hops, 400 ms blocks: one value per ten blocks)
        const std::vector<double> blocks = kWeightedBlocks(audio.samples.data(), ch, frames, audio.sampleRate, 3.0);
        const std::size_t stride = std::max<std::size_t>(10, blocks.size() / 240 + 1);
        for (std::size_t i = 0; i < blocks.size(); i += stride) r.shortTermTimeline.push_back(blockLufs(blocks[i]));
    }
    return r;
}

nlohmann::json toJson(const AudioReport& r) {
    auto rd = [](double v, int digits = 2) { const double f = std::pow(10.0, digits); return std::round(v * f) / f; };
    nlohmann::json j;
    j["seconds"] = rd(r.seconds);
    j["sampleRate"] = r.sampleRate;
    j["channels"] = r.channels;
    j["level"] = {{"peakDb", rd(r.peakDb)}, {"rmsDb", rd(r.rmsDb)}, {"crestDb", rd(r.crestDb)}};
    j["loudness"] = {{"integratedLufs", rd(r.loudness.integrated)}, {"momentaryMaxLufs", rd(r.loudness.momentaryMax)}, {"shortTermMaxLufs", rd(r.loudness.shortTermMax)},
                     {"rangeLu", rd(r.loudness.range)}, {"truePeakDbtp", rd(r.loudness.truePeakDb)}};
    j["problems"] = {{"dcOffset", rd(r.dcOffset, 5)}, {"clippedSamples", r.clippedSamples}, {"clippedPercent", rd(r.clippedPercent, 4)}, {"silencePercent", rd(r.silencePercent, 1)},
                     {"leadingSilenceSeconds", rd(r.leadingSilenceSeconds, 1)}, {"trailingSilenceSeconds", rd(r.trailingSilenceSeconds, 1)}};
    nlohmann::json bands = nlohmann::json::array();
    for (std::size_t i = 0; i < r.bandCentres.size(); ++i) bands.push_back({{"hz", rd(r.bandCentres[i], 1)}, {"db", rd(r.bandDb[i], 1)}});
    j["spectrum"] = {{"thirdOctaveBands", bands}, {"centroidHz", rd(r.centroidHz, 0)}, {"rolloff85Hz", rd(r.rolloffHz, 0)},
                     {"energyPercent", {{"below120", rd(r.lowPercent, 1)}, {"from120to500", rd(r.lowMidPercent, 1)}, {"from500to2k", rd(r.midPercent, 1)},
                                        {"from2kto6k", rd(r.highMidPercent, 1)}, {"above6k", rd(r.highPercent, 1)}}}};
    j["stereo"] = {{"correlation", rd(r.correlation)}, {"widthPercent", rd(r.widthPercent, 1)}, {"balanceDb", rd(r.balanceDb)}};
    j["tempo"] = {{"bpm", rd(r.bpm, 1)}, {"confidence", rd(r.tempoConfidence)}};
    nlohmann::json tl = nlohmann::json::array();
    for (double v : r.shortTermTimeline) tl.push_back(rd(v, 1));
    j["shortTermLufsTimeline"] = tl;
    return j;
}

std::vector<std::string> describeReport(const AudioReport& r) {
    std::vector<std::string> out;
    auto say = [&](const std::string& s) { out.push_back(s); };
    char buf[200];
    if (r.seconds <= 0) return {"There is no audio."};
    if (r.clippedPercent > 0.01) {
        std::snprintf(buf, sizeof buf, "Clipping: %lld samples (%.3f %%) are at full scale; lower the level before the clip or use a limiter.", static_cast<long long>(r.clippedSamples), r.clippedPercent);
        say(buf);
    }
    if (r.peakDb > -0.1 && r.clippedPercent <= 0.01) say("The peak touches 0 dBFS: there is no headroom.");
    if (r.loudness.truePeakDb > -1.0) {
        std::snprintf(buf, sizeof buf, "True peak %.1f dBTP is above -1 dBTP: it can clip after lossy encoding.", r.loudness.truePeakDb);
        say(buf);
    }
    if (r.loudness.integrated > -69) {
        std::snprintf(buf, sizeof buf, "Loudness %.1f LUFS-I (streaming platforms aim at about -14; a mix to be mastered should have peaks around -6 dBFS).", r.loudness.integrated);
        say(buf);
        if (r.loudness.range > 14) say("A wide loudness range (over 14 LU): quiet and loud parts differ a lot; compression or level automation may help.");
        if (r.loudness.range < 3 && r.seconds > 20 && r.crestDb < 14) say("A very narrow loudness range (under 3 LU) with a low crest factor: the dynamics are already squeezed.");
    } else {
        say("The audio is silent or far below -70 LUFS.");
    }
    if (r.crestDb < 8) say("Low crest factor (under 8 dB): heavily compressed or limited.");
    if (r.crestDb > 20) say("High crest factor (over 20 dB): sharp peaks over a low level; a compressor or limiter would raise the loudness.");
    if (std::abs(r.dcOffset) > 0.002) say("A DC offset is present; a high-pass at 20 Hz removes it.");
    if (r.leadingSilenceSeconds > 2.0) say("More than two seconds of silence at the start.");
    if (r.trailingSilenceSeconds > 5.0) say("More than five seconds of silence at the end.");
    if (r.lowPercent > 55) say("Most of the energy is below 120 Hz: boomy or bass-heavy; check the low end.");
    if (r.highPercent + r.highMidPercent < 1.0 && r.lowPercent + r.lowMidPercent > 80) say("Almost nothing above 2 kHz: dull; a high shelf or exciter could add air.");
    if (r.highPercent > 20) say("A lot of energy above 6 kHz: bright or harsh.");
    if (r.channels > 1) {
        if (r.correlation < 0.0) say("The channels are out of phase (negative correlation): the mix will lose level and body in mono.");
        else if (r.correlation < 0.3) say("Low channel correlation: very wide; check the mono compatibility.");
        if (r.widthPercent < 1.0) say("The audio is practically mono.");
        if (std::abs(r.balanceDb) > 1.5) {
            std::snprintf(buf, sizeof buf, "The image leans %s by %.1f dB.", r.balanceDb > 0 ? "left" : "right", std::abs(r.balanceDb));
            say(buf);
        }
    }
    if (r.bpm > 0) {
        std::snprintf(buf, sizeof buf, "Tempo about %.1f bpm (confidence %.0f %%).", r.bpm, 100.0 * r.tempoConfidence);
        say(buf);
    }
    if (out.size() <= 1) say("Nothing stands out as a problem.");
    return out;
}

MaskingReport compareMasking(const AudioReport& a, const AudioReport& b) {
    MaskingReport m;
    m.bandCentres = a.bandCentres;
    const std::size_t n = std::min(a.bandDb.size(), b.bandDb.size());
    m.overlap.assign(n, 0.0);
    const double topA = *std::max_element(a.bandDb.begin(), a.bandDb.end()), topB = *std::max_element(b.bandDb.begin(), b.bandDb.end());
    double sum = 0.0;
    int active = 0;
    std::vector<std::pair<double, double>> ranked;  // overlap, hz
    for (std::size_t i = 0; i < n; ++i) {
        const bool strongA = a.bandDb[i] > topA - 25.0, strongB = b.bandDb[i] > topB - 25.0;  // within 25 dB of the loudest band of each
        if (!strongA && !strongB) continue;
        ++active;
        if (strongA && strongB) {
            const double d = std::abs((a.bandDb[i] - topA) - (b.bandDb[i] - topB));  // how alike their shapes are in that band
            m.overlap[i] = std::max(0.0, 1.0 - d / 12.0);
        }
        sum += m.overlap[i];
        ranked.emplace_back(m.overlap[i], m.bandCentres[i]);
    }
    m.score = active ? sum / active : 0.0;
    std::sort(ranked.rbegin(), ranked.rend());
    for (std::size_t i = 0; i < ranked.size() && i < 5; ++i)
        if (ranked[i].first > 0.5) m.worstHz.push_back(ranked[i].second);
    return m;
}

}  // namespace lpc
