#pragma once

#include <cstdint>
#include <vector>

namespace manifold3 {
namespace inference {

// Per-window latency sample set with nearest-rank percentiles.
// Samples are kept in insertion order; Add() is O(1). Percentile queries
// sort a copy of the samples (nearest-rank requires ordered data), so
// querying is O(N log N) but window sizes are small.
class LatencySamples {
  public:
    void Add(int64_t value_us);
    // Empty window: returns 0.
    int64_t average_us() const;
    // Nearest-rank percentile: index = ceil(p * N) - 1, clamped.
    // p in (0.0, 1.0]; empty window returns 0.
    int64_t percentile_us(double percentile) const;
    // Empty window: returns 0.
    int64_t max_us() const;
    void Clear();

  private:
    std::vector<int64_t> values_us_;
};

struct PipelineWindowStats {
    uint64_t frames = 0;
    uint64_t detections = 0;

    LatencySamples preprocess_us;
    LatencySamples host_to_device_us;
    LatencySamples execute_us;
    LatencySamples device_to_host_us;
    LatencySamples engine_total_us;
    LatencySamples postprocess_us;
    LatencySamples end_to_end_us;

    void Clear();
};

} // namespace inference
} // namespace manifold3
