# Inference Pipeline Implementation Plan (Phase 5, Device Side)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run continuous TensorRT inference on Manifold 3 over the Phase 4 NV12 capture stream, producing structured detections with bounded memory.

**Architecture:** `src/inference/` owns engine loading (TensorRT 8.5.2, cross-compile only), CPU preprocessing (NV12->RGB->1280x1280->NCHW), and YOLO-style postprocessing (threshold + NMS + RLE mask). `src/app/main.cpp` connects capture -> inference and prints per-second latency/throughput stats. A dummy ONNX with a synthetic three-output test contract validates the pipeline before any real model exists.

**Tech Stack:** C++17, TensorRT 8.5.2 (C API runtime), CUDA 11.4, Python 3 + onnx (dummy model generation), CMake cross preset.

## Global Constraints

- Target: Manifold 3, JetPack 5.1.3, Jetson Linux r35.5.0, glibc 2.31, TensorRT 8.5.2, CUDA 11.4.
- Cross-compile with NVIDIA Bootlin GCC 9.3.0 against `sysroot/`; never resolve headers/libraries from host x86_64 paths.
- Engine files are device-bound: convert with `trtexec` on the device, never reuse engines across versions or architectures.
- The 30 fps NV12 input gives a 33 ms/frame budget; inference (FP16) should stay well under it, leaving room for CPU preprocessing.
- Result schema must be independent of PSDK callback internals.
- C++17 for `src/inference/`; LLVM clang-format, 120 columns; snake_case files, PascalCase types; no Chinese comments.
- Host (x86_64) builds must still configure and build; `tensorrt_engine.cpp` is excluded from host builds (no TensorRT on host).
- `third_party/psdk/` is read-only; do not modify.
- The drone/device connection is currently suspended. Tasks that require the device (trtexec conversion, target runs) are executed after the user re-allows the connection.

---

### Task 1: Extend sysroot with CUDA/TensorRT development files

**Files:**
- Create: `docs/build-environment.md` (append "Phase 5 Sysroot Extension" section)
- Create: `scripts/check_inference_sysroot.sh` (verification helper)

**Interfaces:**
- Consumes: existing `sysroot/` (Phase 2 base, Jetson Linux r35.5.0).
- Produces: `sysroot/usr/include/aarch64-linux-gnu/NvInfer.h`, `NvOnnxParser.h`, `cuda_runtime.h`; `sysroot/usr/lib/aarch64-linux-gnu/libnvinfer.so`, `libnvonnxparser.so`, `libcudart.so`; and a documented, repeatable procedure.

- [ ] **Step 1: Record which dev packages the device firmware already ships**

When device access is re-allowed, run on the device:

```bash
dpkg -l | grep -E "libnvinfer-dev|libnvonnxparser|nvidia-cuda|cudart"
```

Expected: `libnvinfer-dev 8.5.2-1+cuda11.4` (arm64) and the corresponding CUDA runtime dev packages (observed in Phase 4; record exact names/versions in the doc).

- [ ] **Step 2: Copy dev headers and libraries from the device into the sysroot**

From the device copy (paths confirmed present in Phase 4):

```bash
# TensorRT headers (device: /usr/include/aarch64-linux-gnu/)
scp -i config/manifold3_id_rsa dji@192.168.42.120:/usr/include/aarch64-linux-gnu/NvInfer*.h sysroot/usr/include/aarch64-linux-gnu/
scp -i config/manifold3_id_rsa dji@192.168.42.120:/usr/include/aarch64-linux-gnu/NvOnnx*.h sysroot/usr/include/aarch64-linux-gnu/
# CUDA Toolkit headers (device: /usr/local/cuda/include/) - the whole tree,
# because cuda_runtime.h pulls in cuda.h, crt/host_config.h, etc.
mkdir -p sysroot/usr/local/cuda/include
scp -r -i config/manifold3_id_rsa dji@192.168.42.120:/usr/local/cuda/include/. sysroot/usr/local/cuda/include/
# TensorRT libraries (dev symlinks + runtime .so.8.5.2)
scp -i config/manifold3_id_rsa dji@192.168.42.120:/usr/lib/aarch64-linux-gnu/libnvinfer.so* sysroot/usr/lib/aarch64-linux-gnu/
scp -i config/manifold3_id_rsa dji@192.168.42.120:/usr/lib/aarch64-linux-gnu/libnvonnxparser.so* sysroot/usr/lib/aarch64-linux-gnu/
# CUDA runtime libraries (device: /usr/local/cuda/lib64/)
mkdir -p sysroot/usr/local/cuda/lib64
scp -r -i config/manifold3_id_rsa dji@192.168.42.120:/usr/local/cuda/lib64/libcudart.so* sysroot/usr/local/cuda/lib64/
```

Record the exact source paths and package versions in `docs/build-environment.md` under a new "Phase 5 Sysroot Extension" section. If device access is unavailable, download the matching r35.5.0 `.deb` packages from NVIDIA (same versions) and extract with `dpkg-deb -x` into the sysroot; record which packages.

- [ ] **Step 3: Add a verification helper script**

Create `scripts/check_inference_sysroot.sh`:

```bash
#!/usr/bin/env bash
# Verifies the Phase 5 sysroot extension for TensorRT inference builds.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SYSROOT="${MANIFOLD3_SYSROOT:-${REPO_ROOT}/sysroot}"
HDR="${SYSROOT}/usr/include/aarch64-linux-gnu"
LIB="${SYSROOT}/usr/lib/aarch64-linux-gnu"
CUDA_HDR="${SYSROOT}/usr/local/cuda/include"
CUDA_LIB="${SYSROOT}/usr/local/cuda/lib64"
missing=0
for f in "${HDR}/NvInfer.h" "${HDR}/NvOnnxParser.h" "${CUDA_HDR}/cuda_runtime.h" \
         "${CUDA_HDR}/cuda.h" "${CUDA_HDR}/crt/host_config.h"; do
    if [ ! -f "$f" ]; then echo "MISSING $f"; missing=1; fi
done
for f in "${LIB}/libnvinfer.so" "${LIB}/libnvonnxparser.so" "${CUDA_LIB}/libcudart.so"; do
    if [ ! -e "$f" ]; then echo "MISSING $f"; missing=1; fi
done
if [ "$missing" -eq 1 ]; then echo "FAIL"; exit 1; fi
echo "PASS: inference sysroot extension present"
```

- [ ] **Step 4: Run the helper**

```bash
bash scripts/check_inference_sysroot.sh
```

Expected: `PASS: inference sysroot extension present`

- [ ] **Step 5: Commit**

```bash
git add docs/build-environment.md scripts/check_inference_sysroot.sh
git commit -m "feat: extend sysroot with CUDA/TensorRT dev files for phase 5"
```

---

