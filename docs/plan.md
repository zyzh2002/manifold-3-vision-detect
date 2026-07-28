# Implementation Plan

This plan fixes only decisions that are required to reach the next working system. Detailed implementation choices are
made after the preceding milestone produces target evidence.

## Phase 1: Scaffold and Baseline Documentation [DONE]

- [x] Create the repository structure.
- [x] Pin DJI Payload SDK 3.16.0 as a read-only submodule.
- [x] Document the Manifold 3, Matrice 4T, JetPack, toolchain, and sysroot baselines.
- [x] Separate confirmed constraints from deferred implementation decisions.

## Phase 2: Reproducible Target Build [IN PROGRESS: HOST VERIFIED]

### Outcome

Produce a minimal AArch64 program with NVIDIA's Bootlin GCC 9.3 toolchain and the complete Jetson Linux r35.5.0
sysroot, then run it on Manifold 3.

### Work

- [x] Download and record the checksum of the NVIDIA Bootlin GCC 9.3.0 toolchain.
- [x] Build Phase 2 base sysroot from the Jetson Linux r35.5.0 BSP, sample root filesystem,
  and NVIDIA binary overlay. (CUDA/TensorRT/cuDNN development packages are deferred to Phase 5.)
- [x] Add the CMake cross-compilation toolchain configuration.
- [x] Add environment validation for `MANIFOLD3_TOOLCHAIN_DIR` and `MANIFOLD3_SYSROOT`.
- [x] Compile a minimal C and C++ target program.
- [x] Verify ELF architecture, dynamic dependencies, `GLIBC_*`, and `GLIBCXX_*` requirements
  (host-side static checks).
- [ ] Run the target program on Manifold 3.

### Exit Criteria

- The build does not resolve target headers or libraries from host x86_64 paths.
- The generated ELF is AArch64 and starts on the target firmware.
- Any difference between the standard r35.5.0 sysroot and Manifold 3 is recorded before adding an overlay.

## Phase 3: Minimal PSDK and DPK Application

### Outcome

Start a minimal Payload SDK application on Manifold 3 and exercise its complete install and lifecycle path.

### Work

- [ ] Port the required Linux OSAL, socket, filesystem, logging, and Manifold 3 USB Bulk handlers from the PSDK
  samples into `src/platform/`.
- [ ] Add the minimal PSDK lifecycle under `src/core/`.
- [ ] Link PSDK 3.16.0 with the smallest required system libraries.
- [ ] Add a minimal application entry point under `src/app/`.
- [ ] Add a development `app.json` without committing credentials.
- [ ] Use the PSDK-provided `build_dpk.sh` to generate the development package.
- [ ] Build, install, start, stop, update, and uninstall the DPK.

### Exit Criteria

- PSDK initializes and connects to the aircraft.
- Logs are available through the supported Manifold 3 application workflow.
- The DPK lifecycle works before video capture or TensorRT is introduced.

## Phase 4: Single-Stream Video Capture

### Outcome

Receive one Matrice 4T visible-light stream and expose bounded, owned frames to a consumer.

### Work

- [ ] Initialize PSDK Liveview after core initialization.
- [ ] Validate `DjiLiveview_StartImageStream()` with NV12 output on Manifold 3.
- [ ] Define frame ownership, metadata, bounded buffering, drop behavior, and shutdown behavior from observed callback
  timing.
- [ ] Record frame dimensions, format, frame rate, drop count, latency, CPU use, and memory growth.
- [ ] Validate H.264 capture separately for recording and fallback capability.

### Exit Criteria

- A stable visible-light NV12 stream reaches a test consumer.
- Callback buffers are not retained after callback return.
- Backpressure has an explicit bounded policy.
- Capture stops cleanly without use-after-free or unbounded memory growth.

Infrared streaming, simultaneous streams, and source combinations are capability tests performed only after the
single-stream path is stable.

## Phase 5: TensorRT Inference

### Outcome

Feed the captured NV12 frames into a TensorRT 8.5.2 model and produce structured detection results continuously.

### Work

- [ ] Add only the CUDA and TensorRT development packages required by the selected APIs.
- [ ] Define the model input, output, precision, and engine compatibility contract.
- [ ] Implement preprocessing without custom `.cu` files initially.
- [ ] Load a target-compatible TensorRT engine.
- [ ] Run single-frame inference before enabling continuous inference.
- [ ] Measure end-to-end latency, inference time, throughput, memory use, and frame drops.
- [ ] Add postprocessing and a stable detection result interface.

### Exit Criteria

- Continuous inference runs on Manifold 3 with bounded memory.
- The result schema is independent of PSDK callback internals.
- Performance measurements identify whether additional acceleration work is justified.

## Phase 6: Product Output and Packaging

### Outcome

Select the required product output, audit dependencies, and produce a release candidate DPK.

### Work

- [ ] Select result output based on the product workflow: local structured result, Pilot AI metadata, processed video,
  or another verified transport.
- [ ] Implement only the selected output path.
- [ ] Classify every runtime dependency as firmware-provided, statically linked, or packaged application data.
- [ ] Add `scripts/package_dpk.sh` as the repository release wrapper around the PSDK packaging tool.
- [ ] Verify DPK install, start, stop, update, uninstall, logs, and data cleanup.
- [ ] Run the target ELF and dependency release checks.
- [ ] Record supported firmware, aircraft, camera source, model, and performance bounds.

### Exit Criteria

- The selected user-visible output works end to end.
- No unclassified dynamic dependency remains.
- The release candidate passes lifecycle and target compatibility checks.

## Decision Triggers

| Decision | Trigger |
|---|---|
| Add FFmpeg or GStreamer | ImageStream is unavailable or the product must process H.264 as model input. |
| Add OpenCV or VPI | Measured preprocessing complexity or performance justifies the dependency. |
| Compile custom `.cu` files | The initial preprocessing path cannot meet the measured latency target. |
| Use crosstool-ng | NVIDIA's Bootlin toolchain has a documented limitation that blocks the target build. |
| Link `libstdc++` or `libgcc` statically | Target validation demonstrates a real `GLIBCXX_*` compatibility problem. |
| Overlay files from Manifold 3 | A measured ABI or dependency difference exists against the r35.5.0 sysroot. |
| Use DLA, FP16, or INT8 | Model accuracy, throughput, and power tests show a product benefit. |
| Support multiple camera streams | Single-stream capture and inference are stable and hardware capability tests pass. |
| Send AI metadata or processed video to Pilot | The product workflow requires Pilot presentation and the relevant PSDK path is validated. |
