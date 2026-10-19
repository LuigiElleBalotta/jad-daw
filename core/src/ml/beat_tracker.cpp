#include "lpc/ml/beat_tracker.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>
#include <mutex>

#include "lpc/fft.h"
#include "lpc/ml/onnx.h"

namespace lpc::ml {

namespace {

constexpr double kRate = 22050.0;
constexpr int kFft = 2048;
constexpr int kHop = 441;
constexpr int kMels = 131;
constexpr int kWindow = 4096;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> toMono22k(const WavData& a) {
    const std::int64_t frames = a.frames();
    if (frames <= 0 || a.channels < 1 || a.sampleRate <= 0) return {};
    std::vector<float> mono(static_cast<std::size_t>(frames));
    for (std::int64_t i = 0; i < frames; ++i) {
        double s = 0.0;
        for (int c = 0; c < a.channels; ++c) s += a.samples[static_cast<std::size_t>(i * a.channels + c)];
        mono[static_cast<std::size_t>(i)] = static_cast<float>(s / a.channels);
    }
    const double ratio = static_cast<double>(a.sampleRate) / kRate;
    const std::size_t n = static_cast<std::size_t>(static_cast<double>(frames) / ratio);
    std::vector<float> out(n);
    const int reach = std::max(1, static_cast<int>(std::floor(ratio)));  // a short box filter before decimating
    for (std::size_t i = 0; i < n; ++i) {
        const double pos = static_cast<double>(i) * ratio;
        const std::int64_t k = static_cast<std::int64_t>(pos);
        double s = 0.0;
        int count = 0;
        for (int d = -reach / 2; d <= reach / 2; ++d) {
            const std::int64_t p = std::clamp<std::int64_t>(k + d, 0, frames - 1);
            s += mono[static_cast<std::size_t>(p)];
            ++count;
        }
        out[i] = static_cast<float>(s / std::max(1, count));
    }
    return out;
}

double hzToMel(double f) { return f < 1000.0 ? f * 3.0 / 200.0 : 15.0 + std::log(f / 1000.0) / (std::log(6.4) / 27.0); }
double melToHz(double m) { return m < 15.0 ? m * 200.0 / 3.0 : 1000.0 * std::exp((m - 15.0) * std::log(6.4) / 27.0); }

// 131 Slaney-mel triangles (area normalised, as librosa) from 30 Hz to 11 kHz
std::vector<std::vector<float>> melFilters() {
    const int bins = kFft / 2 + 1;
    const double lo = hzToMel(30.0), hi = hzToMel(11000.0);
    std::vector<double> edges(kMels + 2);
    for (int i = 0; i < kMels + 2; ++i) edges[static_cast<std::size_t>(i)] = melToHz(lo + (hi - lo) * i / (kMels + 1));
    std::vector<std::vector<float>> w(kMels, std::vector<float>(static_cast<std::size_t>(bins), 0.0f));
    for (int m = 0; m < kMels; ++m) {
        const double f0 = edges[static_cast<std::size_t>(m)], f1 = edges[static_cast<std::size_t>(m) + 1], f2 = edges[static_cast<std::size_t>(m) + 2];
        for (int k = 0; k < bins; ++k) {
            const double f = static_cast<double>(k) * kRate / kFft;
            const double v = std::max(0.0, std::min((f - f0) / (f1 - f0), (f2 - f) / (f2 - f1)));
            w[static_cast<std::size_t>(m)][static_cast<std::size_t>(k)] = static_cast<float>(v * 2.0 / (f2 - f0));
        }
    }
    return w;
}

// log(1 + mel magnitude), band-major: [band][frame]
std::vector<float> logMel(const std::vector<float>& x, int& frames) {
    frames = static_cast<int>(x.size() / kHop) + 1;
    const Fft fft(kFft);
    const auto filters = melFilters();
    std::vector<double> window(kFft);
    for (int i = 0; i < kFft; ++i) window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / kFft);
    std::vector<std::complex<double>> buf(kFft);
    std::vector<double> mag(kFft / 2 + 1);
    std::vector<float> out(static_cast<std::size_t>(kMels) * static_cast<std::size_t>(frames), 0.0f);
    const std::int64_t size = static_cast<std::int64_t>(x.size());
    for (int t = 0; t < frames; ++t) {
        const std::int64_t start = static_cast<std::int64_t>(t) * kHop - kFft / 2;
        for (int i = 0; i < kFft; ++i) {
            std::int64_t p = start + i;
            if (p < 0) p = -p;
            if (p >= size) p = 2 * (size - 1) - p;
            p = std::clamp<std::int64_t>(p, 0, size - 1);
            buf[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(p)] * window[static_cast<std::size_t>(i)];
        }
        fft.forward(buf.data());
        for (int k = 0; k <= kFft / 2; ++k) mag[static_cast<std::size_t>(k)] = std::abs(buf[static_cast<std::size_t>(k)]);
        for (int b = 0; b < kMels; ++b) {
            double s = 0.0;
            const auto& f = filters[static_cast<std::size_t>(b)];
            for (std::size_t k = 0; k < f.size(); ++k) s += f[k] * mag[k];
            out[static_cast<std::size_t>(b) * static_cast<std::size_t>(frames) + static_cast<std::size_t>(t)] = static_cast<float>(std::log1p(s));
        }
    }
    return out;
}

}  // namespace