### Task 2: Dummy YOLO11-seg ONNX generation script

**Files:**
- Create: `scripts/generate_dummy_onnx.py`

**Interfaces:**
- Produces: `build/dummy_yolo11_seg.onnx` with a synthetic three-output test contract at 1280x1280 input: `output0 [1, 4+7+32, 25600]` (=43 channels), `output1 [1, 32, 25600]`, `output2 [1, 32, 160, 160]`. This lets Tasks 3-5 develop against the full output layout before a real model exists. Constants used by Task 4/5: `kInputSize=1280`, `kNumSpecies=2`, `kNumAgeBins=5`, `kNumClasses=7` (=species+age), `kNumMaskCoeffs=32`, `kNumAnchors=25600` (single-stride 160x160 grid), `kMaskProtoH=160`, `kMaskProtoW=160`.

Note: this dummy generator defines a synthetic three-output test contract; the real-model ABI (standard 2-output or custom) is defined in the Phase B plan, not here.

- [ ] **Step 1: Write the generator script**

```python
#!/usr/bin/env python3
"""Generates a dummy ONNX model with YOLO11-seg output shapes.

The network is a single Conv that downsamples the 1280x1280 input to 160x160
feature maps and produces the three YOLO11-seg outputs with correct shapes but
deterministic garbage values. Used to validate the inference pipeline before a
real model is available.
"""
import argparse

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

K_NUM_SPECIES = 2
K_NUM_AGE_BINS = 5
K_NUM_CLASSES = K_NUM_SPECIES + K_NUM_AGE_BINS  # 7
K_NUM_MASK_COEFFS = 32
K_INPUT_SIZE = 1280
K_FEAT = 160  # 1280 / 8
K_ANCHORS = 160 * 160  # 25600, single-stride 160x160 grid (real model concatenates 3 strides into 33600)
K_PROTO = 32

K_CLS_CHANNELS = 4 + K_NUM_CLASSES + K_NUM_MASK_COEFFS  # 43


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="build/dummy_yolo11_seg.onnx")
    args = parser.parse_args()

    inp = helper.make_tensor_value_info("images", TensorProto.FLOAT, [1, 3, K_INPUT_SIZE, K_INPUT_SIZE])
    # YOLO11-seg emits (1, 4+nc+32, anchors) per stride; dummy uses one stride.
    out0 = helper.make_tensor_value_info("output0", TensorProto.FLOAT, [1, K_CLS_CHANNELS, K_ANCHORS])
    out1 = helper.make_tensor_value_info("output1", TensorProto.FLOAT, [1, K_NUM_MASK_COEFFS, K_ANCHORS])
    out2 = helper.make_tensor_value_info("output2", TensorProto.FLOAT, [1, K_PROTO, K_FEAT, K_FEAT])

    # One conv: 3->K_CLS_CHANNELS, then a slice into three outputs.
    w0 = numpy_helper.from_array(
        np.random.RandomState(0).randn(K_CLS_CHANNELS, 3, 1, 1).astype(np.float32) * 0.01,
        "conv_w",
    )
    conv = helper.make_node("Conv", ["images", "conv_w"], ["feat"], kernel_shape=[1, 1], pads=[0, 0, 0, 0])
    # Slice feature dims: [0:43] -> output0; [4+nc : 4+nc+32] -> mask coeffs; proto from [0:32] reshaped.
    sl0 = helper.make_node("Slice", ["feat"], ["out0"], starts=[0, 0, 0], ends=[1, K_CLS_CHANNELS, K_ANCHORS], axes=[0, 1, 2])
    # Mask coeffs live in channels [4+nc, 4+nc+32)
    sl1 = helper.make_node("Slice", ["feat"], ["mask_coeffs"], starts=[0, 4 + K_NUM_CLASSES, 0], ends=[1, 4 + K_NUM_CLASSES + K_NUM_MASK_COEFFS, K_ANCHORS], axes=[0, 1, 2])
    proto = helper.make_node("Slice", ["feat"], ["proto_slice"], starts=[0, 0], ends=[1, 32], axes=[0, 1])
    resh = helper.make_node("Reshape", ["proto_slice", "shape_proto"], ["out2"])
    shape_proto = numpy_helper.from_array(np.array([1, K_PROTO, K_FEAT, K_FEAT], dtype=np.int64), "shape_proto")

    graph = helper.make_graph(
        [conv, sl0, sl1, proto, resh],
        "dummy_yolo11_seg",
        [inp],
        [out0, out1, out2],
        initializer=[w0, shape_proto],
    )
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)])
    onnx.checker.check_model(model)
    onnx.save(model, args.out)
    print(f"Wrote {args.out}")
    for info in (out0, out1, out2):
        print(f"  {info.name}: {[d.dim_value for d in info.type.tensor_type.shape.dim]}")


if __name__ == "__main__":
    main()
```

Note: `K_ANCHORS = 160*160` models a single-stride anchor grid (the real model concatenates 160/80/40 grids into 33600). The dummy keeps one stride so the generated file stays trivial; Task 4's decoder must accept the anchor count from the tensor shape, not a hardcoded constant.

- [ ] **Step 2: Run the generator and verify shapes**

```bash
mkdir -p build && python3 scripts/generate_dummy_onnx.py
python3 - <<'EOF'
import onnx
m = onnx.load("build/dummy_yolo11_seg.onnx")
for vi in m.graph.output:
    print(vi.name, [d.dim_value for d in vi.type.tensor_type.shape.dim])
EOF
```

Expected:
```
output0 [1, 43, 25600]
output1 [1, 32, 25600]
output2 [1, 32, 160, 160]
```

- [ ] **Step 3: Commit**

```bash
git add scripts/generate_dummy_onnx.py
git commit -m "feat: add dummy YOLO11-seg ONNX generator for pipeline validation"
```

---

### Task 3: Inference result schema and preprocessing/postprocessing (host-testable)

**Files:**
- Create: `src/inference/inference_types.h`
- Create: `src/inference/preprocess.h`, `src/inference/preprocess.cpp`
- Create: `src/inference/postprocess.h`, `src/inference/postprocess.cpp`
- Create: `src/inference/CMakeLists.txt`
- Modify: `src/CMakeLists.txt` (add `add_subdirectory(inference)`)
- Create: `tests/inference/test_preprocess.cpp`, `tests/inference/test_postprocess.cpp`, `tests/inference/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt` (add `add_subdirectory(inference)`)

**Interfaces:**
- Consumes: nothing from other tasks (pure C++, no TensorRT, host-testable).
- Produces (used by Task 4/5):

