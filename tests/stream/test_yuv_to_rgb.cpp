#include <cassert>
#include <cstdint>
#include <cstring>

#include "stream/yuv_to_rgb.h"

using manifold3::stream::DownscaleRgb24;
using manifold3::stream::YuvNv12ToRgb24;

namespace {

// 4x2 NV12 frame: 8 Y bytes, then U plane (2x1) = {64, 192}, then V plane
// (2x1) = {192, 64}. Column pairs share one UV pair.
//   Y:   128  0  255  128
//        128  128 255  0
//   UV:  (64,192) (192,64) per column pair.
const uint8_t kNv12[12] = {128, 0, 255, 128, 128, 128, 255, 0, 64, 192, 192, 64};

// Expected RGB24 (computed with the fixed-point formula's round-to-nearest
// rounding, clamped to [0,255]).
const uint8_t kExpected[4 * 2 * 3] = {
    218, 104, 15,   // (0,0): Y=128 U=64 V=192
    90, 0, 0,       // (1,0): Y=0   U=64 V=192 (G,B clamp to 0)
    165, 255, 255,  // (2,0): Y=255 U=192 V=64 (G,B clamp to 255)
    38, 152, 241,   // (3,0): Y=128 U=192 V=64
    218, 104, 15,   // (0,1): same UV as column 0
    218, 104, 15,   // (1,1): Y=128 U=64 V=192
    165, 255, 255,  // (2,1): Y=255 U=192 V=64
    0, 24, 113,     // (3,1): Y=0   U=192 V=64 (R clamps to 0)
};

void TestNv12ToRgb24() {
    uint8_t rgb[4 * 2 * 3];
    YuvNv12ToRgb24(kNv12, 4, 2, rgb);
    assert(std::memcmp(rgb, kExpected, sizeof(kExpected)) == 0);
}

void TestDownscale() {
    // 4x2 -> 2x1: each output pixel averages a 2x2 source block.
    const uint8_t src[4 * 2 * 3] = {
        0, 0, 0,   4, 0, 0,   0, 0, 0,   4, 0, 0,
        0, 0, 0,   4, 0, 0,   0, 0, 0,   4, 0, 0,
    };
    uint8_t dst[2 * 1 * 3];
    DownscaleRgb24(src, 4, 2, dst, 2, 1);
    const uint8_t expected[6] = {2, 0, 0, 2, 0, 0};
    assert(std::memcmp(dst, expected, sizeof(expected)) == 0);
}

} // namespace

int main() {
    TestNv12ToRgb24();
    TestDownscale();
    return 0;
}
