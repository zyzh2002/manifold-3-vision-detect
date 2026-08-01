#pragma once

#include <cstdint>

namespace manifold3 {
namespace inference {

// Synthetic three-output ABI used only to exercise the device inference
// pipeline. Real-model ABI is defined in Plan B (not implemented yet).
constexpr char kSyntheticInputName[] = "images";
constexpr char kSyntheticPredictionName[] = "output0";
constexpr char kSyntheticMaskCoefficientsName[] = "output1";
constexpr char kSyntheticPrototypeName[] = "output2";

constexpr int32_t kSyntheticInputChannels = 3;
constexpr int32_t kSyntheticInputHeight = 1280;
constexpr int32_t kSyntheticInputWidth = 1280;
constexpr int32_t kSyntheticPredictionChannels = 43;
constexpr int32_t kSyntheticAnchors = 25600;
constexpr int32_t kSyntheticMaskCoefficientChannels = 32;
constexpr int32_t kSyntheticPrototypeHeight = 160;
constexpr int32_t kSyntheticPrototypeWidth = 160;

} // namespace inference
} // namespace manifold3
