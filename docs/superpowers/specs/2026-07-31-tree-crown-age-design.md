# Tree Crown Age Estimation Design

## Goal

Detect tree crowns from fixed-altitude drone imagery and estimate tree age. The first
release classifies age into 5-year bins; a regression head is added later once the
classification model is stable and enough samples are collected.

## Background and Constraints

- Input: single visible-light NV12 stream at 1440x1080, 30 fps from the Matrice 4E
  (Phase 4 validated on Manifold 3).
- Acquisition: the drone flies at a fixed altitude with a top-down view, so crown
  pixel area is scale-consistent and can be calibrated to physical area.
- Tree species: a small set (2-5 species). The model must classify species alongside
  detection because crown-area-to-age relationships differ strongly by species.
- Tree distribution: crowns partially overlap but individual boundaries are still
  distinguishable; per-tree segmentation is required.
- Age labels: plantations / sample plots with known planting records provide ground
  truth; labels are converted to 5-year bins (e.g. 0-5, 5-10, 10-15, ...).
- Target device: Manifold 3, JetPack 5.1.3, TensorRT 8.5.2, CUDA 11.4.
- Target hardware: Jetson Orin NX 16 GB, rated 100 TOPS (INT8) across GPU + DLA.

## Precision Selection

Manifold 3's Orin (Ampere) Tensor Cores accelerate FP16 and INT8 strongly (the rated
100 TOPS is INT8); FP32 runs on CUDA cores without Tensor Core acceleration. The
per-frame budget is 33 ms at 30 fps.

| Precision | Throughput vs FP32 | Accuracy loss | Decision |
|---|---|---|---|
| FP32 | 1x (baseline) | None | Not chosen: leaves Tensor Core performance unused |
| **FP16** | ~2x | Usually <0.5% mAP | **Chosen**: negligible loss for YOLO-style detection, default embedded choice |
| INT8 | ~3-4x | ~1-2% mAP | Deferred: requires a calibration set and is only worth it if measured latency exceeds the budget (decision triggers in `docs/plan.md`) |

Training precision is decoupled from inference precision: train on PC with FP32/AMP,
verify the exported ONNX with onnxruntime (FP32 baseline), then select FP16 at engine
conversion time on the device.

## Model Approach

Chosen approach: single YOLO11-seg instance-segmentation model with a multi-task head.

Per detected crown, the model outputs:

| Output | Type | Notes |
|---|---|---|
| Crown mask | pixel-level instance mask | Handles partially overlapping crowns; mask area is the crown-area feature for the later regression head |
| Species | classification | 2-5 species classes |
| Age class | classification | 5-year bins, one class per bin |
| Confidence | scalar | Detection confidence |

Alternatives considered and rejected:

- Two-stage (detect + crop classify): decouples stages but adds pipeline latency,
  accumulates errors, and axis-aligned boxes fit partially overlapping crowns poorly.
- Detection + crown-area empirical table: simplest, but unreliable under overlap and
  empirical formulas have large error; not the main path.

### Training (PC side)

- Training GPU: RTX 2070 Super (8 GB GDDR6, Turing) is sufficient for YOLO11s-seg at
  1280x1280 with batch 4-8 and AMP mixed precision (ultralytics default). If VRAM is
  tight, start at 640x640 and fine-tune at 1280x1280.
- Annotate with polygon masks + species + age bin (CVAT or LabelMe).
- Train with ultralytics YOLO11-seg with a customized multi-task head.
- Input resolution: 1280x1280 (better for small crowns and overlap than 640).
- Inference precision: FP16.
- Export: fixed-shape ONNX at 1280x1280, then convert to a TensorRT engine on the
  device with `trtexec --onnx=model.onnx --saveEngine=model.engine --fp16`. Engine
  conversion runs on the device (TensorRT 8.5.2); the training GPU is not involved.

## Device-Side Integration (Phase 5, `src/inference/`, C++17)

Pipeline:

```
NV12 1440x1080 frame (from src/capture/)
  -> preprocess: NV12->RGB, resize to 1280x1280, normalize
     (CPU first; switch to VPI/custom .cu only if latency budget is exceeded)
   -> TensorRT inference (deserializeCudaEngine + enqueueV3 with setTensorAddress, FP16)
  -> postprocess: mask prototype decoding + confidence threshold + NMS
  -> detections[]
```

Result schema (independent of PSDK callback internals):

```cpp
struct Detection {
    uint16_t species_id;    // tree species class
    uint16_t age_class_id;  // 5-year bin
    float confidence;
    uint16_t cx, cy, w, h;  // normalized bounding box
    std::vector<uint8_t> mask_rle; // run-length encoded mask
};
```

Module boundaries:

- `src/inference/` owns engine loading, preprocessing, inference, postprocessing;
  it is a pure compute path with no PSDK dependency.
- `src/app/main.cpp` connects capture -> inference -> output (output transport is
  selected in Phase 6: local records, Pilot AI metadata, or processed video).
- `src/capture/` stays as-is (Phase 4 deliverable).

## Performance Budget

- Input rate 30 fps -> 33 ms per frame budget.
- YOLO11s-seg FP16 at 1280x1280 estimates 15-25 ms inference on the target, leaving
  headroom for preprocessing.
- If CPU preprocessing exceeds the budget: pipeline capture/inference across two
  threads, or use VPI (decision triggers in `docs/plan.md`).

## Development Order (model training runs in parallel with code)

1. Build a dummy ONNX (fixed 1280x1280 shape) and validate the inference pipeline
   end to end on the device; this unblocks `src/inference/` without a real model.
2. When the real model is ready: convert with `trtexec`, swap the engine, verify
   detection outputs against PC-side onnxruntime results.
3. Continuous inference measurement: latency, throughput, memory, frame drops.
4. Record results in `docs/plan.md` Phase 5 target validation record.

## Regression Upgrade Path

Once the classification model is stable, add a regression head on the same backbone
(crown pixel area as a feature), collecting "crown area vs. actual age" samples to
output continuous age. The 5-year bins keep the first release usable while this is
being collected.

## Open Items

- Final species list and age span (defines the number of species classes and age bins).
- Dataset size target and class balance plan (needs the collected plantation records).
- Model size choice (n/s/m) is finalized after a first training run measures accuracy
  versus latency.
