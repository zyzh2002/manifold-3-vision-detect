#include "inference/tensorrt_engine.h"

#include <NvInfer.h>
#include <cuda_runtime.h>

#include <chrono>
#include <cstdio>
#include <fstream>

namespace manifold3 {
namespace inference {

namespace {

using namespace nvinfer1;

class Logger : public ILogger {
  public:
    void log(Severity severity, const char *msg) noexcept override {
        if (severity <= Severity::kWARNING) {
            std::fprintf(stderr, "[TRT] %s\n", msg);
        }
    }
};

Logger g_logger;

std::vector<uint8_t> ReadFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return {};
    }
    const std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char *>(data.data()), size);
    return data;
}

}  // namespace

TensorRtEngine::~TensorRtEngine() {
    if (context_ != nullptr) {
        static_cast<IExecutionContext *>(context_)->destroy();
    }
    if (engine_ != nullptr) {
        static_cast<ICudaEngine *>(engine_)->destroy();
    }
    if (runtime_ != nullptr) {
        static_cast<IRuntime *>(runtime_)->destroy();
    }
}

bool TensorRtEngine::Load(const std::string &engine_path) {
    if (loaded_) {
        return true;
    }
    engine_data_ = ReadFile(engine_path);
    if (engine_data_.empty()) {
        std::fprintf(stderr, "TensorRtEngine: failed to read engine file %s\n", engine_path.c_str());
        return false;
    }
    runtime_ = createInferRuntime(g_logger);
    if (runtime_ == nullptr) {
        std::fprintf(stderr, "TensorRtEngine: createInferRuntime failed\n");
        return false;
    }
    engine_ = static_cast<IRuntime *>(runtime_)->deserializeCudaEngine(engine_data_.data(), engine_data_.size());
    if (engine_ == nullptr) {
        std::fprintf(stderr, "TensorRtEngine: deserializeCudaEngine failed\n");
        return false;
    }
    context_ = static_cast<ICudaEngine *>(engine_)->createExecutionContext();
    if (context_ == nullptr) {
        std::fprintf(stderr, "TensorRtEngine: createExecutionContext failed\n");
        return false;
    }
    loaded_ = true;
    return true;
}

bool TensorRtEngine::Infer(const std::vector<float> &input, std::vector<float> *out0, std::vector<float> *out1,
                           std::vector<float> *out2, int64_t *latency_us) {
    if (!loaded_) {
        std::fprintf(stderr, "TensorRtEngine::Infer called before Load\n");
        return false;
    }
    ICudaEngine *engine = static_cast<ICudaEngine *>(engine_);
    IExecutionContext *context = static_cast<IExecutionContext *>(context_);

    const char *inputName = nullptr;
    std::vector<const char *> outputNames;
    for (int32_t i = 0; i < engine->getNbIOTensors(); ++i) {
        const char *name = engine->getIOTensorName(i);
        if (engine->getTensorIOMode(name) == TensorIOMode::kINPUT) {
            inputName = name;
        } else {
            outputNames.push_back(name);
        }
    }
    if (inputName == nullptr || outputNames.size() != 3) {
        std::fprintf(stderr, "TensorRtEngine: expected 1 input and 3 outputs, got %zu\n", outputNames.size());
        return false;
    }

    const Dims inDims = engine->getTensorShape(inputName);
    int64_t inSize = 1;
    for (int32_t d = 0; d < inDims.nbDims; ++d) {
        inSize *= inDims.d[d];
    }
    if (static_cast<int64_t>(input.size()) != inSize) {
        std::fprintf(stderr, "TensorRtEngine: input size %zu != engine input %lld\n", input.size(),
                     static_cast<long long>(inSize));
        return false;
    }

    std::vector<std::vector<float> *> outs = {out0, out1, out2};
    std::vector<void *> deviceBufs(1 + outputNames.size(), nullptr);
    const int64_t inBytes = inSize * sizeof(float);
    if (cudaMalloc(&deviceBufs[0], inBytes) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine: cudaMalloc input failed\n");
        return false;
    }
    for (size_t i = 0; i < outputNames.size(); ++i) {
        const Dims dims = engine->getTensorShape(outputNames[i]);
        int64_t size = 1;
        for (int32_t d = 0; d < dims.nbDims; ++d) {
            size *= dims.d[d];
        }
        outs[i]->resize(static_cast<size_t>(size));
        if (cudaMalloc(&deviceBufs[1 + i], size * sizeof(float)) != cudaSuccess) {
            std::fprintf(stderr, "TensorRtEngine: cudaMalloc output %zu failed\n", i);
            for (void *p : deviceBufs) {
                if (p != nullptr) {
                    cudaFree(p);
                }
            }
            return false;
        }
    }

    cudaStream_t stream = nullptr;
    cudaStreamCreate(&stream);
    cudaMemcpyAsync(deviceBufs[0], input.data(), inBytes, cudaMemcpyHostToDevice, stream);
    for (size_t i = 0; i < outputNames.size(); ++i) {
        context->setTensorAddress(outputNames[i], deviceBufs[1 + i]);
    }
    context->setTensorAddress(inputName, deviceBufs[0]);

    const auto start = std::chrono::steady_clock::now();
    const bool enqueued = context->enqueueV3(stream);
    cudaStreamSynchronize(stream);
    const auto end = std::chrono::steady_clock::now();
    if (latency_us != nullptr) {
        *latency_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }

    for (size_t i = 0; i < outputNames.size(); ++i) {
        const Dims dims = engine->getTensorShape(outputNames[i]);
        int64_t size = 1;
        for (int32_t d = 0; d < dims.nbDims; ++d) {
            size *= dims.d[d];
        }
        cudaMemcpyAsync(outs[i]->data(), deviceBufs[1 + i], size * sizeof(float), cudaMemcpyDeviceToHost, stream);
    }
    cudaStreamSynchronize(stream);
    cudaStreamDestroy(stream);
    for (void *p : deviceBufs) {
        if (p != nullptr) {
            cudaFree(p);
        }
    }
    if (!enqueued) {
        std::fprintf(stderr, "TensorRtEngine: enqueueV3 failed\n");
        return false;
    }
    return true;
}

}  // namespace inference
}  // namespace manifold3
