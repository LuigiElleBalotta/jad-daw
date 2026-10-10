#include "lpc/flac.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace lpc {

namespace {

constexpr int kBlock = 4096;

class BitWriter {
public:
    void put(std::uint64_t value, int bits) {
        for (int i = bits - 1; i >= 0; --i) {
            current_ = static_cast<std::uint8_t>((current_ << 1) | ((value >> i) & 1u));
            if (++filled_ == 8) {
                bytes.push_back(current_);
                current_ = 0;
                filled_ = 0;
            }
        }
    }
    void putSigned(std::int64_t value, int bits) { put(static_cast<std::uint64_t>(value) & ((bits >= 64) ? ~std::uint64_t{0} : ((std::uint64_t{1} << bits) - 1)), bits); }
    void unary(std::uint64_t zeros) {  // `zeros` zero bits then a one
        for (std::uint64_t i = 0; i < zeros; ++i) put(0, 1);
        put(1, 1);
    }
    void align() {
        while (filled_ != 0) put(0, 1);
    }
    std::vector<std::uint8_t> bytes;

private:
    std::uint8_t current_ = 0;
    int filled_ = 0;
};

std::uint8_t crc8(const std::vector<std::uint8_t>& data) {
    std::uint8_t crc = 0;
    for (const std::uint8_t b : data) {
        crc ^= b;
        for (int i = 0; i < 8; ++i) crc = static_cast<std::uint8_t>((crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1));
    }
    return crc;
}

std::uint16_t crc16(const std::vector<std::uint8_t>& data) {
    std::uint16_t crc = 0;
    for (const std::uint8_t b : data) {
        crc = static_cast<std::uint16_t>(crc ^ (b << 8));
        for (int i = 0; i < 8; ++i) crc = static_cast<std::uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x8005 : (crc << 1));
    }
    return crc;
}

std::uint64_t zigzag(std::int64_t v) { return v >= 0 ? static_cast<std::uint64_t>(v) << 1 : (static_cast<std::uint64_t>(-(v + 1)) << 1) | 1; }

// The residual of the fixed predictor of `order` over samples[0, n): residual[i] for i >= order.
std::vector<std::int64_t> fixedResidual(const std::vector<std::int32_t>& s, int order) {
    std::vector<std::int64_t> r(s.size(), 0);
    for (std::size_t i = static_cast<std::size_t>(order); i < s.size(); ++i) {
        const std::int64_t x = s[i];
        std::int64_t p = 0;
        switch (order) {
            case 0: p = 0; break;
            case 1: p = s[i - 1]; break;
            case 2: p = 2 * static_cast<std::int64_t>(s[i - 1]) - s[i - 2]; break;
            case 3: p = 3 * static_cast<std::int64_t>(s[i - 1]) - 3 * static_cast<std::int64_t>(s[i - 2]) + s[i - 3]; break;
            default: p = 4 * static_cast<std::int64_t>(s[i - 1]) - 6 * static_cast<std::int64_t>(s[i - 2]) + 4 * static_cast<std::int64_t>(s[i - 3]) - s[i - 4]; break;
        }
        r[i] = x - p;
    }
    return r;
}

struct RicePlan {
    int partitionOrder = 0;
    std::vector<int> params;  // one per partition
    std::uint64_t bits = std::numeric_limits<std::uint64_t>::max();
};

// The cheapest Rice coding of residual[order, n): partition orders 0..6, parameters 0..14.
RicePlan planRice(const std::vector<std::int64_t>& residual, int order) {
    const int n = static_cast<int>(residual.size());
    RicePlan best;
    for (int po = 0; po <= 6; ++po) {
        const int parts = 1 << po;
        if (n % parts != 0 || n / parts <= order) break;
        RicePlan plan;
        plan.partitionOrder = po;
        plan.bits = 0;
        bool ok = true;
        for (int p = 0; p < parts && ok; ++p) {
            const int lo = p == 0 ? order : p * (n / parts), hi = (p + 1) * (n / parts);
            std::uint64_t bestBits = std::numeric_limits<std::uint64_t>::max();
            int bestK = 0;
            for (int k = 0; k <= 14; ++k) {
                std::uint64_t bits = 4;  // the parameter
                for (int i = lo; i < hi; ++i) bits += (zigzag(residual[static_cast<std::size_t>(i)]) >> k) + 1 + static_cast<std::uint64_t>(k);
                if (bits < bestBits) {
                    bestBits = bits;
                    bestK = k;
                }
                if (bits > bestBits + 64 && k > bestK + 2) break;  // getting worse
            }
            plan.params.push_back(bestK);
            plan.bits += bestBits;
            if (bestBits > (std::uint64_t{1} << 40)) ok = false;
        }
        if (ok && plan.bits < best.bits) best = plan;
    }
    return best;
}