```cpp
namespace manifold3::inference {

constexpr uint32_t kInputSize = 1280;
constexpr uint32_t kNumSpecies = 2;      // TODO: configurable, spec Open Items
constexpr uint32_t kNumAgeBins = 5;      // TODO: configurable, spec Open Items
constexpr float kConfidenceThreshold = 0.25f;
constexpr float kNmsThreshold = 0.45f;

struct Detection {
    uint16_t species_id;
    uint16_t age_class_id;
    float confidence;
    uint16_t cx, cy, w, h;               // normalized to [0, 65535]
    std::vector<uint8_t> mask_rle;       // run-length encoded binary mask
};

// NV12 (Y plane then interleaved UV) -> NCHW float, letterboxed to dst_w x dst_h.
// Returns false on size mismatch. Out is sized dst_w*dst_h*3.
bool PreprocessNv12ToNchw(const uint8_t *nv12, uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
                          std::vector<float> *out);

// Decodes YOLO11-seg outputs into detections.
// output0: [1, 4+nc+32, anchors], output1: [1, 32, anchors], output2: [1, 32, 160, 160].
// Class channels: [0,4) box(cx,cy,w,h normalized), [4,4+nc) classes (species then age bins),
// [4+nc, 4+nc+32) mask coefficients.
void DecodeYolo11Seg(const std::vector<float> &output0, const std::vector<float> &output1,
                     const std::vector<float> &output2, uint32_t anchors, uint32_t mask_w, uint32_t mask_h,
                     std::vector<Detection> *detections);

}  // namespace manifold3::inference
```

- [ ] **Step 1: Write the failing tests first**

Create `tests/inference/test_preprocess.cpp`:

```cpp
#include <cassert>
#include <cstdint>
#include <vector>

#include "inference/preprocess.h"

using manifold3::inference::PreprocessNv12ToNchw;

// 2x2 NV12 frame: Y=0..3, UV half-size (1x1) = 128, 129.
static const uint8_t kNv12[6] = {0, 1, 2, 3, 128, 129};

int main() {
    std::vector<float> out;
    // Letterbox to 4x4: 2x2 content scaled up; exact values depend on implementation,
    // so only assert shape and that output is finite and non-zero.
    bool ok = PreprocessNv12ToNchw(kNv12, 2, 2, 4, 4, &out);
    assert(ok);
    assert(out.size() == 4 * 4 * 3);
    for (float v : out) {
        assert(v == v);  // not NaN
    }
    // Mismatched src size must fail.
    assert(!PreprocessNv12ToNchw(kNv12, 3, 2, 4, 4, &out));
    assert(!PreprocessNv12ToNchw(kNv12, 2, 2, 0, 4, &out));
    return 0;
}
```

Create `tests/inference/test_postprocess.cpp`:

```cpp
#include <cassert>
#include <cstdint>
#include <vector>

#include "inference/postprocess.h"

using manifold3::inference::{DecodeYolo11Seg, Detection};

int main() {
    // One anchor, one 160x160 proto mask, 32 mask coeffs.
    constexpr uint32_t kAnchors = 1;
    constexpr uint32_t kMaskW = 160, kMaskH = 160;
    constexpr uint32_t kNumClasses = manifold3::inference::kNumSpecies + manifold3::inference::kNumAgeBins;
    // output0: 4 box + 7 classes + 32 coeffs.
    std::vector<float> out0(4 + kNumClasses + 32, 0.0f);
    // Box: cx=0.5, cy=0.5, w=0.2, h=0.1 (normalized).
    out0[0] = 0.5f; out0[1] = 0.5f; out0[2] = 0.2f; out0[3] = 0.1f;
    // Species class 1 (index 4+1) and age bin 3 (index 4+2+3) are confident.
    out0[4 + 1] = 0.9f;
    out0[4 + manifold3::inference::kNumSpecies + 3] = 0.8f;
    // Mask coefficients: all ones.
    for (uint32_t i = 0; i < 32; ++i) {
        out0[4 + kNumClasses + i] = 1.0f;
    }
    std::vector<float> out1(32 * kAnchors, 0.0f);
    for (uint32_t i = 0; i < 32; ++i) {
        out1[i] = 1.0f;  // same coeffs as out0 for simplicity
    }
    std::vector<float> out2(32 * kMaskW * kMaskH, 0.0f);
    out2[0] = 5.0f;  // proto pixel (0,0) of first proto strongly positive

    std::vector<Detection> dets;
    DecodeYolo11Seg(out0, out1, out2, kAnchors, kMaskW, kMaskH, &dets);
    // Confidence = max(species 0.9, age 0.8) = 0.9 > 0.25 -> one detection.
    assert(dets.size() == 1);
    assert(dets[0].species_id == 1);
    assert(dets[0].age_class_id == 3);
    assert(dets[0].confidence > 0.8f);
    assert(dets[0].cx >= 32767 && dets[0].cx <= 32768);  // 0.5 * 65535
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
```

- [ ] **Step 2: Run tests and verify they fail (no implementation yet)**

```bash
cmake --preset host-debug && cmake --build --preset host-debug --target test_inference_preprocess test_inference_postprocess
```

Expected: build failure, "no such file or directory" for `inference/preprocess.h`.

- [ ] **Step 3: Implement `inference_types.h`**

```cpp
#pragma once

#include <cstdint>
#include <vector>

namespace manifold3 {
namespace inference {

constexpr uint32_t kInputSize = 1280;
constexpr uint32_t kNumSpecies = 2;  // spec Open Item: set when the species list is fixed
constexpr uint32_t kNumAgeBins = 5;  // spec Open Item: set when the age span is fixed
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

}  // namespace inference
}  // namespace manifold3
```

- [ ] **Step 4: Implement `preprocess.h/.cpp`**

`src/inference/preprocess.h`:

```cpp
#pragma once

#include <cstdint>
#include <vector>

namespace manifold3 {
namespace inference {

// Converts an NV12 frame (Y plane, then interleaved UV) into a planar NCHW
// float tensor with RGB channel order, letterboxed to dst_w x dst_h and
// normalized to [0, 1]. Returns false when dimensions are invalid.
bool PreprocessNv12ToNchw(const uint8_t *nv12, uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
                          std::vector<float> *out);

}  // namespace inference
}  // namespace manifold3
```

`src/inference/preprocess.cpp`:

