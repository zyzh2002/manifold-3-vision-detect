#include <cstddef>

#include "stream/yuv_to_rgb.h"

namespace manifold3 {
namespace stream {

namespace {

constexpr int32_t kScale = 8192;        // 13 fractional bits
constexpr int32_t kHalf = 4096;         // round-to-nearest offset
constexpr int32_t kCvR = 11485;         // 1.402  * 8192
constexpr int32_t kCuG = 2819;          // 0.344136 * 8192
constexpr int32_t kCvG = 5850;          // 0.714136 * 8192
constexpr int32_t kCuB = 14516;         // 1.772  * 8192

uint8_t ClampByte(int32_t v) {
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return static_cast<uint8_t>(v);
}

} // namespace

void YuvNv12ToRgb24(const uint8_t *nv12, uint32_t width, uint32_t height, uint8_t *rgbOut) {
    const uint32_t uvWidth = width / 2;
    const uint8_t *yPlane = nv12;
    const uint8_t *uvPlane = nv12 + static_cast<size_t>(width) * height;
    uint8_t *out = rgbOut;
    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t *uvRow = uvPlane + (row / 2) * uvWidth * 2;
        for (uint32_t col = 0; col < width; ++col) {
            const int32_t y = yPlane[row * width + col];
            const int32_t u = uvRow[(col / 2) * 2] - 128;
            const int32_t v = uvRow[(col / 2) * 2 + 1] - 128;
            const int32_t yBase = y * kScale;
            *out++ = ClampByte((yBase + kCvR * v + kHalf) >> 13);
            *out++ = ClampByte((yBase - kCuG * u - kCvG * v + kHalf) >> 13);
            *out++ = ClampByte((yBase + kCuB * u + kHalf) >> 13);
        }
    }
}

void DownscaleRgb24(const uint8_t *rgbIn, uint32_t srcW, uint32_t srcH, uint8_t *rgbOut,
                    uint32_t dstW, uint32_t dstH) {
    const uint32_t bw = srcW / dstW;
    const uint32_t bh = srcH / dstH;
    uint8_t *out = rgbOut;
    for (uint32_t dy = 0; dy < dstH; ++dy) {
        for (uint32_t dx = 0; dx < dstW; ++dx) {
            int32_t sum[3] = {0, 0, 0};
            for (uint32_t sy = dy * bh; sy < (dy + 1) * bh; ++sy) {
                const uint8_t *row = rgbIn + (sy * srcW + dx * bw) * 3;
                for (uint32_t sx = dx * bw; sx < (dx + 1) * bw; ++sx, row += 3) {
                    sum[0] += row[0];
                    sum[1] += row[1];
                    sum[2] += row[2];
                }
            }
            const uint32_t n = bw * bh;
            *out++ = static_cast<uint8_t>((sum[0] + n / 2) / n);
            *out++ = static_cast<uint8_t>((sum[1] + n / 2) / n);
            *out++ = static_cast<uint8_t>((sum[2] + n / 2) / n);
        }
    }
}

} // namespace stream
} // namespace manifold3
