#pragma once

#include <cstdint>

namespace manifold3 {
namespace stream {

// Converts one NV12 frame (Y plane then interleaved UV at half resolution)
// into RGB24 (R,G,B byte triplets, row-major). width/height must be even.
// BT.601 conversion with integer fixed-point arithmetic, 13 fractional bits.
void YuvNv12ToRgb24(const uint8_t *nv12, uint32_t width, uint32_t height, uint8_t *rgbOut);

// Box-average downscale of an RGB24 image. dstW/dstH must be <= srcW/srcH
// and even. Source pixels are partitioned into dstW*dstH equal blocks.
void DownscaleRgb24(const uint8_t *rgbIn, uint32_t srcW, uint32_t srcH, uint8_t *rgbOut,
                    uint32_t dstW, uint32_t dstH);

} // namespace stream
} // namespace manifold3
