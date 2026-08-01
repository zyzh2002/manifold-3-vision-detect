#include <cstdio>
#include <string>
#include <vector>

#include "inference/synthetic_engine_contract.h"
#include "inference/tensorrt_engine.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: inference_smoke <engine.engine>\n");
        return 2;
    }
    manifold3::inference::TensorRtEngine engine;
    if (!engine.Load(argv[1])) {
        return 1;
    }
    std::vector<float> input(static_cast<size_t>(manifold3::inference::kSyntheticInputChannels) *
                                 manifold3::inference::kSyntheticInputHeight *
                                 manifold3::inference::kSyntheticInputWidth,
                             0.5f);
    manifold3::inference::SyntheticOutputs outputs;
    manifold3::inference::EngineTiming timing;
    if (!engine.Infer(input, &outputs, &timing)) {
        return 1;
    }
    std::printf("synthetic inference smoke PASS\n");
    std::printf("images=float[1,%d,%d,%d]\n", manifold3::inference::kSyntheticInputChannels,
                manifold3::inference::kSyntheticInputHeight, manifold3::inference::kSyntheticInputWidth);
    std::printf("output0=float[1,%d,%d]\n", manifold3::inference::kSyntheticPredictionChannels,
                manifold3::inference::kSyntheticAnchors);
    std::printf("output1=float[1,%d,%d]\n", manifold3::inference::kSyntheticMaskCoefficientChannels,
                manifold3::inference::kSyntheticAnchors);
    std::printf("output2=float[1,%d,%d,%d]\n", manifold3::inference::kSyntheticMaskCoefficientChannels,
                manifold3::inference::kSyntheticPrototypeHeight, manifold3::inference::kSyntheticPrototypeWidth);
    std::printf("timing_h2d_us=%lld\n", static_cast<long long>(timing.host_to_device_us));
    std::printf("timing_execute_us=%lld\n", static_cast<long long>(timing.execute_us));
    std::printf("timing_d2h_us=%lld\n", static_cast<long long>(timing.device_to_host_us));
    std::printf("timing_total_us=%lld\n", static_cast<long long>(timing.total_us));
    return 0;
}
