#include "lpc/tempo_analysis.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

#include "lpc/fft.h"

namespace lpc {

namespace {

constexpr int kFrame = 1024;
constexpr int kHop = 256;

struct Mono {
    std::vector<float> x;
    int decimation = 1;
    double rate = 0.0;  // of x
};

Mono toMono(const WavData& a) {
    Mono m;
    m.decimation = std::max(1, static_cast<int>(std::lround(a.sampleRate / 22050.0)));
    m.rate = static_cast<double>(a.sampleRate) / m.decimation;
    const std::int64_t frames = a.frames();
    const std::int64_t n = frames / m.decimation;
    m.x.assign(static_cast<std::size_t>(n), 0.0f);
    for (std::int64_t i = 0; i < n; ++i) {
        double s = 0.0;
        for (int d = 0; d < m.decimation; ++d)
            for (int c = 0; c < a.channels; ++c) s += a.samples[static_cast<std::size_t>((i * m.decimation + d) * a.channels + c)];
        m.x[static_cast<std::size_t>(i)] = static_cast<float>(s / (m.decimation * a.channels));
    }
    return m;
}

// log-compressed spectral flux, one value per hop
std::vector<double> onsetStrength(const Mono& m) {
    if (static_cast<int>(m.x.size()) < kFrame * 2) return {};
    const Fft fft(kFrame);
    std::vector<double> window(kFrame);
    for (int i = 0; i < kFrame; ++i) window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / kFrame);
    const int bins = kFrame / 2;
    const std::size_t frames = (m.x.size() - kFrame) / kHop;
    std::vector<double> prev(static_cast<std::size_t>(bins), 0.0), env(frames, 0.0);
    std::vector<std::complex<double>> buf(kFrame);
    for (std::size_t t = 0; t < frames; ++t) {
        for (int i = 0; i < kFrame; ++i) buf[static_cast<std::size_t>(i)] = m.x[t * kHop + static_cast<std::size_t>(i)] * window[static_cast<std::size_t>(i)];
        fft.forward(buf.data());
        double flux = 0.0;
        for (int k = 1; k < bins; ++k) {
            const double mag = std::log1p(200.0 * std::abs(buf[static_cast<std::size_t>(k)]) / kFrame);
            const double d = mag - prev[static_cast<std::size_t>(k)];
            if (d > 0 && t > 0) flux += d;
            prev[static_cast<std::size_t>(k)] = mag;
        }
        env[t] = flux;
    }
    // zero mean over a second, unit scale
    const int half = static_cast<int>(0.5 * m.rate / kHop);
    std::vector<double> out(frames, 0.0);
    double sq = 0.0;
    for (std::size_t t = 0; t < frames; ++t) {
        const std::size_t lo = t > static_cast<std::size_t>(half) ? t - static_cast<std::size_t>(half) : 0, hi = std::min(frames, t + static_cast<std::size_t>(half) + 1);
        double mean = 0.0;
        for (std::size_t i = lo; i < hi; ++i) mean += env[i];
        mean /= static_cast<double>(hi - lo);
        out[t] = std::max(0.0, env[t] - mean);
        sq += out[t] * out[t];
    }
    const double sd = std::sqrt(sq / static_cast<double>(frames));
    if (sd > 1e-9)
        for (double& v : out) v /= sd;
    return out;
}

}  // namespace

