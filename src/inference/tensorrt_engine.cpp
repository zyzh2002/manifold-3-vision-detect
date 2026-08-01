#include "inference/tensorrt_engine.h"

#include <NvInfer.h>
#include <cuda_runtime.h>

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "inference/synthetic_engine_contract.h"

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

std::string DimsToString(int32_t nbDims, const int32_t *dims) {
    std::string s = "[";
    for (int32_t i = 0; i < nbDims; ++i) {
        if (i > 0) {
            s += ",";
        }
        s += std::to_string(dims[i]);
    }
    s += "]";
    return s;
}

std::string DimsToString(const Dims &dims) {
    return DimsToString(dims.nbDims, dims.d);
}

struct ExpectedTensor {
    const char *name;
    TensorIOMode mode;
    int32_t nbDims;
    const int32_t *dims;
};

constexpr int32_t kInputDims[] = {1, kSyntheticInputChannels, kSyntheticInputHeight, kSyntheticInputWidth};
constexpr int32_t kPredictionDims[] = {1, kSyntheticPredictionChannels, kSyntheticAnchors};
constexpr int32_t kMaskCoefficientsDims[] = {1, kSyntheticMaskCoefficientChannels, kSyntheticAnchors};
constexpr int32_t kPrototypeDims[] = {1, kSyntheticMaskCoefficientChannels, kSyntheticPrototypeHeight,
                                      kSyntheticPrototypeWidth};

const ExpectedTensor kExpectedTensors[] = {
    {kSyntheticInputName, TensorIOMode::kINPUT, 4, kInputDims},
    {kSyntheticPredictionName, TensorIOMode::kOUTPUT, 3, kPredictionDims},
    {kSyntheticMaskCoefficientsName, TensorIOMode::kOUTPUT, 3, kMaskCoefficientsDims},
    {kSyntheticPrototypeName, TensorIOMode::kOUTPUT, 4, kPrototypeDims},
};

constexpr size_t kNumExpectedTensors = sizeof(kExpectedTensors) / sizeof(kExpectedTensors[0]);

size_t NumElements(const int32_t *dims, int32_t nbDims) {
    size_t n = 1;
    for (int32_t i = 0; i < nbDims; ++i) {
        n *= static_cast<size_t>(dims[i]);
    }
    return n;
}

const char *DataTypeToString(DataType dtype) {
    switch (dtype) {
    case DataType::kFLOAT:
        return "kFLOAT";
    case DataType::kHALF:
        return "kHALF";
    case DataType::kINT8:
        return "kINT8";
    case DataType::kINT32:
        return "kINT32";
    case DataType::kBOOL:
        return "kBOOL";
    default:
        return "unknown";
    }
}

} // namespace

TensorRtEngine::~TensorRtEngine() {
    Release();
}

void TensorRtEngine::Release() {
    delete static_cast<IExecutionContext *>(context_);
    context_ = nullptr;
    delete static_cast<ICudaEngine *>(engine_);
    engine_ = nullptr;
    delete static_cast<IRuntime *>(runtime_);
    runtime_ = nullptr;
    engine_data_.clear();
    if (stream_ != nullptr) {
        cudaStreamDestroy(static_cast<cudaStream_t>(stream_));
        stream_ = nullptr;
    }
    cudaFree(input_buffer_);
    input_buffer_ = nullptr;
    cudaFree(prediction_buffer_);
    prediction_buffer_ = nullptr;
    cudaFree(mask_coefficients_buffer_);
    mask_coefficients_buffer_ = nullptr;
    cudaFree(prototype_buffer_);
    prototype_buffer_ = nullptr;
    if (event_start_ != nullptr) {
        cudaEventDestroy(static_cast<cudaEvent_t>(event_start_));
        event_start_ = nullptr;
    }
    if (event_h2d_ != nullptr) {
        cudaEventDestroy(static_cast<cudaEvent_t>(event_h2d_));
        event_h2d_ = nullptr;
    }
    if (event_execute_ != nullptr) {
        cudaEventDestroy(static_cast<cudaEvent_t>(event_execute_));
        event_execute_ = nullptr;
    }
    if (event_d2h_ != nullptr) {
        cudaEventDestroy(static_cast<cudaEvent_t>(event_d2h_));
        event_d2h_ = nullptr;
    }
    loaded_ = false;
}