```cpp
#include "inference/preprocess.h"

#include <algorithm>

namespace manifold3 {
namespace inference {

namespace {

constexpr float kInv255 = 1.0f / 255.0f;

// Nearest-neighbor sample of the NV12 Y plane.
uint8_t SampleY(const uint8_t *nv12, uint32_t srcW, uint32_t srcH, uint32_t x, uint32_t y) {
    x = std::min(x, srcW - 1);
    y = std::min(y, srcH - 1);
    return nv12[y * srcW + x];
}

// Nearest-neighbor sample of the NV12 UV plane (2x2 subsampled, interleaved U,V).
void SampleUV(const uint8_t *nv12, uint32_t srcW, uint32_t srcH, uint32_t x, uint32_t y, uint8_t *u, uint8_t *v) {
    const uint32_t uvW = srcW / 2;
    x = std::min(x / 2, uvW - 1);
    y = std::min(y / 2, srcH / 2 - 1);
    const uint8_t *uv = nv12 + srcW * srcH + (y * uvW + x) * 2;
    *u = uv[0];
    *v = uv[1];
}

}  // namespace

bool PreprocessNv12ToNchw(const uint8_t *nv12, uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h,
                          std::vector<float> *out) {
    if (nv12 == nullptr || out == nullptr || src_w == 0 || src_h == 0 || src_w % 2 != 0 || src_h % 2 != 0 ||
        dst_w == 0 || dst_h == 0) {
        return false;
    }
    out->assign(dst_w * dst_h * 3, 0.0f);

    // Letterbox scale: fit source into destination keeping aspect ratio.
    const float scale = std::min(static_cast<float>(dst_w) / src_w, static_cast<float>(dst_h) / src_h);
    const uint32_t scaledW = std::max(1u, static_cast<uint32_t>(src_w * scale));
    const uint32_t scaledH = std::max(1u, static_cast<uint32_t>(src_h * scale));
    const uint32_t offsetX = (dst_w - scaledW) / 2;
    const uint32_t offsetY = (dst_h - scaledH) / 2;

    for (uint32_t dy = 0; dy < dst_h; ++dy) {
        for (uint32_t dx = 0; dx < dst_w; ++dx) {
            if (dx < offsetX || dx >= offsetX + scaledW || dy < offsetY || dy >= offsetY + scaledH) {
                continue;  // letterbox padding stays 0
            }
            const uint32_t sx = (dx - offsetX) * src_w / scaledW;
            const uint32_t sy = (dy - offsetY) * src_h / scaledH;
            const uint8_t y = SampleY(nv12, src_w, src_h, sx, sy);
            uint8_t u = 0, v = 0;
            SampleUV(nv12, src_w, src_h, sx, sy, &u, &v);
            const float r = y + 1.402f * (v - 128.0f);
            const float g = y - 0.344136f * (u - 128.0f) - 0.714136f * (v - 128.0f);
            const float b = y + 1.772f * (u - 128.0f);
            const uint32_t idx = dy * dst_w + dx;
            (*out)[idx] = std::max(0.0f, std::min(255.0f, r)) * kInv255;
            (*out)[dst_w * dst_h + idx] = std::max(0.0f, std::min(255.0f, g)) * kInv255;
            (*out)[2 * dst_w * dst_h + idx] = std::max(0.0f, std::min(255.0f, b)) * kInv255;
        }
    }
    return true;
}

}  // namespace inference
}  // namespace manifold3
```

- [ ] **Step 5: Implement `postprocess.h/.cpp`**

`src/inference/postprocess.h`:

```cpp
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

}  // namespace inference
}  // namespace manifold3
```

`src/inference/postprocess.cpp`:

```cpp
#include "inference/postprocess.h"

#include <algorithm>
#include <cmath>

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

}  // namespace

void DecodeYolo11Seg(const std::vector<float> &output0, const std::vector<float> &output1,
                     const std::vector<float> &output2, uint32_t anchors, uint32_t mask_w, uint32_t mask_h,
                     std::vector<Detection> *detections) {
    detections->clear();
    constexpr uint32_t kNumClasses = kNumSpecies + kNumAgeBins;
    const uint32_t maskStride = anchors;

    std::vector<Detection> candidates;
    for (uint32_t a = 0; a < anchors; ++a) {
        // Species and age are independent heads over their own channel spans;
        // detection confidence is the stronger of the two.
        float bestSpecies = 0.0f;
        uint32_t speciesIdx = 0;
        for (uint32_t c = 0; c < kNumSpecies; ++c) {
            const float score = output0[(4 + c) * maskStride + a];
            if (score > bestSpecies) {
                bestSpecies = score;
                speciesIdx = c;
            }
        }
        float bestAge = 0.0f;
        uint32_t ageIdx = 0;
        for (uint32_t c = 0; c < kNumAgeBins; ++c) {
            const float score = output0[(4 + kNumSpecies + c) * maskStride + a];
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
        d.cx = NormalizedToU16(output0[0 * maskStride + a]);
        d.cy = NormalizedToU16(output0[1 * maskStride + a]);
        d.w = NormalizedToU16(output0[2 * maskStride + a]);
        d.h = NormalizedToU16(output0[3 * maskStride + a]);
        d.confidence = bestClass;
        d.species_id = static_cast<uint16_t>(speciesIdx);
        d.age_class_id = static_cast<uint16_t>(ageIdx);

        // Mask: sum of coeffs * protos per pixel, sigmoid, threshold 0.5.
        std::vector<uint8_t> mask(mask_w * mask_h, 0);
        for (uint32_t p = 0; p < mask_w * mask_h; ++p) {
            float acc = 0.0f;
            for (uint32_t k = 0; k < kNumMaskCoeffs; ++k) {
                acc += output1[k * maskStride + a] * output2[k * mask_w * mask_h + p];
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
}

}  // namespace inference
}  // namespace manifold3
```

- [ ] **Step 6: Wire up CMake**

`src/inference/CMakeLists.txt`:

```cmake
# Pure C++ preprocessing/postprocessing: buildable on host for unit tests.
add_library(inference STATIC
    preprocess.cpp
    postprocess.cpp
)

target_include_directories(inference PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

# TensorRT engine loading is a cross-only source; see Task 4.
```

Modify `src/CMakeLists.txt`:

```cmake
add_subdirectory(platform)
add_subdirectory(core)
add_subdirectory(capture)
add_subdirectory(inference)
add_subdirectory(app)
```

`tests/inference/CMakeLists.txt`:

```cmake
# Host unit tests for the pure-C++ inference pieces. The TensorRT engine
# wrapper (Task 4) is exercised only on the target.
add_executable(test_inference_preprocess test_preprocess.cpp)
target_link_libraries(test_inference_preprocess PRIVATE inference)

add_executable(test_inference_postprocess test_postprocess.cpp)
target_link_libraries(test_inference_postprocess PRIVATE inference)

add_test(NAME inference_preprocess COMMAND test_inference_preprocess)
add_test(NAME inference_postprocess COMMAND test_inference_postprocess)
```

Modify `tests/CMakeLists.txt` (append): `add_subdirectory(inference)`

- [ ] **Step 7: Run tests, iterate to green**

```bash
cmake --preset host-debug && cmake --build --preset host-debug && ctest --test-dir build-host --output-on-failure
```

Expected: `inference_preprocess` and `inference_postprocess` both Pass. If the postprocess test fails on `cx == 32768`, check rounding: `NormalizedToU16` truncates; adjust the test to `>= 32767 && <= 32768` or round in the implementation (keep implementation, relax test).

