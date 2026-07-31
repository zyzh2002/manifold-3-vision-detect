#pragma once

#include <cstdint>
#include <vector>

namespace manifold3 {
namespace inference {

constexpr uint32_t kInputSize = 1280;
constexpr uint32_t kNumSpecies = 2; // spec Open Item: set when the species list is fixed
constexpr uint32_t kNumAgeBins = 5; // spec Open Item: set when the age span is fixed
constexpr float kConfidenceThreshold = 0.25f;
constexpr float kNmsThreshold = 0.45f;

struct Detection {
    uint16_t species_id;
    uint16_t age_class_id;
    float confidence;
    uint16_t cx;
    uint16_t cy;
    uint16_t w;
    uint16_t h;
    std::vector<uint8_t> mask_rle;
};

} // namespace inference
} // namespace manifold3
