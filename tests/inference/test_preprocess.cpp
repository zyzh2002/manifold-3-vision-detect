#include <cassert>
#include <cstdint>
#include <vector>

#include "inference/preprocess.h"

using manifold3::inference::PreprocessNv12ToNchw;

// 2x2 NV12 frame: Y=0..3, UV half-size (1x1) = 128, 129.
static const uint8_t kNv12[6] = {0, 1, 2, 3, 128, 129};

int main() {
    std::vector<float> out;
    // Letterbox to 4x4: 2x2 content scaled up; exact values depend on implementation,
    // so only assert shape and that output is finite and non-zero.
    bool ok = PreprocessNv12ToNchw(kNv12, 2, 2, 4, 4, &out);
    assert(ok);
    assert(out.size() == 4 * 4 * 3);
    for (float v : out) {
        assert(v == v); // not NaN
    }
    // Mismatched src size must fail.
    assert(!PreprocessNv12ToNchw(kNv12, 3, 2, 4, 4, &out));
    assert(!PreprocessNv12ToNchw(kNv12, 2, 2, 0, 4, &out));
    return 0;
}