TempoAnalysis analyseTempo(const WavData& audio, double minBpm, double maxBpm) {
    TempoAnalysis result;
    if (audio.frames() < audio.sampleRate || audio.channels < 1 || audio.sampleRate <= 0) return result;
    const Mono mono = toMono(audio);
    const std::vector<double> onset = onsetStrength(mono);
    if (onset.size() < 64) return result;
    const double fps = mono.rate / kHop;  // onset frames per second
    const std::size_t n = onset.size();

    // tempo: the autocorrelation of the onset curve, weighted towards 120 bpm
    const int lagLo = std::max(2, static_cast<int>(std::floor(60.0 * fps / maxBpm)));
    const int lagHi = std::min(static_cast<int>(n / 2), static_cast<int>(std::ceil(60.0 * fps / minBpm)));
    if (lagHi <= lagLo + 2) return result;
    std::vector<double> ac(static_cast<std::size_t>(lagHi * 2 + 2), 0.0);
    for (int lag = 1; lag < static_cast<int>(ac.size()) && lag < static_cast<int>(n); ++lag) {
        double s = 0.0;
        for (std::size_t t = static_cast<std::size_t>(lag); t < n; ++t) s += onset[t] * onset[t - static_cast<std::size_t>(lag)];
        ac[static_cast<std::size_t>(lag)] = s / static_cast<double>(n - static_cast<std::size_t>(lag));
    }
    const double t0 = 60.0 * fps / 120.0;
    std::vector<double> score(static_cast<std::size_t>(lagHi + 1), 0.0);
    double best = 0.0, mean = 0.0;
    int bestLag = 0;
    for (int lag = lagLo; lag <= lagHi; ++lag) {
        // the pulse also shows at twice the lag and at half of it
        double s = ac[static_cast<std::size_t>(lag)] + 0.5 * ac[static_cast<std::size_t>(std::min<int>(lag * 2, static_cast<int>(ac.size()) - 1))];
        if (lag % 2 == 0) s += 0.25 * ac[static_cast<std::size_t>(lag / 2)];
        const double w = std::exp(-0.5 * std::pow(std::log2(lag / t0) / 1.0, 2.0));
        score[static_cast<std::size_t>(lag)] = s * w;
        mean += score[static_cast<std::size_t>(lag)];
        if (score[static_cast<std::size_t>(lag)] > best) { best = score[static_cast<std::size_t>(lag)]; bestLag = lag; }
    }
    if (bestLag == 0 || best <= 0.0) return result;
    mean /= (lagHi - lagLo + 1);
    result.confidence = std::clamp((best / std::max(mean, 1e-9) - 1.0) / 6.0, 0.0, 1.0);
    // a parabola through the peak and its neighbours for a period finer than a frame
    double period = bestLag;
    if (bestLag > lagLo && bestLag < lagHi) {
        const double a = score[static_cast<std::size_t>(bestLag - 1)], b = score[static_cast<std::size_t>(bestLag)], c = score[static_cast<std::size_t>(bestLag + 1)];
        const double den = a - 2 * b + c;
        if (std::abs(den) > 1e-12) period += std::clamp(0.5 * (a - c) / den, -0.5, 0.5);
    }

    // the beats: dynamic programming (Ellis 2007)
    const double alpha = 100.0;
    std::vector<double> cum(n, 0.0);
    std::vector<int> back(n, -1);
    for (std::size_t t = 0; t < n; ++t) {
        const int from = static_cast<int>(std::lround(static_cast<double>(t) - 2.0 * period)), to = static_cast<int>(std::lround(static_cast<double>(t) - period / 2.0));
        double bestPrev = 0.0;
        int arg = -1;
        bool any = false;
        for (int p = std::max(0, from); p <= to; ++p) {
            if (p < 0 || p >= static_cast<int>(t)) continue;
            const double d = std::log(static_cast<double>(t - static_cast<std::size_t>(p)) / period);
            const double v = cum[static_cast<std::size_t>(p)] - alpha * d * d;
            if (!any || v > bestPrev) { bestPrev = v; arg = p; any = true; }
        }
        if (any && bestPrev > 0) { cum[t] = onset[t] + bestPrev; back[t] = arg; }
        else cum[t] = onset[t];
    }
    // the last beat is the best score among the final period
    std::size_t last = n - 1;
    {
        double top = -1e300;
        const std::size_t from = n > static_cast<std::size_t>(period) ? n - static_cast<std::size_t>(period) : 0;
        for (std::size_t t = from; t < n; ++t)
            if (cum[t] > top) { top = cum[t]; last = t; }
    }
    std::vector<std::size_t> frames;
    for (int t = static_cast<int>(last); t >= 0; t = back[static_cast<std::size_t>(t)]) {
        frames.push_back(static_cast<std::size_t>(t));
        if (back[static_cast<std::size_t>(t)] < 0) break;
    }
    std::reverse(frames.begin(), frames.end());
    if (frames.size() < 3) return result;

    // sample positions: the middle of the analysis frame, then the sharpest rise in energy within 30 ms of it
    const double scale = static_cast<double>(mono.decimation);
    std::vector<std::int64_t> beats;
    const std::int64_t total = audio.frames();
    const std::int64_t block = std::max<std::int64_t>(8, audio.sampleRate / 500);  // 2 ms
    const std::int64_t reach = audio.sampleRate * 30 / 1000;
    auto energyOf = [&](std::int64_t b) {
        double e = 0.0;
        for (std::int64_t i = b * block; i < (b + 1) * block && i < total; ++i)
            for (int c = 0; c < audio.channels; ++c) {
                const double v = audio.samples[static_cast<std::size_t>(i * audio.channels + c)];
                e += v * v;
            }
        return e;
    };
    for (std::size_t f : frames) {
        const std::int64_t centre = static_cast<std::int64_t>((static_cast<double>(f) * kHop + kFrame / 2.0) * scale);
        const std::int64_t lo = std::max<std::int64_t>(block, centre - reach), hi = std::min<std::int64_t>(total - 1, centre + reach);
        double top = 0.0;
        std::int64_t at = centre;
        for (std::int64_t b = lo / block; b <= hi / block; ++b) {
            const double rise = energyOf(b) - energyOf(b - 1);
            if (rise > top) { top = rise; at = b * block; }
        }
        if (at < 0 || at >= total) continue;
        if (!beats.empty() && at <= beats.back()) continue;
        beats.push_back(at);
    }
    if (beats.size() < 3) return result;

    // a bar of four: the phase whose beats carry the most onset strength opens the bars
    result.beatsPerBar = 4;
    double bestPhase = -1.0;
    for (int p = 0; p < 4; ++p) {
        double sum = 0.0;
        int count = 0;
        for (std::size_t i = static_cast<std::size_t>(p); i < frames.size(); i += 4, ++count) sum += onset[frames[i]];
        const double avg = count ? sum / count : 0.0;
        if (avg > bestPhase + 1e-9) { bestPhase = avg; result.firstDownbeat = p; }
    }
    // the tempo from the beats found (their median distance), which is finer than the period on the onset curve
    std::vector<double> gaps;
    for (std::size_t i = 1; i < beats.size(); ++i) gaps.push_back(static_cast<double>(beats[i] - beats[i - 1]));
    std::nth_element(gaps.begin(), gaps.begin() + static_cast<std::ptrdiff_t>(gaps.size() / 2), gaps.end());
    result.bpm = 60.0 * audio.sampleRate / gaps[gaps.size() / 2];
    result.beats = std::move(beats);
    return result;
}

}  // namespace lpc
