#include "inference/preprocess.h"

#include <algorithm>

namespace manifold3 {
namespace inference {

namespace {

constexpr float kInv255 = 1.0f / 255.0f;

// Nearest-neighbor sample of the NV12 Y plane.
uint8_t SampleY(const uint8_t *nv12, uint32_t srcW, uint32_t srcH, uint32_t x, uint32_t y) {
    x = std::min(x, srcW - 1);
    y = std::min(y, srcH - 1);
    return nv12[y * srcW + x];
}

// Nearest-neighbor sample of the NV12 UV plane (2x2 subsampled, interleaved U,V).
void SampleUV(const uint8_t *nv12, uint32_t srcW, uint32_t srcH, uint32_t x, uint32_t y, uint8_t *u, uint8_t *v) {
    const uint32_t uvW = srcW / 2;
    x = std::min(x / 2, uvW - 1);
    y = std::min(y / 2, srcH / 2 - 1);
    const uint8_t *uv = nv12 + srcW * srcH + (y * uvW + x) * 2;
    *u = uv[0];
    *v = uv[1];
}

} // namespace

bool PreprocessNv12ToNchw(const uint8_t *nv12, uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
                          std::vector<float> *out) {
    if (nv12 == nullptr || out == nullptr || src_w == 0 || src_h == 0 || src_w % 2 != 0 || src_h % 2 != 0 ||
        dst_w == 0 || dst_h == 0) {
        return false;
    }
    out->assign(dst_w * dst_h * 3, 0.0f);

    // Letterbox scale: fit source into destination keeping aspect ratio.
    const float scale = std::min(static_cast<float>(dst_w) / src_w, static_cast<float>(dst_h) / src_h);
    const uint32_t scaledW = std::max(1u, static_cast<uint32_t>(src_w * scale));
    const uint32_t scaledH = std::max(1u, static_cast<uint32_t>(src_h * scale));
    const uint32_t offsetX = (dst_w - scaledW) / 2;
    const uint32_t offsetY = (dst_h - scaledH) / 2;

    for (uint32_t dy = 0; dy < dst_h; ++dy) {
        for (uint32_t dx = 0; dx < dst_w; ++dx) {
            if (dx < offsetX || dx >= offsetX + scaledW || dy < offsetY || dy >= offsetY + scaledH) {
                continue; // letterbox padding stays 0
            }
            const uint32_t sx = (dx - offsetX) * src_w / scaledW;
            const uint32_t sy = (dy - offsetY) * src_h / scaledH;
            const uint8_t y = SampleY(nv12, src_w, src_h, sx, sy);
            uint8_t u = 0, v = 0;
            SampleUV(nv12, src_w, src_h, sx, sy, &u, &v);
            const float r = y + 1.402f * (v - 128.0f);
            const float g = y - 0.344136f * (u - 128.0f) - 0.714136f * (v - 128.0f);
            const float b = y + 1.772f * (u - 128.0f);
            const uint32_t idx = dy * dst_w + dx;
            (*out)[idx] = std::max(0.0f, std::min(255.0f, r)) * kInv255;
            (*out)[dst_w * dst_h + idx] = std::max(0.0f, std::min(255.0f, g)) * kInv255;
            (*out)[2 * dst_w * dst_h + idx] = std::max(0.0f, std::min(255.0f, b)) * kInv255;
        }
    }
    return true;
}

} // namespace inference
} // namespace manifold3