- [ ] **Step 8: Cross-compile check**

```bash
source scripts/setup_env.sh && cmake --preset manifold3-cross-release && cmake --build --preset manifold3-cross-release
```

Expected: builds clean, all smoke tests pass (the app target links against `inference` without TensorRT yet, since Task 4 adds the engine source).

- [ ] **Step 9: Commit**

```bash
git add src/inference/ src/CMakeLists.txt tests/inference/ tests/CMakeLists.txt
git commit -m "feat: add inference preprocessing and YOLO11-seg postprocessing with host tests"
```

---

### Task 4: TensorRT engine wrapper (cross-compile only)

**Files:**
- Create: `src/inference/tensorrt_engine.h`, `src/inference/tensorrt_engine.cpp`
- Modify: `src/inference/CMakeLists.txt` (conditionally add tensorrt_engine.cpp and link flags)
- Create: `scripts/run_inference_smoke.sh` (target-side smoke runner)

**Interfaces:**
- Consumes: `src/inference/preprocess.h`, `src/inference/postprocess.h`, dummy engine file `dummy_yolo11_seg.engine` (from Task 2, converted on device).
- Produces:

```cpp
namespace manifold3::inference {

class TensorRtEngine {
  public:
    TensorRtEngine() = default;
    ~TensorRtEngine();

    TensorRtEngine(const TensorRtEngine &) = delete;
    TensorRtEngine &operator=(const TensorRtEngine &) = delete;

    // Loads a serialized engine from path. Returns false on any failure.
    bool Load(const std::string &engine_path);

    // Runs inference on one NCHW float input (size = channels*h*w) into the
    // three YOLO11-seg outputs. Latency is measured in microseconds when
    // latency_us is non-null.
    bool Infer(const std::vector<float> &input, std::vector<float> *out0, std::vector<float> *out1,
               std::vector<float> *out2, int64_t *latency_us = nullptr);

  private:
    void *runtime_ = nullptr;   // nvinfer1::IRuntime*
    void *engine_ = nullptr;    // nvinfer1::ICudaEngine*
    void *context_ = nullptr;   // nvinfer1::IExecutionContext*
    std::vector<uint8_t> engineData_;
    std::vector<std::vector<int64_t>> outputShapes_;
};

}  // namespace manifold3::inference
```

- [ ] **Step 1: Convert the dummy ONNX to an engine on the device**

When device access is re-allowed:

```bash
scp -i config/manifold3_id_rsa build/dummy_yolo11_seg.onnx dji@192.168.42.120:~/vision-detect/
ssh -i config/manifold3_id_rsa dji@192.168.42.120 \
  "cd ~/vision-detect && /usr/src/tensorrt/bin/trtexec --onnx=dummy_yolo11_seg.onnx --saveEngine=dummy_yolo11_seg.engine --fp16 2>&1 | tail -5"
```

Expected: `[I] Engine built in ...` and `dummy_yolo11_seg.engine` exists on the device. Copy the engine back to `build/` for host-side storage of the artifact (optional; not committed).

- [ ] **Step 2: Write the smoke runner first (fails before the wrapper exists)**

Create `scripts/run_inference_smoke.sh`:

```bash
#!/usr/bin/env bash
# Target-side smoke test: loads the dummy engine and runs one inference.
# Usage: scripts/run_inference_smoke.sh <manifold3-ip> [--no-build]
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SSH_KEY="${REPO_ROOT}/config/manifold3_id_rsa"
REMOTE_DIR="~/vision-detect"
REMOTE_BIN="${REMOTE_DIR}/inference_smoke"
SSH_OPTS=(-i "${SSH_KEY}" -o StrictHostKeyChecking=no -o ConnectTimeout=10)

TARGET_IP="$1"
DO_BUILD=true
[ "${2:-}" = "--no-build" ] && DO_BUILD=false

if [ "${DO_BUILD}" = true ]; then
    source "${REPO_ROOT}/scripts/setup_env.sh"
    cmake --build "${REPO_ROOT}/build-cross" --target inference_smoke -j"$(nproc)"
fi

scp "${SSH_OPTS[@]}" "${REPO_ROOT}/build-cross/src/inference/inference_smoke" "dji@${TARGET_IP}:${REMOTE_BIN}"
scp "${SSH_OPTS[@]}" "${REPO_ROOT}/build/dummy_yolo11_seg.engine" "dji@${TARGET_IP}:${REMOTE_DIR}/dummy_yolo11_seg.engine"
ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "chmod +x ${REMOTE_BIN} && ${REMOTE_BIN} ${REMOTE_DIR}/dummy_yolo11_seg.engine"
```

- [ ] **Step 3: Write the engine wrapper implementation**

`src/inference/tensorrt_engine.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace manifold3 {
namespace inference {

// RAII wrapper around the TensorRT runtime, engine, and execution context.
// Serialized engines are device-bound; load only engines converted on the
// same TensorRT version and GPU.
class TensorRtEngine {
  public:
    TensorRtEngine() = default;
    ~TensorRtEngine();

    TensorRtEngine(const TensorRtEngine &) = delete;
    TensorRtEngine &operator=(const TensorRtEngine &) = delete;

    bool Load(const std::string &engine_path);

    // Runs inference on one NCHW float input into the YOLO11-seg outputs.
    // Output vectors are resized to the engine's tensor shapes.
    bool Infer(const std::vector<float> &input, std::vector<float> *out0, std::vector<float> *out1,
               std::vector<float> *out2, int64_t *latency_us = nullptr);

  private:
    void *runtime_ = nullptr;
    void *engine_ = nullptr;
    void *context_ = nullptr;
    std::vector<uint8_t> engine_data_;
    bool loaded_ = false;
};

}  // namespace inference
}  // namespace manifold3
```

`src/inference/tensorrt_engine.cpp`:

