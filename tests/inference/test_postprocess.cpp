#include <cassert>
#include <cstdint>
#include <vector>

#include "inference/postprocess.h"

using manifold3::inference::DecodeYolo11Seg;
using manifold3::inference::Detection;

int main() {
    // One anchor, one 160x160 proto mask, 32 mask coeffs.
    constexpr uint32_t kAnchors = 1;
    constexpr uint32_t kMaskW = 160, kMaskH = 160;
    constexpr uint32_t kNumClasses = manifold3::inference::kNumSpecies + manifold3::inference::kNumAgeBins;
    // output0: 4 box + 7 classes + 32 coeffs.
    std::vector<float> out0(4 + kNumClasses + 32, 0.0f);
    // Box: cx=0.5, cy=0.5, w=0.2, h=0.1 (normalized).
    out0[0] = 0.5f;
    out0[1] = 0.5f;
    out0[2] = 0.2f;
    out0[3] = 0.1f;
    // Species class 1 (index 4+1) and age bin 3 (index 4+2+3) are confident.
    out0[4 + 1] = 0.9f;
    out0[4 + manifold3::inference::kNumSpecies + 3] = 0.8f;
    // Mask coefficients: all ones.
    for (uint32_t i = 0; i < 32; ++i) {
        out0[4 + kNumClasses + i] = 1.0f;
    }
    std::vector<float> out1(32 * kAnchors, 0.0f);
    for (uint32_t i = 0; i < 32; ++i) {
        out1[i] = 1.0f; // same coeffs as out0 for simplicity
    }
    std::vector<float> out2(32 * kMaskW * kMaskH, 0.0f);
    out2[0] = 5.0f; // proto pixel (0,0) of first proto strongly positive

    std::vector<Detection> dets;
    DecodeYolo11Seg(out0, out1, out2, kAnchors, kMaskW, kMaskH, &dets);
    // Confidence = max(species 0.9, age 0.8) = 0.9 > 0.25 -> one detection.
    assert(dets.size() == 1);
    assert(dets[0].species_id == 1);
    assert(dets[0].age_class_id == 3);
    assert(dets[0].confidence > 0.8f);
    assert(dets[0].cx >= 32767 && dets[0].cx <= 32768); // 0.5 * 65535
    assert(dets[0].cy >= 32767 && dets[0].cy <= 32768);
    assert(!dets[0].mask_rle.empty());

    // Below-threshold classes must produce no detection.
    out0[4 + 1] = 0.1f;
    out0[4 + manifold3::inference::kNumSpecies + 3] = 0.1f;
    dets.clear();
    DecodeYolo11Seg(out0, out1, out2, kAnchors, kMaskW, kMaskH, &dets);
    assert(dets.empty());
    return 0;
}
