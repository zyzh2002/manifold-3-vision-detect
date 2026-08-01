#include "inference/postprocess.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "inference/synthetic_engine_contract.h"

namespace manifold3 {
namespace inference {

namespace {

constexpr uint32_t kNumMaskCoeffs = 32;

uint16_t NormalizedToU16(float v) {
    return static_cast<uint16_t>(std::max(0.0f, std::min(1.0f, v)) * 65535.0f);
}

float Sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

float Iou(const Detection &a, const Detection &b) {
    const float ax1 = a.cx - a.w / 2.0f, ay1 = a.cy - a.h / 2.0f;
    const float ax2 = a.cx + a.w / 2.0f, ay2 = a.cy + a.h / 2.0f;
    const float bx1 = b.cx - b.w / 2.0f, by1 = b.cy - b.h / 2.0f;
    const float bx2 = b.cx + b.w / 2.0f, by2 = b.cy + b.h / 2.0f;
    const float ix1 = std::max(ax1, bx1), iy1 = std::max(ay1, by1);
    const float ix2 = std::min(ax2, bx2), iy2 = std::min(ay2, by2);
    const float iw = std::max(0.0f, ix2 - ix1), ih = std::max(0.0f, iy2 - iy1);
    const float inter = iw * ih;
    const float unionA = (ax2 - ax1) * (ay2 - ay1) + (bx2 - bx1) * (by2 - by1) - inter;
    return unionA > 0.0f ? inter / unionA : 0.0f;
}

std::vector<uint8_t> EncodeRle(const std::vector<uint8_t> &mask) {
    // Run-length pairs of (value, count), count capped at 255.
    std::vector<uint8_t> rle;
    for (size_t i = 0; i < mask.size();) {
        const uint8_t value = mask[i];
        size_t j = i;
        while (j < mask.size() && j - i < 255 && mask[j] == value) {
            ++j;
        }
        rle.push_back(value);
        rle.push_back(static_cast<uint8_t>(j - i));
        i = j;
    }
    return rle;
}

} // namespace

bool DecodeSyntheticSeg(const SyntheticOutputs &outputs, std::vector<Detection> *detections) {
    if (detections == nullptr) {
        std::fprintf(stderr, "DecodeSyntheticSeg: null detections output\n");
        return false;
    }
    const size_t predictionSize = static_cast<size_t>(kSyntheticPredictionChannels) * kSyntheticAnchors;
    const size_t maskCoefficientsSize = static_cast<size_t>(kSyntheticMaskCoefficientChannels) * kSyntheticAnchors;
    const size_t prototypeSize = static_cast<size_t>(kSyntheticMaskCoefficientChannels) * kSyntheticPrototypeHeight *
                                 kSyntheticPrototypeWidth;
    if (outputs.prediction.size() != predictionSize) {
        std::fprintf(stderr, "DecodeSyntheticSeg: prediction size %zu, expected %zu\n", outputs.prediction.size(),
                     predictionSize);
        return false;
    }
    if (outputs.mask_coefficients.size() != maskCoefficientsSize) {
        std::fprintf(stderr, "DecodeSyntheticSeg: mask_coefficients size %zu, expected %zu\n",
                     outputs.mask_coefficients.size(), maskCoefficientsSize);
        return false;
    }
    if (outputs.prototype.size() != prototypeSize) {
        std::fprintf(stderr, "DecodeSyntheticSeg: prototype size %zu, expected %zu\n", outputs.prototype.size(),
                     prototypeSize);
        return false;
    }

    detections->clear();
    constexpr uint32_t kNumClasses = kNumSpecies + kNumAgeBins;
    constexpr uint32_t kAnchors = static_cast<uint32_t>(kSyntheticAnchors);
    constexpr uint32_t kMaskW = static_cast<uint32_t>(kSyntheticPrototypeWidth);
    constexpr uint32_t kMaskH = static_cast<uint32_t>(kSyntheticPrototypeHeight);
    const uint32_t maskStride = kAnchors;

    std::vector<Detection> candidates;
    for (uint32_t a = 0; a < kAnchors; ++a) {
        // Species and age are independent heads over their own channel spans;
        // detection confidence is the stronger of the two.
        float bestSpecies = 0.0f;
        uint32_t speciesIdx = 0;
        for (uint32_t c = 0; c < kNumSpecies; ++c) {
            const float score = outputs.prediction[(4 + c) * maskStride + a];
            if (score > bestSpecies) {
                bestSpecies = score;
                speciesIdx = c;
            }
        }
        float bestAge = 0.0f;
        uint32_t ageIdx = 0;
        for (uint32_t c = 0; c < kNumAgeBins; ++c) {
            const float score = outputs.prediction[(4 + kNumSpecies + c) * maskStride + a];
            if (score > bestAge) {
                bestAge = score;
                ageIdx = c;
            }
        }
        const float bestClass = std::max(bestSpecies, bestAge);
        if (bestClass < kConfidenceThreshold) {
            continue;
        }
        Detection d;
        d.cx = NormalizedToU16(outputs.prediction[0 * maskStride + a]);
        d.cy = NormalizedToU16(outputs.prediction[1 * maskStride + a]);
        d.w = NormalizedToU16(outputs.prediction[2 * maskStride + a]);
        d.h = NormalizedToU16(outputs.prediction[3 * maskStride + a]);
        d.confidence = bestClass;
        d.species_id = static_cast<uint16_t>(speciesIdx);
        d.age_class_id = static_cast<uint16_t>(ageIdx);

        // Mask: sum of coeffs * protos per pixel, sigmoid, threshold 0.5.
        std::vector<uint8_t> mask(kMaskW * kMaskH, 0);
        for (uint32_t p = 0; p < kMaskW * kMaskH; ++p) {
            float acc = 0.0f;
            for (uint32_t k = 0; k < kNumMaskCoeffs; ++k) {
                acc += outputs.mask_coefficients[k * maskStride + a] *
                       outputs.prototype[k * kMaskW * kMaskH + p];
            }
            mask[p] = Sigmoid(acc) >= 0.5f ? 1 : 0;
        }
        d.mask_rle = EncodeRle(mask);
        candidates.push_back(std::move(d));
    }

    // Greedy NMS by confidence.
    std::sort(candidates.begin(), candidates.end(),
              [](const Detection &a, const Detection &b) { return a.confidence > b.confidence; });
    std::vector<bool> suppressed(candidates.size(), false);
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (suppressed[i]) {
            continue;
        }
        detections->push_back(candidates[i]);
        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (!suppressed[j] && Iou(candidates[i], candidates[j]) > kNmsThreshold) {
                suppressed[j] = true;
            }
        }
    }
    return true;
}

} // namespace inference
} // namespace manifold3