void writeSubframe(BitWriter& w, const std::vector<std::int32_t>& s, int bps) {
    const int n = static_cast<int>(s.size());
    if (std::all_of(s.begin(), s.end(), [&](std::int32_t v) { return v == s.front(); })) {  // constant (silence)
        w.put(0, 1);
        w.put(0, 6);
        w.put(0, 1);
        w.putSigned(s.front(), bps);
        return;
    }
    int bestOrder = -1;
    RicePlan bestPlan;
    std::vector<std::int64_t> bestResidual;
    std::uint64_t bestBits = static_cast<std::uint64_t>(n) * static_cast<std::uint64_t>(bps);  // verbatim
    for (int order = 0; order <= 4 && order < n; ++order) {
        const std::vector<std::int64_t> r = fixedResidual(s, order);
        const RicePlan plan = planRice(r, order);
        const std::uint64_t total = static_cast<std::uint64_t>(order) * static_cast<std::uint64_t>(bps) + 2 + 4 + plan.bits;
        if (plan.bits != std::numeric_limits<std::uint64_t>::max() && total < bestBits) {
            bestBits = total;
            bestOrder = order;
            bestPlan = plan;
            bestResidual = r;
        }
    }
    if (bestOrder < 0) {  // verbatim
        w.put(0, 1);
        w.put(1, 6);
        w.put(0, 1);
        for (const std::int32_t v : s) w.putSigned(v, bps);
        return;
    }
    w.put(0, 1);
    w.put(8 + static_cast<std::uint64_t>(bestOrder), 6);  // 001xxx: fixed predictor
    w.put(0, 1);
    for (int i = 0; i < bestOrder; ++i) w.putSigned(s[static_cast<std::size_t>(i)], bps);  // warm-up samples
    w.put(0, 2);                                                                              // Rice coding with 4-bit parameters
    w.put(static_cast<std::uint64_t>(bestPlan.partitionOrder), 4);
    const int parts = 1 << bestPlan.partitionOrder;
    for (int p = 0; p < parts; ++p) {
        const int k = bestPlan.params[static_cast<std::size_t>(p)];
        w.put(static_cast<std::uint64_t>(k), 4);
        const int lo = p == 0 ? bestOrder : p * (n / parts), hi = (p + 1) * (n / parts);
        for (int i = lo; i < hi; ++i) {
            const std::uint64_t z = zigzag(bestResidual[static_cast<std::size_t>(i)]);
            w.unary(z >> k);
            if (k > 0) w.put(z & ((std::uint64_t{1} << k) - 1), k);
        }
    }
}

void putUtf8Number(BitWriter& w, std::uint64_t v) {
    if (v < 0x80) {
        w.put(v, 8);
        return;
    }
    static const int capacity[] = {7, 11, 16, 21, 26, 31, 36};  // bits held by 1 to 7 bytes
    int bytes = 1;
    while (bytes < 7 && v >= (std::uint64_t{1} << capacity[bytes - 1])) ++bytes;
    const int cont = bytes - 1;
    const std::uint8_t lead = static_cast<std::uint8_t>((0xff00 >> bytes) & 0xff);
    w.put(static_cast<std::uint8_t>(lead | (v >> (6 * cont))), 8);
    for (int i = cont - 1; i >= 0; --i) w.put(0x80 | ((v >> (6 * i)) & 0x3f), 8);
}

}  // namespace