```cpp
#include "inference/tensorrt_engine.h"

#include <NvInfer.h>
#include <cuda_runtime.h>

#include <chrono>
#include <cstdio>
#include <fstream>

namespace manifold3 {
namespace inference {

namespace {

using namespace nvinfer1;

class Logger : public ILogger {
  public:
    void log(Severity severity, const char *msg) noexcept override {
        if (severity <= Severity::kWARNING) {
            std::fprintf(stderr, "[TRT] %s\n", msg);
        }
    }
};

Logger g_logger;

std::vector<uint8_t> ReadFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return {};
    }
    const std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char *>(data.data()), size);
    return data;
}

}  // namespace

TensorRtEngine::~TensorRtEngine() {
    if (context_ != nullptr) {
        static_cast<IExecutionContext *>(context_)->destroy();
    }
    if (engine_ != nullptr) {
        static_cast<ICudaEngine *>(engine_)->destroy();
    }
    if (runtime_ != nullptr) {
        static_cast<IRuntime *>(runtime_)->destroy();
    }
}

bool TensorRtEngine::Load(const std::string &engine_path) {
    if (loaded_) {
        return true;
    }
    engine_data_ = ReadFile(engine_path);
    if (engine_data_.empty()) {
        std::fprintf(stderr, "TensorRtEngine: failed to read engine file %s\n", engine_path.c_str());
        return false;
    }
    runtime_ = createInferRuntime(g_logger);
    if (runtime_ == nullptr) {
        std::fprintf(stderr, "TensorRtEngine: createInferRuntime failed\n");
        return false;
    }
    engine_ = static_cast<IRuntime *>(runtime_)->deserializeCudaEngine(engine_data_.data(), engine_data_.size());
    if (engine_ == nullptr) {
        std::fprintf(stderr, "TensorRtEngine: deserializeCudaEngine failed\n");
        return false;
    }
    context_ = static_cast<ICudaEngine *>(engine_)->createExecutionContext();
    if (context_ == nullptr) {
        std::fprintf(stderr, "TensorRtEngine: createExecutionContext failed\n");
        return false;
    }
    loaded_ = true;
    return true;
}

bool TensorRtEngine::Infer(const std::vector<float> &input, std::vector<float> *out0, std::vector<float> *out1,
                           std::vector<float> *out2, int64_t *latency_us) {
    if (!loaded_) {
        std::fprintf(stderr, "TensorRtEngine::Infer called before Load\n");
        return false;
    }
    ICudaEngine *engine = static_cast<ICudaEngine *>(engine_);
    IExecutionContext *context = static_cast<IExecutionContext *>(context_);

    const char *inputName = nullptr;
    std::vector<const char *> outputNames;
    for (int32_t i = 0; i < engine->getNbIOTensors(); ++i) {
        const char *name = engine->getIOTensorName(i);
        if (engine->getTensorIOMode(name) == TensorIOMode::kINPUT) {
            inputName = name;
        } else {
            outputNames.push_back(name);
        }
    }
    if (inputName == nullptr || outputNames.size() != 3) {
        std::fprintf(stderr, "TensorRtEngine: expected 1 input and 3 outputs, got %zu\n", outputNames.size());
        return false;
    }

    const Dims inDims = engine->getTensorShape(inputName);
    int64_t inSize = 1;
    for (int32_t d = 0; d < inDims.nbDims; ++d) {
        inSize *= inDims.d[d];
    }
    if (static_cast<int64_t>(input.size()) != inSize) {
        std::fprintf(stderr, "TensorRtEngine: input size %zu != engine input %lld\n", input.size(),
                     static_cast<long long>(inSize));
        return false;
    }

    std::vector<std::vector<float> *> outs = {out0, out1, out2};
    std::vector<void *> deviceBufs(1 + outputNames.size(), nullptr);
    const int64_t inBytes = inSize * sizeof(float);
    if (cudaMalloc(&deviceBufs[0], inBytes) != cudaSuccess) {
        std::fprintf(stderr, "TensorRtEngine: cudaMalloc input failed\n");
        return false;
    }
    for (size_t i = 0; i < outputNames.size(); ++i) {
        const Dims dims = engine->getTensorShape(outputNames[i]);
        int64_t size = 1;
        for (int32_t d = 0; d < dims.nbDims; ++d) {
            size *= dims.d[d];
        }
        outs[i]->resize(static_cast<size_t>(size));
        if (cudaMalloc(&deviceBufs[1 + i], size * sizeof(float)) != cudaSuccess) {
            std::fprintf(stderr, "TensorRtEngine: cudaMalloc output %zu failed\n", i);
            for (void *p : deviceBufs) {
                if (p != nullptr) {
                    cudaFree(p);
                }
            }
            return false;
        }
    }

    cudaStream_t stream = nullptr;
    cudaStreamCreate(&stream);
    cudaMemcpyAsync(deviceBufs[0], input.data(), inBytes, cudaMemcpyHostToDevice, stream);
    for (size_t i = 0; i < outputNames.size(); ++i) {
        context->setTensorAddress(outputNames[i], deviceBufs[1 + i]);
    }
    context->setTensorAddress(inputName, deviceBufs[0]);

    const auto start = std::chrono::steady_clock::now();
    const bool enqueued = context->enqueueV3(stream, nullptr);
    cudaStreamSynchronize(stream);
    const auto end = std::chrono::steady_clock::now();
    if (latency_us != nullptr) {
        *latency_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }

    for (size_t i = 0; i < outputNames.size(); ++i) {
        const Dims dims = engine->getTensorShape(outputNames[i]);
        int64_t size = 1;
        for (int32_t d = 0; d < dims.nbDims; ++d) {
            size *= dims.d[d];
        }
        cudaMemcpyAsync(outs[i]->data(), deviceBufs[1 + i], size * sizeof(float), cudaMemcpyDeviceToHost, stream);
    }
    cudaStreamSynchronize(stream);
    cudaStreamDestroy(stream);
    for (void *p : deviceBufs) {
        if (p != nullptr) {
            cudaFree(p);
        }
    }
    if (!enqueued) {
        std::fprintf(stderr, "TensorRtEngine: enqueueV3 failed\n");
        return false;
    }
    return true;
}

}  // namespace inference
}  // namespace manifold3
```

- [ ] **Step 4: Add the smoke executable and cross-only sources to CMake**

Modify `src/inference/CMakeLists.txt` to:

```cmake
add_library(inference STATIC
    preprocess.cpp
    postprocess.cpp
)

target_include_directories(inference PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

# TensorRT engine wrapper: cross-compile only (no TensorRT on host).
if(CMAKE_CROSSCOMPILING)
    target_sources(inference PRIVATE tensorrt_engine.cpp)
    target_include_directories(inference PRIVATE
        ${CMAKE_SYSROOT}/usr/include/aarch64-linux-gnu
        ${CMAKE_SYSROOT}/usr/local/cuda/include
    )
    target_link_directories(inference PRIVATE
        ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu
        ${CMAKE_SYSROOT}/usr/local/cuda/lib64
    )
    target_link_libraries(inference PRIVATE nvinfer cudart)
endif()

# Target-side smoke executable for the dummy engine.
if(CMAKE_CROSSCOMPILING)
    add_executable(inference_smoke inference_smoke.cpp)
    target_include_directories(inference_smoke PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SYSROOT}/usr/include/aarch64-linux-gnu
        ${CMAKE_SYSROOT}/usr/local/cuda/include
    )
    target_link_directories(inference_smoke PRIVATE
        ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu
        ${CMAKE_SYSROOT}/usr/local/cuda/lib64
    )
    target_link_libraries(inference_smoke PRIVATE inference nvinfer cudart)
endif()
```

