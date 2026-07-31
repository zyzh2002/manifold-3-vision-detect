#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "inference/preprocess.h"

using manifold3::inference::PreprocessNv12ToNchw;

// 2x2 NV12 frame: Y=0..3, UV half-size (1x1) = 128, 129.
static const uint8_t kNv12Gray[6] = {0, 1, 2, 3, 128, 129};

// 2x2 NV12 frame with uniform Y=128, U=255, V=0: YUV->RGB yields distinct
// planes (R clamped to 0, G ~ 175.70/255, B clamped to 255).
static const uint8_t kNv12Blue[6] = {128, 128, 128, 128, 255, 0};

int main() {
    std::vector<float> out;
    // Letterbox to 4x4: 2x2 content scaled up; exact values depend on implementation,
    // so only assert shape and that output is finite and non-zero.
    bool ok = PreprocessNv12ToNchw(kNv12Gray, 2, 2, 4, 4, &out);
    assert(ok);
    assert(out.size() == 4 * 4 * 3);
    for (float v : out) {
        assert(v == v); // not NaN
    }
    // Mismatched src size must fail.
    assert(!PreprocessNv12ToNchw(kNv12Gray, 3, 2, 4, 4, &out));
    assert(!PreprocessNv12ToNchw(kNv12Gray, 2, 2, 0, 4, &out));

    // Letterbox to 6x4: scale = min(6/2, 4/2) = 2, scaled content 4x4 centered
    // with offsetX = (6-4)/2 = 1, offsetY = 0. Columns 0 and 5 are padding.
    ok = PreprocessNv12ToNchw(kNv12Blue, 2, 2, 6, 4, &out);
    assert(ok);
    assert(out.size() == 6 * 4 * 3);
    for (uint32_t dy = 0; dy < 4; ++dy) {
        const uint32_t idx = dy * 6;
        // Padding columns (dx = 0 and dx = 5) must be zero in all three planes.
        for (uint32_t plane = 0; plane < 3; ++plane) {
            assert(std::fabs(out[plane * 24 + idx]) < 1e-6f);
            assert(std::fabs(out[plane * 24 + idx + 5]) < 1e-6f);
        }
    }
    // The 4x4 content region must not be all zeros.
    bool centerNonZero = false;
    for (uint32_t dy = 0; dy < 4; ++dy) {
        for (uint32_t dx = 1; dx <= 4; ++dx) {
            const uint32_t idx = dy * 6 + dx;
            for (uint32_t plane = 0; plane < 3; ++plane) {
                centerNonZero = centerNonZero || out[plane * 24 + idx] != 0.0f;
            }
        }
    }
    assert(centerNonZero);

    // Planar order and YUV->RGB coefficients: for Y=128, U=255, V=0
    //   r = 128 + 1.402*(0-128)  = -51.46  -> clamp 0
    //   g = 128 - 0.344136*127 - 0.714136*(-128) = 175.70 -> 175.70/255 ~ 0.689
    //   b = 128 + 1.772*127      = 353.04  -> clamp 255 -> 1.0
    // All content pixels sample the single UV pixel and uniform Y, so check one
    // interior pixel (dx=2, dy=2).
    const uint32_t center = 2 * 6 + 2;
    assert(out[center] < 0.01f);                           // R plane
    assert(std::fabs(out[24 + center] - 0.689f) < 0.005f); // G plane
    assert(out[48 + center] > 0.99f);                      // B plane
    return 0;
}
