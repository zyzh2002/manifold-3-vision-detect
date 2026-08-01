#include <cassert>
#include <cstdint>
#include <vector>

#include "inference/postprocess.h"
#include "synthetic_fixture.h"

using manifold3::inference::DecodeSyntheticSeg;
using manifold3::inference::Detection;
using manifold3::inference::SyntheticOutputs;
using manifold3::inference::kSyntheticMaskCoefficientChannels;
using manifold3::inference::kSyntheticPrototypeHeight;
using manifold3::inference::kSyntheticPrototypeWidth;
using manifold3::inference::test::DecodeRle;
using manifold3::inference::test::MakeSyntheticOutputs;
using manifold3::inference::test::PlaceAnchorBox;
using manifold3::inference::test::PlaceMaskCoefficients;
using manifold3::inference::test::PlacePrototypePixel;

int main() {
    // One confident anchor at anchor 0, full mask coefficients, prototype
    // plane with pixel (0,0) strongly positive and every other pixel strongly
    // negative, so the decoded mask is exactly one set pixel at (0,0).
    SyntheticOutputs outputs = MakeSyntheticOutputs();
    PlaceAnchorBox(&outputs, 0, 0.5f, 0.5f, 0.2f, 0.1f, 1, 0.9f, 3, 0.8f);
    PlaceMaskCoefficients(&outputs, 0, 1.0f);
    const size_t plane = static_cast<size_t>(kSyntheticPrototypeHeight) * kSyntheticPrototypeWidth;
    for (int32_t k = 0; k < kSyntheticMaskCoefficientChannels; ++k) {
        PlacePrototypePixel(&outputs, k, 0, 5.0f);
        for (size_t p = 1; p < plane; ++p) {
            PlacePrototypePixel(&outputs, k, p, -5.0f);
        }
    }

    std::vector<Detection> dets;
    assert(DecodeSyntheticSeg(outputs, &dets));
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
    assert(mask.size() == plane);
    assert(mask[0] == 1);
    for (size_t i = 1; i < mask.size(); ++i) {
        assert(mask[i] == 0);
    }

    // Below-threshold classes must produce no detection.
    SyntheticOutputs lowConf = MakeSyntheticOutputs();
    PlaceAnchorBox(&lowConf, 0, 0.5f, 0.5f, 0.2f, 0.1f, 1, 0.1f, 3, 0.1f);
    dets.clear();
    assert(DecodeSyntheticSeg(lowConf, &dets));
    assert(dets.empty());

    // NMS: two heavily overlapping boxes (IoU > 0.45); the lower-confidence
    // one must be suppressed. prediction is [1, 4+nc+32, anchors], so the
    // channel stride is the anchor count.
    SyntheticOutputs nms = MakeSyntheticOutputs();
    // Anchor 0 (A): cx=0.5, cy=0.5, w=0.2, h=0.2, species 0 confidence 0.9.
    PlaceAnchorBox(&nms, 0, 0.5f, 0.5f, 0.2f, 0.2f, 0, 0.9f, 0, 0.0f);
    // Anchor 1 (B): nearly identical box, species 0 confidence 0.8.
    PlaceAnchorBox(&nms, 1, 0.51f, 0.5f, 0.2f, 0.2f, 0, 0.8f, 0, 0.0f);
    // Mask coefficients are zero, so mask content is irrelevant here.
    dets.clear();
    assert(DecodeSyntheticSeg(nms, &dets));
    assert(dets.size() == 1);
    assert(dets[0].confidence > 0.85f); // A (0.9) survives, B (0.8) suppressed

    // NMS: add a third, non-overlapping box (C) far from A; it must be kept.
    SyntheticOutputs nms3 = MakeSyntheticOutputs();
    PlaceAnchorBox(&nms3, 0, 0.5f, 0.5f, 0.2f, 0.2f, 0, 0.9f, 0, 0.0f);
    PlaceAnchorBox(&nms3, 1, 0.51f, 0.5f, 0.2f, 0.2f, 0, 0.8f, 0, 0.0f);
    // Anchor 2 (C): cx=0.9, cy=0.9, w=0.1, h=0.1, species 0 confidence 0.7.
    PlaceAnchorBox(&nms3, 2, 0.9f, 0.9f, 0.1f, 0.1f, 0, 0.7f, 0, 0.0f);
    dets.clear();
    assert(DecodeSyntheticSeg(nms3, &dets));
    assert(dets.size() == 2);
    assert(dets[0].confidence > 0.85f);                               // A (0.9) first, B suppressed
    assert(dets[1].confidence > 0.65f && dets[1].confidence < 0.75f); // C (0.7) kept
    return 0;
}