Create `src/inference/inference_smoke.cpp`:

```cpp
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "inference/tensorrt_engine.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: inference_smoke <engine.engine>\n");
        return 2;
    }
    manifold3::inference::TensorRtEngine engine;
    if (!engine.Load(argv[1])) {
        return 1;
    }
    std::vector<float> input(1 * 3 * 1280 * 1280, 0.5f);
    std::vector<float> out0, out1, out2;
    int64_t latencyUs = 0;
    if (!engine.Infer(input, &out0, &out1, &out2, &latencyUs)) {
        return 1;
    }
    std::printf("inference smoke PASS: out0=%zu out1=%zu out2=%zu latency_us=%lld\n", out0.size(), out1.size(),
                out2.size(), static_cast<long long>(latencyUs));
    return 0;
}
```

- [ ] **Step 5: Cross-compile**

```bash
source scripts/setup_env.sh && cmake --preset manifold3-cross-release && cmake --build --preset manifold3-cross-release
```

Expected: builds cleanly (needs Task 1 sysroot dev files; `-lnvinfer` and `-lcudart` resolve). If the linker reports missing GLIBCXX versions from the toolchain's static libstdc++, note it in `docs/build-environment.md` and revisit only if target runtime shows a real problem (AGENTS.md constraint).

- [ ] **Step 6: Run the smoke on the target**

```bash
scripts/run_inference_smoke.sh 192.168.42.120
```

Expected: `inference smoke PASS: out0=1100800 out1=819200 out2=819200 latency_us=<value>` (43*25600, 32*25600, 32*160*160).

- [ ] **Step 7: Commit**

```bash
git add src/inference/ scripts/run_inference_smoke.sh
git commit -m "feat: add TensorRT engine wrapper and target smoke test"
```

---

### Task 5: Wire capture -> inference in the app with per-second stats

**Files:**
- Modify: `src/capture/liveview_capture.h`, `src/capture/liveview_capture.cpp` (add bounded latest-wins frame sink)
- Modify: `src/app/main.cpp`
- Modify: `src/app/CMakeLists.txt` (link `inference`)

**Interfaces:**
- Consumes: `LiveviewCapture::Get()` (Phase 4), `TensorRtEngine`, `PreprocessNv12ToNchw`, `DecodeYolo11Seg`, and the dummy engine path.
- Produces: continuous detection stats printed every second: frames processed, mean/95th-percentile inference latency, detections per frame, dropped frames, RSS.

- [ ] **Step 1: Add a bounded latest-wins frame sink to `LiveviewCapture`**

Modify `src/capture/liveview_capture.h` — add `#include <vector>` and these members:

```cpp
    // Copies the latest NV12 frame into out; returns false if none is
    // available. The buffer is owned by the caller and the latest-wins slot
    // is cleared, so a slow consumer never accumulates memory.
    bool TakeFrame(std::vector<uint8_t> *out, uint32_t *width, uint32_t *height);
    // ...
  private:
    std::mutex frameMutex_;
    std::vector<uint8_t> latestFrame_;
    uint32_t latestWidth_ = 0;
    uint32_t latestHeight_ = 0;
```

Modify `src/capture/liveview_capture.cpp` — in `OnImage`, after the stats update, copy the buffer into the latest-wins slot:

```cpp
    // Copy the buffer into the single latest-wins slot; the PSDK buffer is
    // only valid during the callback.
    {
        std::lock_guard<std::mutex> lock(capture.frameMutex_);
        capture.latestFrame_.assign(buf, buf + len);
        capture.latestWidth_ = imageInfo.width;
        capture.latestHeight_ = imageInfo.height;
    }
```

And implement `TakeFrame`:

```cpp
bool LiveviewCapture::TakeFrame(std::vector<uint8_t> *out, uint32_t *width, uint32_t *height) {
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (latestFrame_.empty()) {
        return false;
    }
    *out = std::move(latestFrame_);
    latestFrame_.clear();
    *width = latestWidth_;
    *height = latestHeight_;
    return true;
}
```

- [ ] **Step 2: Rewrite `main.cpp` to run capture -> preprocess -> infer -> postprocess**

Full file:

```cpp
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "capture/liveview_capture.h"
#include "core/psdk_lifecycle.h"
#include "inference/inference_types.h"
#include "inference/postprocess.h"
#include "inference/preprocess.h"
#include "inference/tensorrt_engine.h"

namespace {
volatile std::sig_atomic_t g_stopRequested = 0;

void OnStopSignal(int signalNum) {
    (void)signalNum;
    g_stopRequested = 1;
}

long ReadRssKb() {
    std::FILE *statusFile = std::fopen("/proc/self/status", "r");
    if (statusFile == nullptr) {
        return -1;
    }
    char line[256];
    long rssKb = -1;
    while (std::fgets(line, sizeof(line), statusFile) != nullptr) {
        if (std::sscanf(line, "VmRSS: %ld kB", &rssKb) == 1) {
            break;
        }
    }
    std::fclose(statusFile);
    return rssKb;
}
}  // namespace

int main(int argc, char **argv) {
    std::signal(SIGINT, OnStopSignal);
    std::signal(SIGTERM, OnStopSignal);

    const std::string enginePath =
        argc > 1 ? argv[1] : std::string("/home/dji/vision-detect/dummy_yolo11_seg.engine");

    auto &lifecycle = manifold3::PsdkLifecycle::Get();
    auto &capture = manifold3::LiveviewCapture::Get();
    manifold3::inference::TensorRtEngine engine;

    if (!lifecycle.Initialize()) {
        std::fprintf(stderr, "PSDK initialization failed\n");
        return 1;
    }
    if (!capture.Initialize()) {
        std::fprintf(stderr, "liveview init failed\n");
        lifecycle.Shutdown();
        return 1;
    }
    if (!lifecycle.Start()) {
        std::fprintf(stderr, "PSDK application start failed\n");
        capture.Shutdown();
        lifecycle.Shutdown();
        return 1;
    }
    if (!capture.Start()) {
        std::fprintf(stderr, "capture start failed\n");
        capture.Shutdown();
        lifecycle.Shutdown();
        return 1;
    }
    if (!engine.Load(enginePath)) {
        std::fprintf(stderr, "engine load failed: %s\n", enginePath.c_str());
        capture.Shutdown();
        lifecycle.Shutdown();
        return 1;
    }
    std::printf("PSDK + capture + engine ready\n");

    uint64_t inferenceCount = 0;
    int64_t latencySumUs = 0;
    int64_t latencyMaxUs = 0;
    std::vector<int64_t> latencySamples;
    uint64_t detectionsTotal = 0;
    auto lastReport = std::chrono::steady_clock::now();

    while (!g_stopRequested) {
        std::vector<uint8_t> frame;
        uint32_t w = 0, h = 0;
        if (!capture.TakeFrame(&frame, &w, &h)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        std::vector<float> nchw;
        if (!manifold3::inference::PreprocessNv12ToNchw(frame.data(), w, h,
                                                        manifold3::inference::kInputSize,
                                                        manifold3::inference::kInputSize, &nchw)) {
            std::fprintf(stderr, "preprocess failed\n");
            continue;
        }
        std::vector<float> out0, out1, out2;
        int64_t latencyUs = 0;
        if (!engine.Infer(nchw, &out0, &out1, &out2, &latencyUs)) {
            std::fprintf(stderr, "infer failed\n");
            continue;
        }
        constexpr uint32_t kChannels =
            4 + manifold3::inference::kNumSpecies + manifold3::inference::kNumAgeBins + 32;
        const uint32_t anchors = static_cast<uint32_t>(out0.size() / kChannels);
        std::vector<manifold3::inference::Detection> dets;
        manifold3::inference::DecodeYolo11Seg(out0, out1, out2, anchors, 160, 160, &dets);

        ++inferenceCount;
        latencySumUs += latencyUs;
        latencyMaxUs = std::max(latencyMaxUs, latencyUs);
        latencySamples.push_back(latencyUs);
        detectionsTotal += dets.size();

        const auto now = std::chrono::steady_clock::now();
        if (now - lastReport >= std::chrono::seconds(1)) {
            std::sort(latencySamples.begin(), latencySamples.end());
            const int64_t p95 = latencySamples.empty()
                                    ? 0
                                    : latencySamples[static_cast<size_t>(latencySamples.size() * 0.95f)];
            std::printf("infer frames=%llu avg_us=%lld p95_us=%lld max_us=%lld dets=%llu rss_kb=%ld\n",
                        static_cast<unsigned long long>(inferenceCount),
                        static_cast<long long>(inferenceCount ? latencySumUs / static_cast<int64_t>(inferenceCount)
                                                              : 0),
                        static_cast<long long>(p95), static_cast<long long>(latencyMaxUs),
                        static_cast<unsigned long long>(detectionsTotal), ReadRssKb());
            lastReport = now;
            latencySamples.clear();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    capture.Shutdown();
    lifecycle.Shutdown();
    std::printf("PSDK deinitialized\n");
    return 0;
}
```

