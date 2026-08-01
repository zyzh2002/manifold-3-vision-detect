# Implementation Plan

This plan fixes only decisions that are required to reach the next working system. Detailed implementation choices are
made after the preceding milestone produces target evidence.

## Phase 1: Scaffold and Baseline Documentation [DONE]

- [x] Create the repository structure.
- [x] Pin DJI Payload SDK 3.16.0 as a read-only submodule.
- [x] Document the Manifold 3, Matrice 4E, JetPack, toolchain, and sysroot baselines.
- [x] Separate confirmed constraints from deferred implementation decisions.

## Phase 2: Reproducible Target Build [DONE]

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
- [x] Run the target program on Manifold 3.

### Exit Criteria

- The build does not resolve target headers or libraries from host x86_64 paths.
- The generated ELF is AArch64 and starts on the target firmware.
- Any difference between the standard r35.5.0 sysroot and Manifold 3 is recorded before adding an overlay.

### Target Validation Record

- C and C++ smoke binaries run on Manifold 3 and report PASS.
- Target environment matches the baseline: Jetson Linux R35.5.0, kernel 5.10.192-tegra, glibc 2.31,
  CUDA 11.4.19, TensorRT 8.5.2, cuDNN 8.6.0.
- Dynamic dependencies resolve against device libraries; required `GLIBC_2.17` is available.
- No sysroot overlay is required.

## Phase 3: Minimal PSDK and DPK Application [DONE]

### Outcome

Start a minimal Payload SDK application on Manifold 3 and exercise its complete install and lifecycle path.

### Work

- [x] Port the required Linux OSAL, socket, filesystem, logging, and Manifold 3 USB Bulk handlers from the PSDK
  samples into `src/platform/`.
- [x] Add the minimal PSDK lifecycle under `src/core/`.
- [x] Link PSDK 3.16.0 with the smallest required system libraries.
- [x] Add a minimal application entry point under `src/app/`.
- [x] Add a development `app.json` without committing credentials. The file is generated from `src/app/app.json.in`
  by CMake so `user_app_id` always matches the compiled-in application ID.
- [x] Use the PSDK-provided `build_dpk.sh` to generate the development package.
- [x] Add `scripts/deploy.sh` for direct binary deployment and foreground runs during target debugging, so
      iteration does not require a DPK install per change.
- [x] Generate the development DPK via `build_dpk.sh`. The full install, start, stop, update, and uninstall
      lifecycle is deferred to Phase 6: it requires the DJI Pilot 2 developer workflow and real developer
      credentials, which are not available in Phase 3.

### Exit Criteria

- PSDK initializes and connects to the aircraft. (Pending real DJI developer credentials; the placeholder
  credential path rejects with a clear error and exit code 1.)
- Logs are available through direct foreground deployment (`scripts/deploy.sh run`).
- DPK install/start/stop/update/uninstall lifecycle verification is deferred to Phase 6; development
  iteration does not require a DPK install.

### Target Validation Record (Phase 3)

- The cross-compiled AArch64 binary runs on Manifold 3.
- Platform handler registration (OSAL, logger console, USB Bulk, socket, filesystem) succeeds.
- USB FunctionFS channels are active on the device (`/dev/usb-ffs/bulk2`..`bulk5` mounted).
- Placeholder credentials are rejected with a descriptive error and exit code 1, as designed.
- Development DPK package builds successfully (`manifold3-vision-detect_v01.00.00.00.dpk`).

## Phase 4: Single-Stream Video Capture

### Outcome

Receive one Matrice 4E visible-light stream and expose bounded, owned frames to a consumer.

### Work

- [x] Initialize PSDK Liveview after core initialization.
- [x] Validate `DjiLiveview_StartImageStream()` with NV12 output on Manifold 3.
- [ ] Define frame ownership, metadata, bounded buffering, drop behavior, and shutdown behavior from observed callback
  timing.
- [ ] Record frame dimensions, format, frame rate, drop count, latency, CPU use, and memory growth.
- [ ] Validate H.264 capture separately for recording and fallback capability.

### Target Validation Record (Phase 4)

- `DjiLiveview_StartImageStream()` on `DJI_LIVEVIEW_CAMERA_POSITION_NO_1` / `DJI_LIVEVIEW_CAMERA_SOURCE_M4E_VIS`
  with `PIXFMT_NV12` delivers a stable 30 fps stream at 1440x1080 (2,332,800 bytes/frame).
- 0 dropped frames over 1251 observed frames; `frameId` stays contiguous.
- Callback interval: min 1.8 ms (burst), avg 33.3 ms, max 42.6 ms.
- Process RSS settles at ~73 MB with no unbounded growth over the observation window.
- Preconditions on target: stop `Smart3DExplore` (`dji_app_ctl stop Smart3DExplore`) before running, otherwise the
  local channel bind fails with "Address already in use".
