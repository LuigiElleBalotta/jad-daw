#include "lpc/ml/neural_stream.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#include "lpc/ml/onnx.h"

namespace lpc::ml {

namespace {

// single-producer single-consumer ring of samples
class FloatRing {
public:
    explicit FloatRing(std::size_t capacityPow2) : buf_(capacityPow2, 0.0f), mask_(capacityPow2 - 1) {}
    std::size_t available() const noexcept { return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_relaxed); }
    std::size_t space() const noexcept { return buf_.size() - (tail_.load(std::memory_order_relaxed) - head_.load(std::memory_order_acquire)); }
    bool push(const float* x, std::size_t n) noexcept {
        if (space() < n) return false;
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < n; ++i) buf_[(t + i) & mask_] = x[i];
        tail_.store(t + n, std::memory_order_release);
        return true;
    }
    bool pop(float* x, std::size_t n) noexcept {
        if (available() < n) return false;
        const std::size_t h = head_.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < n; ++i) x[i] = buf_[(h + i) & mask_];
        head_.store(h + n, std::memory_order_release);
        return true;
    }

private:
    std::vector<float> buf_;
    std::size_t mask_;
    std::atomic<std::size_t> head_{0}, tail_{0};
};

#ifdef LPC_WITH_ONNX

class StreamImpl final : public NeuralStream {
public:
    StreamImpl(std::unique_ptr<Model> model, int block, std::size_t audioIndex, std::size_t condIndex, std::vector<std::pair<std::size_t, std::size_t>> stateMap,
               std::size_t audioOutIndex)
        : model_(std::move(model)), block_(block), audioIndex_(audioIndex), condIndex_(condIndex), stateMap_(std::move(stateMap)), audioOut_(audioOutIndex),
          in_(1 << 15), cond_(1 << 15), out_(1 << 15) {
        // two blocks of silence are the latency: the worker always has time to answer
        const std::vector<float> zeros(static_cast<std::size_t>(2 * block), 0.0f);
        out_.push(zeros.data(), zeros.size());
        const std::size_t inputs = model_->inputNames().size();
        feed_.resize(inputs);
        for (std::size_t i = 0; i < inputs; ++i) {
            Tensor t;
            t.shape = model_->inputShape(i);
            std::int64_t count = 1;
            for (std::int64_t& d : t.shape) {
                if (d <= 0) d = 1;
                count *= d;
            }
            t.data.assign(static_cast<std::size_t>(count), 0.0f);
            feed_[i] = std::move(t);
        }
        worker_ = std::thread([this] { run(); });
    }
    ~StreamImpl() override {
        stop_.store(true);
        if (worker_.joinable()) worker_.join();
    }
    int block() const override { return block_; }
    int latency() const override { return 2 * block_; }
    long underruns() const override { return underruns_.load(); }
    void process(const float* in, const float* cond, float* out, int n) noexcept override {
        const std::size_t count = static_cast<std::size_t>(n);
        in_.push(in, count);
        cond_.push(cond, count);
        // the answer for these n samples is the audio from two blocks ago: normally it is there already
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(40);
        while (out_.available() < count && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        if (!out_.pop(out, count)) {
            std::memset(out, 0, count * sizeof(float));
            underruns_.fetch_add(1);
        }
    }

private:
    void run() {
        std::vector<float> a(static_cast<std::size_t>(block_)), c(static_cast<std::size_t>(block_));
        while (!stop_.load()) {
            if (in_.available() < static_cast<std::size_t>(block_) || cond_.available() < static_cast<std::size_t>(block_) || out_.space() < static_cast<std::size_t>(block_)) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            in_.pop(a.data(), a.size());
            cond_.pop(c.data(), c.size());
            feed_[audioIndex_].data = a;
            feed_[condIndex_].data = c;
            std::vector<Tensor> res;
            try {
                res = model_->run(feed_);
            } catch (const std::exception&) {
                res.clear();
            }
            if (res.size() <= audioOut_ || res[audioOut_].data.size() < a.size()) {
                std::fill(a.begin(), a.end(), 0.0f);
                out_.push(a.data(), a.size());
                continue;
            }
            out_.push(res[audioOut_].data.data(), a.size());
            for (const auto& [inIndex, outIndex] : stateMap_)
                if (outIndex < res.size() && res[outIndex].data.size() == feed_[inIndex].data.size()) feed_[inIndex].data = std::move(res[outIndex].data);
        }
    }
    std::unique_ptr<Model> model_;
    int block_;
    std::size_t audioIndex_, condIndex_;
    std::vector<std::pair<std::size_t, std::size_t>> stateMap_;  // (input index of a state, output index that carries its next value)
    std::size_t audioOut_;
    FloatRing in_, cond_, out_;
    std::vector<Tensor> feed_;
    std::atomic<bool> stop_{false};
    std::atomic<long> underruns_{0};
    std::thread worker_;
};

#endif

}  // namespace

std::unique_ptr<NeuralStream> NeuralStream::create(const std::filesystem::path& file, int block) {
#ifdef LPC_WITH_ONNX
    std::unique_ptr<Model> model = Model::load(file);
    if (!model || block < 1) return nullptr;
    const std::vector<std::string> in = model->inputNames(), out = model->outputNames();
    std::size_t audioIndex = in.size(), condIndex = in.size(), audioOut = out.size();
    std::vector<std::pair<std::size_t, std::size_t>> states;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == "audio") audioIndex = i;
        else if (in[i] == "cond") condIndex = i;
        else {
            const std::string suffix = "_in_state";
            if (in[i].size() <= suffix.size() || in[i].compare(in[i].size() - suffix.size(), suffix.size(), suffix) != 0) return nullptr;
            const std::string next = in[i].substr(0, in[i].size() - suffix.size()) + "_out_state";
            std::size_t j = 0;
            while (j < out.size() && out[j] != next) ++j;
            if (j == out.size()) return nullptr;
            states.emplace_back(i, j);
        }
    }
    for (std::size_t j = 0; j < out.size(); ++j)
        if (out[j].find("_out_state") == std::string::npos) audioOut = j;
    if (audioIndex == in.size() || condIndex == in.size() || audioOut == out.size()) return nullptr;
    const std::vector<std::int64_t> shape = model->inputShape(audioIndex);
    if (shape.empty() || shape.back() != block) return nullptr;
    return std::make_unique<StreamImpl>(std::move(model), block, audioIndex, condIndex, std::move(states), audioOut);
#else
    (void)file;
    (void)block;
    return nullptr;
#endif
}

std::filesystem::path findFamilyModel(const std::string& family, double sampleRate, int block) {
    std::error_code ec;
    const std::filesystem::path dir = modelDirectory();
    if (!std::filesystem::is_directory(dir, ec)) return {};
    // the rate classes in order of preference: the nearest first (MADSP: below 46 kHz the 44 model, below 70 the 48, below 92 the 88, else the 96)
    const int wanted = sampleRate < 46000.0 ? 44 : sampleRate < 70000.0 ? 48 : sampleRate < 92000.0 ? 88 : 96;
    std::vector<int> order{wanted};
    for (const int r : {96, 88, 48, 44})
        if (r != wanted) order.push_back(r);
    const std::string tail = ".model_" + std::to_string(block) + "_transformed.onnx";
    for (const int rate : order) {
        const std::string prefix = family + "_" + std::to_string(rate) + "_";
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            const std::string name = e.path().filename().string();
            if (name.size() > tail.size() && name.compare(0, prefix.size(), prefix) == 0 && name.compare(name.size() - tail.size(), tail.size(), tail) == 0) return e.path();
        }
    }
    return {};
}

}  // namespace lpc::ml
