#include "lpc/audio/live_meter.h"

#include <algorithm>
#include <cmath>

namespace lpc::audio {

namespace {
double cubic(double a, double b, double c, double d, double t) {
    return b + 0.5 * t * (c - a + t * (2.0 * a - 5.0 * b + 4.0 * c - d + t * (3.0 * (b - c) + d - a)));
}
constexpr std::size_t kMaxBlocks = 200000;  // about five and a half hours of 400 ms windows hopping 100 ms
}  // namespace

LiveMeter::LiveMeter(double sampleRate)
    : rate_(sampleRate), hop_(static_cast<std::size_t>(std::max(1.0, sampleRate / 10.0))), kl_(static_cast<int>(sampleRate)), kr_(static_cast<int>(sampleRate)),
      decay_(std::exp(-1.0 / (0.3 * sampleRate))) {}

void LiveMeter::reset() {
    *this = LiveMeter(rate_);
}

void LiveMeter::process(const float* l, const float* r, std::size_t frames) {
    for (std::size_t i = 0; i < frames; ++i) {
        const double a = l[i], b = r[i];
        const double ka = kl_.run(a), kb = kr_.run(b);
        acc_ += ka * ka + kb * kb;
        if (++inHop_ == hop_) {
            hops_.push_back(acc_ / static_cast<double>(hop_));
            if (hops_.size() > 30) hops_.pop_front();
            acc_ = 0.0;
            inHop_ = 0;
            if (hops_.size() >= 4 && blocks_.size() < kMaxBlocks) blocks_.push_back(energyOf(4));
        }
        // true peak: the sample and three points of a cubic between the previous two samples (the history is one sample behind)
        for (int c = 0; c < 2; ++c) {
            double* h = hist_[c];
            h[0] = h[1]; h[1] = h[2]; h[2] = h[3]; h[3] = c == 0 ? a : b;
            truePeak_ = std::max(truePeak_, std::abs(h[3]));
            for (const double t : {0.25, 0.5, 0.75}) truePeak_ = std::max(truePeak_, std::abs(cubic(h[0], h[1], h[2], h[3], t)));
        }
        sumLR_ = sumLR_ * decay_ + a * b;
        sumLL_ = sumLL_ * decay_ + a * a;
        sumRR_ = sumRR_ * decay_ + b * b;
    }
}

double LiveMeter::energyOf(std::size_t hops) const {
    if (hops_.size() < hops) return 0.0;
    double sum = 0.0;
    for (std::size_t i = hops_.size() - hops; i < hops_.size(); ++i) sum += hops_[i];
    return sum / static_cast<double>(hops);
}

double LiveMeter::momentary() const { return hops_.size() >= 4 ? blockLufs(energyOf(4)) : -200.0; }
double LiveMeter::shortTerm() const { return hops_.size() >= 30 ? blockLufs(energyOf(30)) : -200.0; }

double LiveMeter::integrated() const {
    double sum = 0.0;
    std::size_t n = 0;
    for (const double e : blocks_)
        if (blockLufs(e) > -70.0) { sum += e; ++n; }
    if (n == 0) return -200.0;
    const double gate = blockLufs(sum / static_cast<double>(n)) - 10.0;
    double sum2 = 0.0;
    std::size_t n2 = 0;
    for (const double e : blocks_)
        if (blockLufs(e) > -70.0 && blockLufs(e) > gate) { sum2 += e; ++n2; }
    return n2 ? blockLufs(sum2 / static_cast<double>(n2)) : -200.0;
}

double LiveMeter::correlation() const {
    const double d = std::sqrt(sumLL_ * sumRR_);
    return d > 1e-12 ? std::clamp(sumLR_ / d, -1.0, 1.0) : 0.0;
}

}  // namespace lpc::audio