void writeFlac(const std::filesystem::path& path, int sampleRate, int channels, const std::vector<float>& interleaved, int bits) {
    if (channels < 1 || channels > 8) throw std::runtime_error("FLAC takes 1 to 8 channels");
    if (bits != 16 && bits != 24) throw std::runtime_error("FLAC output is 16 or 24 bits");
    if (sampleRate < 1 || sampleRate > 655350) throw std::runtime_error("sample rate out of range for FLAC");
    if (interleaved.size() % static_cast<std::size_t>(channels) != 0) throw std::runtime_error("the sample count does not match the channel count");
    const std::size_t total = interleaved.size() / static_cast<std::size_t>(channels);
    const double scale = std::pow(2.0, bits - 1) - 1.0;
    std::vector<std::vector<std::int32_t>> pcm(static_cast<std::size_t>(channels), std::vector<std::int32_t>(total));
    for (std::size_t i = 0; i < total; ++i)
        for (int c = 0; c < channels; ++c) {
            const double v = std::clamp(static_cast<double>(interleaved[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)]), -1.0, 1.0);
            pcm[static_cast<std::size_t>(c)][i] = static_cast<std::int32_t>(std::lround(v * scale));
        }

    std::vector<std::uint8_t> frames;
    std::uint32_t minFrame = std::numeric_limits<std::uint32_t>::max(), maxFrame = 0;
    std::uint64_t frameNumber = 0;
    for (std::size_t pos = 0; pos < total; pos += kBlock, ++frameNumber) {
        const int n = static_cast<int>(std::min<std::size_t>(kBlock, total - pos));
        BitWriter w;
        w.put(0x3ffe, 14);  // sync
        w.put(0, 1);
        w.put(0, 1);        // fixed block size
        w.put(n == kBlock ? 12 : 7, 4);  // 4096, or the size after the frame number (16 bits)
        w.put(0, 4);        // the sample rate is in STREAMINFO
        w.put(static_cast<std::uint64_t>(channels - 1), 4);
        w.put(0, 3);        // the sample size is in STREAMINFO
        w.put(0, 1);
        putUtf8Number(w, frameNumber);
        if (n != kBlock) w.put(static_cast<std::uint64_t>(n - 1), 16);
        w.put(crc8(w.bytes), 8);
        for (int c = 0; c < channels; ++c) {
            std::vector<std::int32_t> block(pcm[static_cast<std::size_t>(c)].begin() + static_cast<std::ptrdiff_t>(pos),
                                            pcm[static_cast<std::size_t>(c)].begin() + static_cast<std::ptrdiff_t>(pos) + n);
            writeSubframe(w, block, bits);
        }
        w.align();
        const std::uint16_t crc = crc16(w.bytes);
        w.put(crc, 16);
        minFrame = std::min<std::uint32_t>(minFrame, static_cast<std::uint32_t>(w.bytes.size()));
        maxFrame = std::max<std::uint32_t>(maxFrame, static_cast<std::uint32_t>(w.bytes.size()));
        frames.insert(frames.end(), w.bytes.begin(), w.bytes.end());
    }
    if (total == 0) minFrame = 0;

    BitWriter head;
    for (const char ch : {'f', 'L', 'a', 'C'}) head.put(static_cast<std::uint8_t>(ch), 8);
    head.put(0x80, 8);  // the last metadata block, type STREAMINFO
    head.put(34, 24);
    head.put(kBlock, 16);
    head.put(kBlock, 16);
    head.put(minFrame, 24);
    head.put(maxFrame, 24);
    head.put(static_cast<std::uint64_t>(sampleRate), 20);
    head.put(static_cast<std::uint64_t>(channels - 1), 3);
    head.put(static_cast<std::uint64_t>(bits - 1), 5);
    head.put(total, 36);
    for (int i = 0; i < 16; ++i) head.put(0, 8);  // MD5 of the audio: unknown

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out.write(reinterpret_cast<const char*>(head.bytes.data()), static_cast<std::streamsize>(head.bytes.size()));
    out.write(reinterpret_cast<const char*>(frames.data()), static_cast<std::streamsize>(frames.size()));
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

}  // namespace lpc
