#pragma once

#include <cstdint>
#include <vector>

#include "inference/inference_types.h"
#include "inference/tensorrt_engine.h"

namespace manifold3 {
namespace inference {

// Validates exact synthetic output sizes before decoding. Returns false on
// mismatch or null detections; never reads out of bounds.
bool DecodeSyntheticSeg(const SyntheticOutputs &outputs, std::vector<Detection> *detections);

} // namespace inference
} // namespace manifold3