bool TensorRtEngine::Load(const std::string &engine_path) {
    if (loaded_) {
        return true;
    }
    auto Fail = [this](const std::string &msg) {
        std::fprintf(stderr, "TensorRtEngine: %s\n", msg.c_str());
        Release();
        return false;
    };

    std::ifstream f(engine_path, std::ios::binary | std::ios::ate);
    if (!f) {
        return Fail("failed to open engine file " + engine_path);
    }
    const std::streamsize size = f.tellg();
    if (size <= 0) {
        return Fail("engine file is empty: " + engine_path);
    }
    f.seekg(0, std::ios::beg);
    engine_data_.resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char *>(engine_data_.data()), size);
    if (!f) {
        return Fail("failed to read engine file: " + engine_path);
    }

    runtime_ = createInferRuntime(g_logger);
    if (runtime_ == nullptr) {
        return Fail("createInferRuntime failed");
    }
    engine_ = static_cast<IRuntime *>(runtime_)->deserializeCudaEngine(engine_data_.data(), engine_data_.size());
    if (engine_ == nullptr) {
        return Fail("deserializeCudaEngine failed");
    }

    ICudaEngine *engine = static_cast<ICudaEngine *>(engine_);
    std::vector<std::string> names;
    names.reserve(static_cast<size_t>(engine->getNbIOTensors()));
    for (int32_t i = 0; i < engine->getNbIOTensors(); ++i) {
        const char *name = engine->getIOTensorName(i);
        if (name == nullptr || name[0] == '\0') {
            return Fail("engine contains an unnamed IO tensor");
        }
        names.emplace_back(name);
    }
    if (names.size() != kNumExpectedTensors) {
        return Fail("expected exactly " + std::to_string(kNumExpectedTensors) +
                    " IO tensors (images, output0, output1, output2), got " + std::to_string(names.size()));
    }

    for (const ExpectedTensor &expected : kExpectedTensors) {
        bool found = false;
        for (const std::string &name : names) {
            if (name == expected.name) {
                found = true;
                break;
            }
        }
        if (!found) {
            return Fail(std::string("missing required tensor '") + expected.name + "'");
        }
        const std::string expectedDims = DimsToString(expected.nbDims, expected.dims);
        if (engine->getTensorIOMode(expected.name) != expected.mode) {
            return Fail(std::string("tensor '") + expected.name + "' has wrong IO mode");
        }
        const DataType dtype = engine->getTensorDataType(expected.name);
        if (dtype != DataType::kFLOAT) {
            return Fail(std::string("tensor '") + expected.name + "' dtype is " + DataTypeToString(dtype) +
                        ", expected kFLOAT");
        }
        const Dims actual = engine->getTensorShape(expected.name);
        if (actual.nbDims != expected.nbDims) {
            return Fail("tensor '" + std::string(expected.name) + "' rank mismatch: expected " + expectedDims +
                        ", got " + DimsToString(actual));
        }
        for (int32_t d = 0; d < actual.nbDims; ++d) {
            if (actual.d[d] != expected.dims[d]) {
                return Fail("tensor '" + std::string(expected.name) + "' dim " + std::to_string(d) +
                            " mismatch: expected " + expectedDims + ", got " + DimsToString(actual));
            }
        }
    }

    context_ = engine->createExecutionContext();
    if (context_ == nullptr) {
        return Fail("createExecutionContext failed");
    }

    cudaStream_t stream = nullptr;
    if (cudaStreamCreate(&stream) != cudaSuccess) {
        return Fail("cudaStreamCreate failed");
    }
    stream_ = stream;

    const size_t inputBytes = NumElements(kInputDims, 4) * sizeof(float);
    if (cudaMalloc(&input_buffer_, inputBytes) != cudaSuccess) {
        return Fail("cudaMalloc input buffer failed");
    }
    const size_t predictionBytes = NumElements(kPredictionDims, 3) * sizeof(float);
    if (cudaMalloc(&prediction_buffer_, predictionBytes) != cudaSuccess) {
        return Fail("cudaMalloc prediction buffer failed");
    }
    const size_t maskCoefficientsBytes = NumElements(kMaskCoefficientsDims, 3) * sizeof(float);
    if (cudaMalloc(&mask_coefficients_buffer_, maskCoefficientsBytes) != cudaSuccess) {
        return Fail("cudaMalloc mask coefficients buffer failed");
    }
    const size_t prototypeBytes = NumElements(kPrototypeDims, 4) * sizeof(float);
    if (cudaMalloc(&prototype_buffer_, prototypeBytes) != cudaSuccess) {
        return Fail("cudaMalloc prototype buffer failed");
    }

    cudaEvent_t events[4] = {nullptr, nullptr, nullptr, nullptr};
    for (int32_t i = 0; i < 4; ++i) {
        if (cudaEventCreate(&events[i]) != cudaSuccess) {
            return Fail("cudaEventCreate failed");
        }
    }
    event_start_ = events[0];
    event_h2d_ = events[1];
    event_execute_ = events[2];
    event_d2h_ = events[3];

    IExecutionContext *context = static_cast<IExecutionContext *>(context_);
    if (!context->setTensorAddress(kSyntheticInputName, input_buffer_)) {
        return Fail(std::string("setTensorAddress failed for '") + kSyntheticInputName + "'");
    }
    if (!context->setTensorAddress(kSyntheticPredictionName, prediction_buffer_)) {
        return Fail(std::string("setTensorAddress failed for '") + kSyntheticPredictionName + "'");
    }
    if (!context->setTensorAddress(kSyntheticMaskCoefficientsName, mask_coefficients_buffer_)) {
        return Fail(std::string("setTensorAddress failed for '") + kSyntheticMaskCoefficientsName + "'");
    }
    if (!context->setTensorAddress(kSyntheticPrototypeName, prototype_buffer_)) {
        return Fail(std::string("setTensorAddress failed for '") + kSyntheticPrototypeName + "'");
    }

    loaded_ = true;
    return true;
}

