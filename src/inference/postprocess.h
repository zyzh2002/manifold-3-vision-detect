#pragma once

#include <cstdint>
#include <vector>

#include "inference/inference_types.h"

namespace manifold3 {
namespace inference {

// Decodes YOLO11-seg raw outputs into detections. See Task 3 interface block
// for channel layout. Anchors is read from the tensor shape (output0 is
// [1, 4+nc+32, anchors]). Mask is reconstructed as sigmoid(coeffs * protos),
// thresholded at 0.5, then run-length encoded per detection.
void DecodeYolo11Seg(const std::vector<float> &output0, const std::vector<float> &output1,
                     const std::vector<float> &output2, uint32_t anchors, uint32_t mask_w, uint32_t mask_h,
                     std::vector<Detection> *detections);

} // namespace inference
} // namespace manifold3
