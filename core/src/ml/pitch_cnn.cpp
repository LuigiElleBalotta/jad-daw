#include "lpc/ml/pitch_cnn.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

#include "lpc/ml/onnx.h"

namespace lpc::ml {

namespace {
constexpr int kWindow = 1024;
constexpr int kBins = 360;
constexpr double kBaseHz = 32.7032;  // C1
}  // namespace

std::vector<std::vector<PitchPeak>> neuralPitchPeaks(const std::vector<float>& xIn, int rate, int hop, std::size_t frameCount, double minProb, int maxPeaks) {
    static std::mutex mutex;
    static std::unique_ptr<Model> model;
    static bool tried = false;
    std::lock_guard<std::mutex> lock(mutex);
    if (!tried) {
        tried = true;
        model = Model::load(modelPath("RTPTM.model.onnx"));
        if (!model) model = Model::load(modelPath("RTPT.espresso.onnx"));
    }
    if (!model || rate <= 0 || hop <= 0) return {};
    // 16 kHz (linear interpolation; the sound is only analysed)
    std::vector<float> x;
    if (rate == 16000) {
        x = xIn;
    } else {
        const double ratio = static_cast<double>(rate) / 16000.0;
        x.resize(static_cast<std::size_t>(static_cast<double>(xIn.size()) / ratio));
        for (std::size_t i = 0; i < x.size() && !xIn.empty(); ++i) {
            const double pos = static_cast<double>(i) * ratio;
            const std::size_t k = static_cast<std::size_t>(pos);
            const double f = pos - static_cast<double>(k);
            const float a = xIn[std::min(k, xIn.size() - 1)], b = xIn[std::min(k + 1, xIn.size() - 1)];
            x[i] = static_cast<float>(a + f * (b - a));
        }
    }
    const double hop16 = static_cast<double>(hop) * 16000.0 / rate;
    std::vector<std::vector<PitchPeak>> out(frameCount);
    Tensor in;
    in.shape = {1, 1, 1, kWindow};
    in.data.resize(kWindow);
    for (std::size_t f = 0; f < frameCount; ++f) {
        const std::int64_t start = static_cast<std::int64_t>(std::llround(static_cast<double>(f) * hop16)) - kWindow / 2;
        double mean = 0.0;
        for (int i = 0; i < kWindow; ++i) {
            const std::int64_t p = start + i;
            const float v = p >= 0 && p < static_cast<std::int64_t>(x.size()) ? x[static_cast<std::size_t>(p)] : 0.0f;
            in.data[static_cast<std::size_t>(i)] = v;
            mean += v;
        }
        mean /= kWindow;
        double var = 0.0;
        for (float& v : in.data) {
            v = static_cast<float>(v - mean);
            var += static_cast<double>(v) * v;
        }
        var /= kWindow;
        if (var < 1e-10) continue;  // silence: no pitch
        const float inv = static_cast<float>(1.0 / std::sqrt(var));
        for (float& v : in.data) v *= inv;
        const std::vector<Tensor> res = model->run({in});
        if (res.empty() || res[0].data.size() < static_cast<std::size_t>(kBins)) continue;
        const std::vector<float>& y = res[0].data;
        std::vector<PitchPeak> peaks;
        for (int b = 1; b < kBins - 1; ++b) {
            if (y[static_cast<std::size_t>(b)] < minProb || y[static_cast<std::size_t>(b)] < y[static_cast<std::size_t>(b) - 1] || y[static_cast<std::size_t>(b)] <= y[static_cast<std::size_t>(b) + 1]) continue;
            const int lo = std::max(0, b - 4), hi = std::min(kBins - 1, b + 4);  // the weighted mean of the bins around the peak refines it
            double sw = 0.0, sb = 0.0;
            for (int k = lo; k <= hi; ++k) {
                sw += y[static_cast<std::size_t>(k)];
                sb += y[static_cast<std::size_t>(k)] * k;
            }
            const double bin = sb / sw;
            const double hz = kBaseHz * std::pow(2.0, bin * 20.0 / 1200.0);
            peaks.push_back({69.0 + 12.0 * std::log2(hz / 440.0), static_cast<double>(y[static_cast<std::size_t>(b)])});
        }
        std::sort(peaks.begin(), peaks.end(), [](const PitchPeak& a, const PitchPeak& b) { return a.prob > b.prob; });
        if (static_cast<int>(peaks.size()) > maxPeaks) peaks.resize(static_cast<std::size_t>(maxPeaks));
        out[f] = std::move(peaks);
    }
    return out;
}

}  // namespace lpc::ml
