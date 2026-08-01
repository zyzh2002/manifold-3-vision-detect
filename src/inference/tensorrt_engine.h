#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace manifold3 {
namespace inference {

struct SyntheticOutputs {
    std::vector<float> prediction;
    std::vector<float> mask_coefficients;
    std::vector<float> prototype;
};

struct EngineTiming {
    int64_t host_to_device_us = 0;
    int64_t execute_us = 0;
    int64_t device_to_host_us = 0;
    int64_t total_us = 0;
};

// RAII wrapper around the TensorRT runtime, engine, context, CUDA stream,
// and persistent device buffers. Accepts ONLY the synthetic three-output
// ABI (see synthetic_engine_contract.h): inputs/outputs are matched BY NAME
// and validated for mode, dtype, rank, and exact shape at Load time.
class TensorRtEngine {
  public:
    TensorRtEngine() = default;
    ~TensorRtEngine();

    TensorRtEngine(const TensorRtEngine &) = delete;
    TensorRtEngine &operator=(const TensorRtEngine &) = delete;

    // Loads and validates a serialized synthetic engine. Returns false on any
    // contract or CUDA failure; on failure all resources are released.
    bool Load(const std::string &engine_path);

    // Runs one inference. Uses persistent buffers; never allocates per call.
    // outputs and timing must be non-null; timing is filled with per-stage
    // CUDA event timings (h2d / execute / d2h / total).
    bool Infer(const std::vector<float> &input, SyntheticOutputs *outputs, EngineTiming *timing);

  private:
    void Release();

    void *runtime_ = nullptr;  // nvinfer1::IRuntime*
    void *engine_ = nullptr;   // nvinfer1::ICudaEngine*
    void *context_ = nullptr;  // nvinfer1::IExecutionContext*
    void *stream_ = nullptr;   // cudaStream_t
    void *input_buffer_ = nullptr;
    void *prediction_buffer_ = nullptr;
    void *mask_coefficients_buffer_ = nullptr;
    void *prototype_buffer_ = nullptr;
    void *event_start_ = nullptr;    // cudaEvent_t: before H2D copy
    void *event_h2d_ = nullptr;      // cudaEvent_t: after H2D copy
    void *event_execute_ = nullptr;  // cudaEvent_t: after enqueueV3
    void *event_d2h_ = nullptr;      // cudaEvent_t: after D2H copies
    std::vector<uint8_t> engine_data_;
    bool loaded_ = false;
};

} // namespace inference
} // namespace manifold3
