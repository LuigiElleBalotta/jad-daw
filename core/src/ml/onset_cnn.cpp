#include "lpc/ml/onset_cnn.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <mutex>

#include "lpc/fft.h"
#include "lpc/ml/onnx.h"

namespace lpc::ml {

namespace {

constexpr double kRate = 44100.0;
constexpr int kHop = 441;
constexpr int kBands = 80;
constexpr int kContext = 15;
constexpr double kPi = 3.14159265358979323846;

// 44.1 kHz mono (linear interpolation: the sound is only analysed)
std::vector<float> toMono44k(const WavData& a) {
    const std::int64_t frames = a.frames();
    if (frames <= 0 || a.channels < 1 || a.sampleRate <= 0) return {};
    std::vector<float> mono(static_cast<std::size_t>(frames));
    for (std::int64_t i = 0; i < frames; ++i) {
        double s = 0.0;
        for (int c = 0; c < a.channels; ++c) s += a.samples[static_cast<std::size_t>(i * a.channels + c)];
        mono[static_cast<std::size_t>(i)] = static_cast<float>(s / a.channels);
    }
    if (a.sampleRate == 44100) return mono;
    const double ratio = static_cast<double>(a.sampleRate) / kRate;
    const std::size_t n = static_cast<std::size_t>(static_cast<double>(frames) / ratio);
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double pos = static_cast<double>(i) * ratio;
        const std::size_t k = static_cast<std::size_t>(pos);
        const double f = pos - static_cast<double>(k);
        const float x0 = mono[std::min(k, mono.size() - 1)], x1 = mono[std::min(k + 1, mono.size() - 1)];
        out[i] = static_cast<float>(x0 + f * (x1 - x0));
    }
    return out;
}

double hzToMel(double f) { return f < 1000.0 ? f * 3.0 / 200.0 : 15.0 + std::log(f / 1000.0) / (std::log(6.4) / 27.0); }
double melToHz(double m) { return m < 15.0 ? m * 200.0 / 3.0 : 1000.0 * std::exp((m - 15.0) * std::log(6.4) / 27.0); }

// 80 triangles from 27.5 Hz to 16 kHz on the mel scale, each normalised to sum 1
std::vector<std::vector<float>> melFilters(int fftSize) {
    const int bins = fftSize / 2 + 1;
    const double lo = hzToMel(27.5), hi = hzToMel(16000.0);
    std::vector<double> edges(kBands + 2);
    for (int i = 0; i < kBands + 2; ++i) edges[static_cast<std::size_t>(i)] = melToHz(lo + (hi - lo) * i / (kBands + 1));
    std::vector<std::vector<float>> w(kBands, std::vector<float>(static_cast<std::size_t>(bins), 0.0f));
    for (int m = 0; m < kBands; ++m) {
        const double f0 = edges[static_cast<std::size_t>(m)], f1 = edges[static_cast<std::size_t>(m) + 1], f2 = edges[static_cast<std::size_t>(m) + 2];
        double sum = 0.0;
        for (int k = 0; k < bins; ++k) {
            const double f = static_cast<double>(k) * kRate / fftSize;
            const double v = std::max(0.0, std::min((f - f0) / (f1 - f0), (f2 - f) / (f2 - f1)));
            w[static_cast<std::size_t>(m)][static_cast<std::size_t>(k)] = static_cast<float>(v);
            sum += v;
        }
        if (sum > 0.0)
            for (float& v : w[static_cast<std::size_t>(m)]) v = static_cast<float>(v / sum);
    }
    return w;
}

}  // namespace

std::vector<float> onsetFeatures(const WavData& audio, int& frames) {
    frames = 0;
    const std::vector<float> x = toMono44k(audio);
    if (x.size() < 4096) return {};
    frames = static_cast<int>(x.size() / kHop) + 1;
    std::vector<float> out(static_cast<std::size_t>(3 * kBands) * static_cast<std::size_t>(frames), 0.0f);
    int channel = 0;
    for (const int n : {1024, 2048, 4096}) {
        const Fft fft(n);
        const auto filters = melFilters(n);
        std::vector<double> window(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n);  // periodic Hann
        std::vector<std::complex<double>> buf(static_cast<std::size_t>(n));
        std::vector<double> mag(static_cast<std::size_t>(n / 2 + 1));
        const std::int64_t size = static_cast<std::int64_t>(x.size());
        for (int t = 0; t < frames; ++t) {
            const std::int64_t start = static_cast<std::int64_t>(t) * kHop - n / 2;  // centred: the signal is reflected at the ends
            for (int i = 0; i < n; ++i) {
                std::int64_t p = start + i;
                if (p < 0) p = -p;
                if (p >= size) p = 2 * (size - 1) - p;
                p = std::clamp<std::int64_t>(p, 0, size - 1);
                buf[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(p)] * window[static_cast<std::size_t>(i)];
            }
            fft.forward(buf.data());
            for (int k = 0; k <= n / 2; ++k) mag[static_cast<std::size_t>(k)] = std::abs(buf[static_cast<std::size_t>(k)]);
            for (int b = 0; b < kBands; ++b) {
                double s = 0.0;
                const auto& f = filters[static_cast<std::size_t>(b)];
                for (std::size_t k = 0; k < f.size(); ++k) s += f[k] * mag[k];
                out[(static_cast<std::size_t>(channel) * kBands + static_cast<std::size_t>(b)) * static_cast<std::size_t>(frames) + static_cast<std::size_t>(t)] =
                    static_cast<float>(std::log10(s + 1.0));
            }
        }
        ++channel;
    }
    return out;
}

OnsetActivation detectOnsetsCnn(const WavData& audio) {
    OnsetActivation result;
    static std::mutex mutex;  // one Model is not thread-safe
    static std::unique_ptr<Model> model;
    static bool tried = false;
    std::lock_guard<std::mutex> lock(mutex);
    if (!tried) {
        tried = true;
        model = Model::load(modelPath("OnsetDetectionCNN.espresso.onnx"));
    }
    if (!model) return result;
    int frames = 0;
    const std::vector<float> features = onsetFeatures(audio, frames);
    if (features.empty() || frames < 1) return result;
    Tensor in;
    in.shape = {1, 3, kBands, kContext};
    in.data.resize(static_cast<std::size_t>(3 * kBands * kContext));
    result.values.assign(static_cast<std::size_t>(frames), 0.0);
    const int half = kContext / 2;
    for (int t = 0; t < frames; ++t) {
        for (int c = 0; c < 3; ++c)
            for (int b = 0; b < kBands; ++b)
                for (int k = 0; k < kContext; ++k) {
                    const int tt = std::clamp(t - half + k, 0, frames - 1);  // the ends repeat the edge frame
                    in.data[(static_cast<std::size_t>(c) * kBands + static_cast<std::size_t>(b)) * kContext + static_cast<std::size_t>(k)] =
                        features[(static_cast<std::size_t>(c) * kBands + static_cast<std::size_t>(b)) * static_cast<std::size_t>(frames) + static_cast<std::size_t>(tt)];
                }
        const std::vector<Tensor> out = model->run({in});
        result.values[static_cast<std::size_t>(t)] = out.empty() || out[0].data.empty() ? 0.0 : out[0].data[0];
    }
    result.fps = kRate / kHop;
    result.hopSamples = kHop;
    return result;
}

}  // namespace lpc::ml
