#pragma once

#include <cstdint>
#include <vector>

#include "inference/synthetic_engine_contract.h"
#include "inference/tensorrt_engine.h"

namespace manifold3 {
namespace inference {
namespace test {

// Builds a full-size synthetic output set with all values zeroed.
SyntheticOutputs MakeSyntheticOutputs() {
    SyntheticOutputs outputs;
    outputs.prediction.resize(static_cast<size_t>(kSyntheticPredictionChannels) * kSyntheticAnchors, 0.0f);
    outputs.mask_coefficients.resize(static_cast<size_t>(kSyntheticMaskCoefficientChannels) * kSyntheticAnchors, 0.0f);
    outputs.prototype.resize(static_cast<size_t>(kSyntheticMaskCoefficientChannels) * kSyntheticPrototypeHeight *
                                 kSyntheticPrototypeWidth,
                             0.0f);
    return outputs;
}

// Places one anchor's box and class-head values. Layout is
// prediction[channel * anchors + anchor] with channels 0..3 box,
// 4..4+kNumSpecies-1 species, then kNumAgeBins age bins.
void PlaceAnchorBox(SyntheticOutputs *outputs, uint32_t anchor, float cx, float cy, float w, float h,
                    uint32_t species_id, float species_conf, uint32_t age_id, float age_conf) {
    const size_t stride = static_cast<size_t>(kSyntheticAnchors);
    outputs->prediction[0 * stride + anchor] = cx;
    outputs->prediction[1 * stride + anchor] = cy;
    outputs->prediction[2 * stride + anchor] = w;
    outputs->prediction[3 * stride + anchor] = h;
    outputs->prediction[(4 + species_id) * stride + anchor] = species_conf;
    outputs->prediction[(4 + kNumSpecies + age_id) * stride + anchor] = age_conf;
}

// Sets all 32 mask coefficients of one anchor to the same value.
void PlaceMaskCoefficients(SyntheticOutputs *outputs, uint32_t anchor, float value) {
    const size_t stride = static_cast<size_t>(kSyntheticAnchors);
    for (int32_t k = 0; k < kSyntheticMaskCoefficientChannels; ++k) {
        outputs->mask_coefficients[static_cast<size_t>(k) * stride + anchor] = value;
    }
}

// Sets one pixel of one prototype plane.
void PlacePrototypePixel(SyntheticOutputs *outputs, int32_t proto, size_t pixel, float value) {
    outputs->prototype[static_cast<size_t>(proto) * kSyntheticPrototypeHeight * kSyntheticPrototypeWidth + pixel] =
        value;
}

// Expands (value, count) RLE pairs back into a pixel mask.
std::vector<uint8_t> DecodeRle(const std::vector<uint8_t> &rle) {
    std::vector<uint8_t> mask;
    for (size_t i = 0; i + 1 < rle.size(); i += 2) {
        mask.insert(mask.end(), rle[i + 1], rle[i]);
    }
    return mask;
}

} // namespace test
} // namespace inference
} // namespace manifold3