- Note: the device identifies the aircraft as "Matrice 4T" from its base info query; the M4E visible-light source
  enum still selects the visible stream as intended.

### Exit Criteria

- A stable visible-light NV12 stream reaches a test consumer.
- Callback buffers are not retained after callback return.
- Backpressure has an explicit bounded policy.
- Capture stops cleanly without use-after-free or unbounded memory growth.

Infrared streaming, simultaneous streams, and source combinations are capability tests performed only after the
single-stream path is stable.

## Phase 5: TensorRT Inference [IN PROGRESS]

### Phase 5A: Synthetic Device Pipeline [DONE]

- [x] CUDA/TensorRT/cuDNN sysroot extension (device-derived, scripted, staged)
- [x] CPU NV12 preprocessing (1440x1080 -> 1280x1280 NCHW)
- [x] Synthetic FP16 engine loading with by-name contract enforcement
- [x] Synthetic single-frame inference smoke on Manifold 3
- [x] Synthetic continuous pipeline validation (capture -> preprocess -> infer -> decode)
- [x] Validated frame handoff with source/handoff drop accounting
- [x] Provisional Detection schema (species/age/box/mask-RLE)

### Target Validation Record (Phase 5A)

- Continuous capture->preprocess->infer->postprocess runs on Manifold 3 at ~21 fps with avg inference latency
  ~1.4 ms and p95 ~1.7 ms (synthetic dummy engine, 1280x1280 input, FP16).
- detections=0 is expected: the dummy engine emits random-weight outputs below the 0.25 confidence threshold.
- RSS stable at ~630 MB over 5 min with slow ~0.5-1 MB/min creep attributed to driver/SDK lazy allocation; no
  application leak path identified.
- Host unit tests (inference preprocess/postprocess) pass 2/2; cross build clean with full TensorRT closure
  (nvinfer, cudart, cublas, libcudnn.so.8, tegra rpath-link).
- Real-model validation pending the trained YOLO11-seg model (separate PC-side training plan; spec Open Items:
  species list, dataset size).

The ~21 fps and ~1.4 ms latency figures above were measured with the synthetic dummy engine only and do not
predict real-model performance. detections=0 in the synthetic run does not validate detection correctness.
The hardened metrics now report per-window preprocess/engine/postprocess/end-to-end stages.

Hardened metrics (2026-08-01): 658 windows over ~11 min on Manifold 3 at ~30 fps (27.5 first window, 30.0
steady). Per-window stage latencies avg/p95: pre 12.8/13.0 ms, h2d 6.6/6.8 ms, exec 1.26/1.3 ms, d2h 4.4/4.5 ms,
eng 12.2/12.9 ms, post 0.15/0.16 ms; e2e avg ~26.2 ms, p95 ~27.3 ms, max ~30 ms. source_drop=0 and invalid=0 in
all 658 windows; handoff_drop=3 per window (constant). RSS 614,588 -> 634,132 kB over the run (~1.8 MB/min creep,
consistent with the earlier driver/SDK lazy-allocation observation). Clean SIGTERM shutdown verified (process
gone). All data from the synthetic dummy engine; does not predict real-model performance.

### Phase 5B: Real Model [PENDING]

- [ ] Freeze the real YOLO11-seg model ABI (standard 2-output or custom multi-task contract)
- [ ] Match PC and device preprocessing (resize, padding, color convention)
- [ ] Implement inverse-letterbox geometry for boxes
- [ ] Implement source-frame instance masks (crop, upscale, unpad)
- [ ] Compare TensorRT outputs numerically against ONNX Runtime
- [ ] Measure real-model latency, throughput, memory, and drops

Note: the hardened metrics recorded under Phase 5A (2026-08-01) measure the synthetic dummy engine only; 5B
must re-measure all stage latencies, throughput, RSS, and drops against the real model before Phase 5 is DONE.

### Exit Criteria

- Continuous inference runs on Manifold 3 with bounded memory.
- The result schema is independent of PSDK callback internals.
- Performance measurements identify whether additional acceleration work is justified.

The exit criteria above remain the Phase 5 exit criteria; the Phase 5B real-model ABI, geometry, and mask
items must be satisfied before the phase can be marked DONE.

## Phase 6: Product Output and Packaging

### Outcome

Select the required product output, audit dependencies, and produce a release candidate DPK.

### Work

- [ ] Select result output based on the product workflow: local structured result, Pilot AI metadata, processed video,
  or another verified transport.
- [ ] Implement only the selected output path.
- [ ] Classify every runtime dependency as firmware-provided, statically linked, or packaged application data.
- [ ] Add `scripts/package_dpk.sh` as the repository release wrapper around the PSDK packaging tool.
      (The development DPK already builds via `build_dpk.sh` since Phase 3.)
- [ ] Verify DPK install, start, stop, update, uninstall, logs, and data cleanup. This includes the
      lifecycle verification deferred from Phase 3 and requires the DJI Pilot 2 developer workflow and
      real developer credentials.
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