bool TensorRtEngine::Infer(const std::vector<float> &input, SyntheticOutputs *outputs, EngineTiming *timing) {
    if (!loaded_) {
        std::fprintf(stderr, "TensorRtEngine::Infer called before Load\n");
        return false;
    }
    if (outputs == nullptr || timing == nullptr) {
        std::fprintf(stderr, "TensorRtEngine::Infer requires non-null outputs and timing\n");
        return false;
    }
    constexpr size_t kInputFloats =
        static_cast<size_t>(kSyntheticInputChannels) * kSyntheticInputHeight * kSyntheticInputWidth;
    if (input.size() != kInputFloats) {
        std::fprintf(stderr, "TensorRtEngine::Infer input size %zu, expected %zu\n", input.size(), kInputFloats);
        return false;
    }

    cudaStream_t stream = static_cast<cudaStream_t>(stream_);
    IExecutionContext *context = static_cast<IExecutionContext *>(context_);
    cudaEvent_t eventStart = static_cast<cudaEvent_t>(event_start_);
    cudaEvent_t eventH2d = static_cast<cudaEvent_t>(event_h2d_);
    cudaEvent_t eventExecute = static_cast<cudaEvent_t>(event_execute_);
    cudaEvent_t eventD2h = static_cast<cudaEvent_t>(event_d2h_);

    outputs->prediction.resize(NumElements(kPredictionDims, 3));
    outputs->mask_coefficients.resize(NumElements(kMaskCoefficientsDims, 3));
    outputs->prototype.resize(NumElements(kPrototypeDims, 4));

    const size_t inputBytes = kInputFloats * sizeof(float);
    const size_t predictionBytes = NumElements(kPredictionDims, 3) * sizeof(float);
    const size_t maskCoefficientsBytes = NumElements(kMaskCoefficientsDims, 3) * sizeof(float);
    const size_t prototypeBytes = NumElements(kPrototypeDims, 4) * sizeof(float);

    if (cudaEventRecord(eventStart, stream) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine::Infer cudaEventRecord start failed\n");
        return false;
    }
    if (cudaMemcpyAsync(input_buffer_, input.data(), inputBytes, cudaMemcpyHostToDevice, stream) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine::Infer cudaMemcpyAsync H2D failed\n");
        return false;
    }
    if (cudaEventRecord(eventH2d, stream) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine::Infer cudaEventRecord h2d failed\n");
        return false;
    }
    if (!context->enqueueV3(stream)) {
        std::fprintf(stderr, "TensorRtEngine::Infer enqueueV3 failed\n");
        return false;
    }
    if (cudaEventRecord(eventExecute, stream) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine::Infer cudaEventRecord execute failed\n");
        return false;
    }
    if (cudaMemcpyAsync(outputs->prediction.data(), prediction_buffer_, predictionBytes, cudaMemcpyDeviceToHost,
                        stream) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine::Infer cudaMemcpyAsync D2H prediction failed\n");
        return false;
    }
    if (cudaMemcpyAsync(outputs->mask_coefficients.data(), mask_coefficients_buffer_, maskCoefficientsBytes,
                        cudaMemcpyDeviceToHost, stream) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine::Infer cudaMemcpyAsync D2H mask coefficients failed\n");
        return false;
    }
    if (cudaMemcpyAsync(outputs->prototype.data(), prototype_buffer_, prototypeBytes, cudaMemcpyDeviceToHost, stream) !=
        cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine::Infer cudaMemcpyAsync D2H prototype failed\n");
        return false;
    }
    if (cudaEventRecord(eventD2h, stream) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine::Infer cudaEventRecord d2h failed\n");
        return false;
    }
    if (cudaStreamSynchronize(stream) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine::Infer cudaStreamSynchronize failed\n");
        return false;
    }

    float ms = 0.0f;
    if (cudaEventElapsedTime(&ms, eventStart, eventH2d) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine::Infer cudaEventElapsedTime h2d failed\n");
        return false;
    }
    timing->host_to_device_us = static_cast<int64_t>(ms * 1000.0f);
    if (cudaEventElapsedTime(&ms, eventH2d, eventExecute) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine::Infer cudaEventElapsedTime execute failed\n");
        return false;
    }
    timing->execute_us = static_cast<int64_t>(ms * 1000.0f);
    if (cudaEventElapsedTime(&ms, eventExecute, eventD2h) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine::Infer cudaEventElapsedTime d2h failed\n");
        return false;
    }
    timing->device_to_host_us = static_cast<int64_t>(ms * 1000.0f);
    if (cudaEventElapsedTime(&ms, eventStart, eventD2h) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine::Infer cudaEventElapsedTime total failed\n");
        return false;
    }
    timing->total_us = static_cast<int64_t>(ms * 1000.0f);
    return true;
}

} // namespace inference
} // namespace manifold3