BeatActivation detectBeatsCnn(const WavData& audio) {
    BeatActivation result;
    static std::mutex mutex;
    static std::unique_ptr<Model> model;
    static bool tried = false;
    std::lock_guard<std::mutex> lock(mutex);
    if (!tried) {
        tried = true;
        model = Model::load(modelPath("DownbeatTrackerCondMPS.model.onnx"));
    }
    if (!model) return result;
    const std::vector<float> x = toMono22k(audio);
    if (x.size() < static_cast<std::size_t>(kRate * 3.0)) return result;
    int frames = 0;
    const std::vector<float> mel = logMel(x, frames);
    result.beats.assign(static_cast<std::size_t>(frames), 0.0);
    result.downbeats.assign(static_cast<std::size_t>(frames), 0.0);
    result.meter.assign(13, 0.0);
    result.tempo.assign(251, 0.0);
    result.fps = kRate / kHop;
    result.hopSamples = kHop;
    result.rate = kRate;
    const std::vector<std::string> names = model->outputNames();
    auto indexOf = [&](const char* n) {
        for (std::size_t i = 0; i < names.size(); ++i)
            if (names[i] == n) return static_cast<int>(i);
        return -1;
    };
    const int iBeats = indexOf("output_beats"), iDown = indexOf("output_downbeats"), iTempo = indexOf("output_tempo"), iMeter = indexOf("output_bpb");
    if (iBeats < 0 || iDown < 0) return BeatActivation{};
    // windows of 4096 frames, a new one every 2048: the middle of each is kept (the first and last windows keep their outer halves)
    const int step = kWindow / 2;
    double counted = 0.0;
    Tensor in;
    in.shape = {1, kWindow, kMels};
    in.data.resize(static_cast<std::size_t>(kWindow) * kMels);
    for (int begin = 0;; begin += step) {
        for (int t = 0; t < kWindow; ++t)
            for (int b = 0; b < kMels; ++b) {
                const int tt = begin + t;
                in.data[static_cast<std::size_t>(t) * kMels + static_cast<std::size_t>(b)] = tt < frames ? mel[static_cast<std::size_t>(b) * static_cast<std::size_t>(frames) + static_cast<std::size_t>(tt)] : 0.0f;
            }
        const std::vector<Tensor> out = model->run({in});
        const int lo = begin == 0 ? 0 : begin + kWindow / 4;
        const int hi = begin + kWindow >= frames ? frames : begin + kWindow - kWindow / 4;
        for (int t = lo; t < hi && t < frames; ++t) {
            const std::size_t k = static_cast<std::size_t>(t - begin);
            result.beats[static_cast<std::size_t>(t)] = out[static_cast<std::size_t>(iBeats)].data[k];
            result.downbeats[static_cast<std::size_t>(t)] = out[static_cast<std::size_t>(iDown)].data[k];
            if (iMeter >= 0)
                for (int c = 0; c < 13; ++c) result.meter[static_cast<std::size_t>(c)] += out[static_cast<std::size_t>(iMeter)].data[k * 13 + static_cast<std::size_t>(c)];
            if (iTempo >= 0)
                for (int c = 0; c < 251; ++c) result.tempo[static_cast<std::size_t>(c)] += out[static_cast<std::size_t>(iTempo)].data[k * 251 + static_cast<std::size_t>(c)];
            counted += 1.0;
        }
        if (begin + kWindow >= frames) break;
    }
    if (counted > 0.0) {
        for (double& v : result.meter) v /= counted;
        for (double& v : result.tempo) v /= counted;
    }
    return result;
}

}  // namespace lpc::ml
