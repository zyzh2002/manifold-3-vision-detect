// Host tests for the synthetic engine contract. TensorRtEngine::Load()'s
// name/mode/dtype/shape enforcement requires TensorRT, so that path is
// covered only by the target-side smoke run (Task 8/9), not by these unit
// tests. These tests cover the host-compilable parts: DecodeSyntheticSeg
// exact-size validation and the correct-path decode.
#include <cassert>
#include <cstdint>
#include <vector>

#include "inference/postprocess.h"
#include "inference/synthetic_engine_contract.h"
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
    // Null detection output -> false.
    {
        SyntheticOutputs outputs = MakeSyntheticOutputs();
        assert(!DecodeSyntheticSeg(outputs, nullptr));
    }

    // Prediction one element short -> false.
    {
        SyntheticOutputs outputs = MakeSyntheticOutputs();
        outputs.prediction.pop_back();
        std::vector<Detection> detections;
        assert(!DecodeSyntheticSeg(outputs, &detections));
    }

    // Prediction one element extra -> false.
    {
        SyntheticOutputs outputs = MakeSyntheticOutputs();
        outputs.prediction.push_back(0.0f);
        std::vector<Detection> detections;
        assert(!DecodeSyntheticSeg(outputs, &detections));
    }

    // Mask coefficients size mismatch -> false.
    {
        SyntheticOutputs outputs = MakeSyntheticOutputs();
        outputs.mask_coefficients.pop_back();
        std::vector<Detection> detections;
        assert(!DecodeSyntheticSeg(outputs, &detections));
    }

    // Prototype size mismatch -> false.
    {
        SyntheticOutputs outputs = MakeSyntheticOutputs();
        outputs.prototype.pop_back();
        std::vector<Detection> detections;
        assert(!DecodeSyntheticSeg(outputs, &detections));
    }

    // Exact-correct synthetic outputs -> true with the expected detection:
    // one confident anchor at anchor 0, full mask coefficients, prototype
    // plane with pixel (0,0) positive, everything else strongly negative.
    {
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
        std::vector<Detection> detections;
        assert(DecodeSyntheticSeg(outputs, &detections));
        assert(detections.size() == 1);
        assert(detections[0].species_id == 1);
        assert(detections[0].age_class_id == 3);
        assert(detections[0].confidence > 0.8f);
        assert(detections[0].cx >= 32767 && detections[0].cx <= 32768); // 0.5 * 65535
        assert(detections[0].cy >= 32767 && detections[0].cy <= 32768);
        assert(!detections[0].mask_rle.empty());
        // RLE must decode to exactly the single set pixel at (0,0).
        const std::vector<uint8_t> mask = DecodeRle(detections[0].mask_rle);
        assert(mask.size() == plane);
        assert(mask[0] == 1);
        for (size_t p = 1; p < mask.size(); ++p) {
            assert(mask[p] == 0);
        }
    }

    // Correct sizes but no confident anchor -> true and empty detections.
    {
        SyntheticOutputs outputs = MakeSyntheticOutputs();
        std::vector<Detection> detections;
        assert(DecodeSyntheticSeg(outputs, &detections));
        assert(detections.empty());
    }

    return 0;
}
