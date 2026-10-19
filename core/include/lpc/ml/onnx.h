#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace lpc::ml {

// Neural models through onnxruntime. The models are Logic Pro's own Core ML networks converted to ONNX (tools/ml, docs/logic-reference/ml-conversion.md)
// and live in a folder: $LPC_MODELS_DIR, or `models` next to the executable. Without onnxruntime at build time (LPC_WITH_ONNX off) nothing here
// is available and every load returns null: the callers keep their classical algorithms.
struct Tensor {
    std::vector<std::int64_t> shape;
    std::vector<float> data;
};

bool available();                                        // the build has onnxruntime
void setModelDirectory(const std::filesystem::path& dir);  // overrides the default folder
std::filesystem::path modelDirectory();
std::filesystem::path modelPath(const std::string& file);  // modelDirectory() / file

class Model {
public:
    virtual ~Model() = default;
    // null when onnxruntime is not built in, the file is missing or cannot be read
    static std::unique_ptr<Model> load(const std::filesystem::path& file);
    virtual std::vector<std::string> inputNames() const = 0;
    virtual std::vector<std::string> outputNames() const = 0;
    virtual std::vector<std::int64_t> inputShape(std::size_t input) const = 0;
    // One tensor per input in the order of inputNames(); the outputs come in the order of outputNames(). Not thread-safe per Model.
    virtual std::vector<Tensor> run(const std::vector<Tensor>& inputs) = 0;
};

}  // namespace lpc::ml