- [ ] **Step 3: Link `inference` into the app**

Modify `src/app/CMakeLists.txt` — the app references `TensorRtEngine` directly. Task 4's `inference` static library carries NO link requirements (its TRT deps are only on `inference_smoke`), so the app must resolve the full TensorRT closure itself: `nvinfer`, `cudart`, `cublas`, `-l:libcudnn.so.8`, plus the tegra link dir (`libnvdla_compiler.so`) and rpath-link options that Task 4's `inference_smoke` used (see `src/inference/CMakeLists.txt` after Task 4 for the exact working pattern):

```cmake
if(CMAKE_CROSSCOMPILING)
    target_link_directories(manifold3_vision_detect PRIVATE
        ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu
        ${CMAKE_SYSROOT}/usr/local/cuda/lib64
        ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/tegra
    )
    target_link_options(manifold3_vision_detect PRIVATE
        "-Wl,-rpath-link,${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu"
        "-Wl,-rpath-link,${CMAKE_SYSROOT}/usr/local/cuda/lib64"
        "-Wl,-rpath-link,${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/tegra"
    )
endif()

target_link_libraries(manifold3_vision_detect PRIVATE
    capture
    core
    platform
    inference
    ${CMAKE_SOURCE_DIR}/third_party/psdk/psdk_lib/lib/aarch64-linux-gnu-gcc/libpayloadsdk.a
    nvinfer
    cudart
    cublas
    "-l:libcudnn.so.8"
    m
    dl
)
```

(If Task 4 ended up putting the closure on `inference` PUBLIC instead, mirror that exact CMake instead — the rule is: the app link must resolve every undefined TensorRT/CUDA symbol, verified by a clean link with no `--allow-shlib-undefined`.)

- [ ] **Step 4: Cross-compile and run on the target**

```bash
source scripts/setup_env.sh && cmake --preset manifold3-cross-release && cmake --build --preset manifold3-cross-release
scripts/deploy.sh 192.168.42.120 --no-build run -- /home/dji/vision-detect/dummy_yolo11_seg.engine
```

Expected: per-second lines of the form `infer frames=... avg_us=... p95_us=... max_us=... dets=... rss_kb=...` with stable RSS. Latency will be small (dummy model) but validates the full chain.

- [ ] **Step 5: Commit**

```bash
git add src/capture/ src/app/
git commit -m "feat: connect capture to inference with bounded frame handoff"
```

---

### Task 6: Continuous-run validation and Phase 5 record

**Files:**
- Modify: `.agents/docs/plan.md` (Phase 5 target validation record)

- [ ] **Step 1: Run the full app on the target for 5+ minutes**

```bash
scripts/deploy.sh 192.168.42.120 --no-build run -- /home/dji/vision-detect/dummy_yolo11_seg.engine
```

Let it run ≥ 300 s, capture output. Record: frames processed, avg/p95/max inference latency, detections, dropped frames (capture), RSS trend (must be bounded).

- [ ] **Step 2: Record the validation evidence in `.agents/docs/plan.md`**

Add a "Target Validation Record (Phase 5)" subsection under Phase 5 with the measured numbers, the dummy-engine caveat, and the note that real-model swap is pending the trained YOLO11-seg model (separate PC-side plan).

- [ ] **Step 3: Commit**

```bash
git add .agents/docs/plan.md
git commit -m "docs: record phase 5 inference pipeline target validation"
```

---

## Self-Review Notes

- Spec coverage: precision selection (FP16) is baked into Task 2's `--fp16` conversion and Task 4's engine load; result schema independence is enforced by `inference_types.h`; bounded memory is the latest-wins frame sink (Task 5) plus measured RSS (Task 6); the 33 ms budget check is Task 6's latency recording. The PC-side training workflow (annotation, ultralytics training, real ONNX export) is deliberately out of scope here — it is a separate plan pending the species list and dataset (spec Open Items).
- Placeholder scan: no TBD/TODO in code steps; `kNumSpecies`/`kNumAgeBins` are documented spec Open Items with defined current values.
- Type consistency: `Detection` fields (`species_id`, `age_class_id`, `confidence`, `cx/cy/w/h`, `mask_rle`) are identical across `inference_types.h`, `postprocess.h`, Task 3 tests, and Task 5 usage. `TensorRtEngine::Infer` signature is stable across Tasks 4 and 5. `DecodeYolo11Seg` anchor parameter is derived from tensor shape at the call site, matching Task 2's dummy output layout.
