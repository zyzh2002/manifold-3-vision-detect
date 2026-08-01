#include <cassert>
#include <cstdint>

#include "inference/pipeline_metrics.h"

using manifold3::inference::LatencySamples;
using manifold3::inference::PipelineWindowStats;

int main() {
    // Empty window: all aggregates are 0.
    {
        LatencySamples s;
        assert(s.average_us() == 0);
        assert(s.percentile_us(0.95) == 0);
        assert(s.max_us() == 0);
    }

    // Single sample: avg == p95 == max == value.
    {
        LatencySamples s;
        s.Add(42);
        assert(s.average_us() == 42);
        assert(s.percentile_us(0.95) == 42);
        assert(s.percentile_us(1.0) == 42);
        assert(s.max_us() == 42);
    }

    // Two samples {100, 200}: nearest-rank p95 rank = ceil(0.95*2) = 2,
    // index = 2-1 = 1 -> sorted[1] = 200; avg = 150; max = 200.
    {
        LatencySamples s;
        s.Add(100);
        s.Add(200);
        assert(s.average_us() == 150);
        assert(s.percentile_us(0.95) == 200);
        assert(s.max_us() == 200);
    }

    // Unsorted insertion order must not change the percentile: the samples
    // are sorted before the nearest-rank lookup.
    {
        LatencySamples s;
        s.Add(200);
        s.Add(100);
        s.Add(300);
        assert(s.average_us() == 200);
        assert(s.percentile_us(0.95) == 300);
        assert(s.max_us() == 300);
    }

    // 20 samples 1..20: p95 rank = ceil(0.95*20) = 19, index = 18 ->
    // sorted[18] = 19 (nearest-rank convention; strict percentile would
    // interpolate to 19.05).
    {
        LatencySamples s;
        for (int64_t v = 1; v <= 20; ++v) {
            s.Add(v);
        }
        assert(s.average_us() == 10);
        assert(s.percentile_us(0.95) == 19);
        assert(s.max_us() == 20);
    }

    // Out-of-range percentile: p <= 0 (and NaN) is rejected by the guard and
    // returns 0 instead of underflowing the nearest-rank index.
    {
        LatencySamples s;
        s.Add(100);
        s.Add(200);
        assert(s.percentile_us(0.0) == 0);
        assert(s.percentile_us(-0.5) == 0);
        const double nan = 0.0 / 0.0;
        assert(s.percentile_us(nan) == 0);
    }

    // Clear empties the sample set.
    {
        LatencySamples s;
        s.Add(7);
        s.Clear();
        assert(s.average_us() == 0);
        assert(s.percentile_us(0.95) == 0);
        assert(s.max_us() == 0);
    }

    // PipelineWindowStats::Clear zeroes counters and every latency set.
    {
        PipelineWindowStats w;
        w.frames = 12;
        w.detections = 34;
        w.preprocess_us.Add(1);
        w.host_to_device_us.Add(2);
        w.execute_us.Add(3);
        w.device_to_host_us.Add(4);
        w.engine_total_us.Add(5);
        w.postprocess_us.Add(6);
        w.end_to_end_us.Add(7);
        w.Clear();
        assert(w.frames == 0);
        assert(w.detections == 0);
        assert(w.preprocess_us.average_us() == 0);
        assert(w.host_to_device_us.average_us() == 0);
        assert(w.execute_us.average_us() == 0);
        assert(w.device_to_host_us.average_us() == 0);
        assert(w.engine_total_us.average_us() == 0);
        assert(w.postprocess_us.average_us() == 0);
        assert(w.end_to_end_us.average_us() == 0);
    }

    // Stage isolation: samples added to one stage never leak into another.
    {
        PipelineWindowStats w;
        w.preprocess_us.Add(100);
        w.execute_us.Add(200);
        assert(w.preprocess_us.average_us() == 100);
        assert(w.execute_us.average_us() == 200);
        assert(w.host_to_device_us.average_us() == 0);
        assert(w.device_to_host_us.average_us() == 0);
        assert(w.engine_total_us.average_us() == 0);
        assert(w.postprocess_us.average_us() == 0);
        assert(w.end_to_end_us.average_us() == 0);
    }

    return 0;
}
