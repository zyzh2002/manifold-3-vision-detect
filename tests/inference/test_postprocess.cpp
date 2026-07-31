#include <cassert>
#include <cstdint>
#include <vector>

#include "inference/postprocess.h"

using manifold3::inference::DecodeYolo11Seg;
using manifold3::inference::Detection;

namespace {

// Expands (value, count) RLE pairs back into a pixel mask.
std::vector<uint8_t> DecodeRle(const std::vector<uint8_t> &rle) {
    std::vector<uint8_t> mask;
    for (size_t i = 0; i + 1 < rle.size(); i += 2) {
        mask.insert(mask.end(), rle[i + 1], rle[i]);
    }
    return mask;
}

} // namespace

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
    // Proto masks: pixel (0,0) strongly positive, every other pixel strongly
    // negative, so the decoded mask is exactly one set pixel at (0,0).
    std::vector<float> out2(32 * kMaskW * kMaskH, -5.0f);
    for (uint32_t k = 0; k < 32; ++k) {
        out2[k * kMaskW * kMaskH] = 5.0f; // (0,0) of each of the 32 protos
    }

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
    // RLE must decode to exactly the single set pixel at (0,0).
    const std::vector<uint8_t> mask = DecodeRle(dets[0].mask_rle);
    assert(mask.size() == kMaskW * kMaskH);
    assert(mask[0] == 1);
    for (size_t i = 1; i < mask.size(); ++i) {
        assert(mask[i] == 0);
    }

    // Below-threshold classes must produce no detection.
    out0[4 + 1] = 0.1f;
    out0[4 + manifold3::inference::kNumSpecies + 3] = 0.1f;
    dets.clear();
    DecodeYolo11Seg(out0, out1, out2, kAnchors, kMaskW, kMaskH, &dets);
    assert(dets.empty());

    // NMS: two heavily overlapping boxes (IoU > 0.45); the lower-confidence
    // one must be suppressed. output0 is [1, 4+nc+32, anchors], so the channel
    // stride is the anchor count.
    constexpr uint32_t kNmsAnchors = 2;
    std::vector<float> nms0(4 + kNumClasses + 32, 0.0f);
    // Anchor 0 (A): cx=0.5, cy=0.5, w=0.2, h=0.2, species 0 confidence 0.9.
    nms0[0 * kNmsAnchors + 0] = 0.5f;
    nms0[1 * kNmsAnchors + 0] = 0.5f;
    nms0[2 * kNmsAnchors + 0] = 0.2f;
    nms0[3 * kNmsAnchors + 0] = 0.2f;
    nms0[(4 + 0) * kNmsAnchors + 0] = 0.9f;
    // Anchor 1 (B): nearly identical box, species 0 confidence 0.8.
    nms0[0 * kNmsAnchors + 1] = 0.51f;
    nms0[1 * kNmsAnchors + 1] = 0.5f;
    nms0[2 * kNmsAnchors + 1] = 0.2f;
    nms0[3 * kNmsAnchors + 1] = 0.2f;
    nms0[(4 + 0) * kNmsAnchors + 1] = 0.8f;
    // Mask coefficients are zero, so mask content is irrelevant here.
    std::vector<float> nms1(32 * kNmsAnchors, 0.0f);
    dets.clear();
    DecodeYolo11Seg(nms0, nms1, out2, kNmsAnchors, kMaskW, kMaskH, &dets);
    assert(dets.size() == 1);
    assert(dets[0].confidence > 0.85f); // A (0.9) survives, B (0.8) suppressed

    // NMS: add a third, non-overlapping box (C) far from A; it must be kept.
    constexpr uint32_t kNmsAnchors3 = 3;
    std::vector<float> nms3(4 + kNumClasses + 32, 0.0f);
    nms3[0 * kNmsAnchors3 + 0] = 0.5f;
    nms3[1 * kNmsAnchors3 + 0] = 0.5f;
    nms3[2 * kNmsAnchors3 + 0] = 0.2f;
    nms3[3 * kNmsAnchors3 + 0] = 0.2f;
    nms3[(4 + 0) * kNmsAnchors3 + 0] = 0.9f;
    nms3[0 * kNmsAnchors3 + 1] = 0.51f;
    nms3[1 * kNmsAnchors3 + 1] = 0.5f;
    nms3[2 * kNmsAnchors3 + 1] = 0.2f;
    nms3[3 * kNmsAnchors3 + 1] = 0.2f;
    nms3[(4 + 0) * kNmsAnchors3 + 1] = 0.8f;
    // Anchor 2 (C): cx=0.9, cy=0.9, w=0.1, h=0.1, species 0 confidence 0.7.
    nms3[0 * kNmsAnchors3 + 2] = 0.9f;
    nms3[1 * kNmsAnchors3 + 2] = 0.9f;
    nms3[2 * kNmsAnchors3 + 2] = 0.1f;
    nms3[3 * kNmsAnchors3 + 2] = 0.1f;
    nms3[(4 + 0) * kNmsAnchors3 + 2] = 0.7f;
    std::vector<float> nms3_1(32 * kNmsAnchors3, 0.0f);
    dets.clear();
    DecodeYolo11Seg(nms3, nms3_1, out2, kNmsAnchors3, kMaskW, kMaskH, &dets);
    assert(dets.size() == 2);
    assert(dets[0].confidence > 0.85f);                               // A (0.9) first, B suppressed
    assert(dets[1].confidence > 0.65f && dets[1].confidence < 0.75f); // C (0.7) kept
    return 0;
}
