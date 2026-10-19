#pragma once
#include <filesystem>
#include <memory>
#include <string>

namespace lpc::ml {

// One audio channel through one of Logic's conditioned neural audio models (the Saturation family: inputs `audio` and `cond` of one block each plus the
// convolution states that go from block to block; one block of processed audio comes out). The network runs on a thread of its own, so the audio thread
// only touches lock-free rings: process() hands n samples in and takes n processed ones out, delayed by latency() (two blocks) so that the worker has
// time to answer. When the worker is late the audio thread waits a few milliseconds for it (an offline render is never faster than the network) and
// then gives silence for what is missing; underruns() counts those.
class NeuralStream {
public:
    virtual ~NeuralStream() = default;
    // null when onnxruntime is not built in, or the file cannot be loaded or has not the expected inputs
    static std::unique_ptr<NeuralStream> create(const std::filesystem::path& file, int block);
    virtual int block() const = 0;
    virtual int latency() const = 0;
    virtual void process(const float* in, const float* cond, float* out, int n) noexcept = 0;
    virtual long underruns() const = 0;
};

// The file of a model family at a sample rate and block size in the model folder: "<family>_<rate>_<hash>.model_<block>_transformed.onnx". The rate is the
// nearest of 44, 48, 88 and 96 kHz that exists for the family (MADSP falls back to the best model for rates it does not know). Empty path when none.
std::filesystem::path findFamilyModel(const std::string& family, double sampleRate, int block);

}  // namespace lpc::ml
