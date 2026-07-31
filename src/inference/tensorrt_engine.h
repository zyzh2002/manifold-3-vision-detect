#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace manifold3 {
namespace inference {

// RAII wrapper around the TensorRT runtime, engine, and execution context.
// Serialized engines are device-bound; load only engines converted on the
// same TensorRT version and GPU.
class TensorRtEngine {
  public:
    TensorRtEngine() = default;
    ~TensorRtEngine();

    TensorRtEngine(const TensorRtEngine &) = delete;
    TensorRtEngine &operator=(const TensorRtEngine &) = delete;

    bool Load(const std::string &engine_path);

    // Runs inference on one NCHW float input into the YOLO11-seg outputs.
    // Output vectors are resized to the engine's tensor shapes.
    bool Infer(const std::vector<float> &input, std::vector<float> *out0, std::vector<float> *out1,
               std::vector<float> *out2, int64_t *latency_us = nullptr);

  private:
    void *runtime_ = nullptr;
    void *engine_ = nullptr;
    void *context_ = nullptr;
    std::vector<uint8_t> engine_data_;
    bool loaded_ = false;
};

}  // namespace inference
}  // namespace manifold3
