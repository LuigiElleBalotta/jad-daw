#include "lpc/ml/onnx.h"

#include <cstdlib>
#include <mutex>
#include <stdexcept>

#ifdef LPC_WITH_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace lpc::ml {

namespace {
std::mutex gDirMutex;
std::filesystem::path gDir;
bool gDirSet = false;
}  // namespace

void setModelDirectory(const std::filesystem::path& dir) {
    std::lock_guard<std::mutex> lock(gDirMutex);
    gDir = dir;
    gDirSet = true;
}

std::filesystem::path modelDirectory() {
    std::lock_guard<std::mutex> lock(gDirMutex);
    if (gDirSet) return gDir;
    if (const char* env = std::getenv("LPC_MODELS_DIR")) return env;  // NOLINT(concurrency-mt-unsafe)
    return std::filesystem::path("models");
}

std::filesystem::path modelPath(const std::string& file) { return modelDirectory() / file; }

#ifdef LPC_WITH_ONNX

namespace {

Ort::Env& env() {
    static Ort::Env e(ORT_LOGGING_LEVEL_ERROR, "lpc");
    return e;
}

class OrtModel final : public Model {
public:
    explicit OrtModel(const std::filesystem::path& file) {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_ = std::make_unique<Ort::Session>(env(), file.c_str(), options);
        Ort::AllocatorWithDefaultOptions alloc;
        for (std::size_t i = 0; i < session_->GetInputCount(); ++i) {
            inputs_.emplace_back(session_->GetInputNameAllocated(i, alloc).get());
            shapes_.push_back(session_->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape());
        }
        for (std::size_t i = 0; i < session_->GetOutputCount(); ++i) outputs_.emplace_back(session_->GetOutputNameAllocated(i, alloc).get());
    }
    std::vector<std::string> inputNames() const override { return inputs_; }
    std::vector<std::string> outputNames() const override { return outputs_; }
    std::vector<std::int64_t> inputShape(std::size_t input) const override { return input < shapes_.size() ? shapes_[input] : std::vector<std::int64_t>{}; }
    std::vector<Tensor> run(const std::vector<Tensor>& in) override {
        if (in.size() != inputs_.size()) throw std::runtime_error("wrong number of inputs");
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<Ort::Value> values;
        std::vector<const char*> inNames, outNames;
        for (std::size_t i = 0; i < in.size(); ++i) {
            values.push_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(in[i].data.data()), in[i].data.size(), in[i].shape.data(), in[i].shape.size()));
            inNames.push_back(inputs_[i].c_str());
        }
        for (const std::string& n : outputs_) outNames.push_back(n.c_str());
        auto out = session_->Run(Ort::RunOptions{nullptr}, inNames.data(), values.data(), values.size(), outNames.data(), outNames.size());
        std::vector<Tensor> result;
        for (Ort::Value& v : out) {
            Tensor t;
            const auto info = v.GetTensorTypeAndShapeInfo();
            t.shape = info.GetShape();
            const float* p = v.GetTensorData<float>();
            t.data.assign(p, p + info.GetElementCount());
            result.push_back(std::move(t));
        }
        return result;
    }

private:
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> inputs_, outputs_;
    std::vector<std::vector<std::int64_t>> shapes_;
};

}  // namespace

bool available() { return true; }

std::unique_ptr<Model> Model::load(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return nullptr;
    try {
        return std::make_unique<OrtModel>(file);
    } catch (const std::exception&) {
        return nullptr;
    }
}

#else

bool available() { return false; }
std::unique_ptr<Model> Model::load(const std::filesystem::path&) { return nullptr; }

#endif

}  // namespace lpc::ml
