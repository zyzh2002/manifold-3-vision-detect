#pragma once

#include <cstdint>
#include <vector>

namespace manifold3 {
namespace inference {

// Converts an NV12 frame (Y plane, then interleaved UV) into a planar NCHW
// float tensor with RGB channel order, letterboxed to dst_w x dst_h and
// normalized to [0, 1]. Returns false when dimensions are invalid.
bool PreprocessNv12ToNchw(const uint8_t *nv12, uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
                          std::vector<float> *out);

} // namespace inference
} // namespace manifold3
