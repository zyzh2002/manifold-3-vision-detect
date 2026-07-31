#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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
    std::vector<float> input(1 * 3 * 1280 * 1280, 0.5f);
    std::vector<float> out0, out1, out2;
    int64_t latencyUs = 0;
    if (!engine.Infer(input, &out0, &out1, &out2, &latencyUs)) {
        return 1;
    }
    std::printf("inference smoke PASS: out0=%zu out1=%zu out2=%zu latency_us=%lld\n", out0.size(), out1.size(),
                out2.size(), static_cast<long long>(latencyUs));
    return 0;
}
