#include "inference/pipeline_metrics.h"

#include <algorithm>
#include <cmath>

namespace manifold3 {
namespace inference {

void LatencySamples::Add(int64_t value_us) {
    values_us_.push_back(value_us);
}

int64_t LatencySamples::average_us() const {
    if (values_us_.empty()) {
        return 0;
    }
    int64_t sum = 0;
    for (const int64_t value_us : values_us_) {
        sum += value_us;
    }
    return sum / static_cast<int64_t>(values_us_.size());
}

int64_t LatencySamples::percentile_us(double percentile) const {
    if (values_us_.empty()) {
        return 0;
    }
    std::vector<int64_t> sorted = values_us_;
    std::sort(sorted.begin(), sorted.end());
    // Nearest-rank: rank = ceil(p * N) with p in (0.0, 1.0]; rank is at
    // least 1, so index = rank - 1 is always valid after clamping.
    const double rankD = std::ceil(percentile * static_cast<double>(sorted.size()));
    const size_t rank = static_cast<size_t>(rankD);
    const size_t index = std::min(rank, sorted.size()) - 1;
    return sorted[index];
}

int64_t LatencySamples::max_us() const {
    if (values_us_.empty()) {
        return 0;
    }
    return *std::max_element(values_us_.begin(), values_us_.end());
}

void LatencySamples::Clear() {
    values_us_.clear();
}

void PipelineWindowStats::Clear() {
    frames = 0;
    detections = 0;
    preprocess_us.Clear();
    host_to_device_us.Clear();
    execute_us.Clear();
    device_to_host_us.Clear();
    engine_total_us.Clear();
    postprocess_us.Clear();
    end_to_end_us.Clear();
}

} // namespace inference
} // namespace manifold3
