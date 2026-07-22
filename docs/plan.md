# Implementation Plan

## Phase 1 — Scaffold & Docs [DONE]

- [x] Initialize git repository
- [x] Create directory structure with `.gitkeep` placeholders
- [x] Add PSDK git submodule (`dji-sdk/Payload-SDK`, pinned to `3.16.0`)
- [x] Write `.gitignore`
- [x] Write `README.md` (Chinese)
- [x] Write `AGENTS.md` (English, agent instructions)
- [x] Write `docs/plan.md` (this file)
- [x] Write `docs/architecture.md`
- [x] Initial commit

## Phase 2 — Target Baseline + PSDK Port + Video Capture

### 2.1: Target Baseline and Minimal DPK

- [ ] Record the actual Manifold 3 environment:
  - OS, kernel, glibc, native GCC, and supported `GLIBCXX_*` versions
  - CUDA, TensorRT, OpenCV, FFmpeg/GStreamer, and NVIDIA runtime package versions
  - USB Bulk device nodes, permissions, application user, and available storage
- [ ] Create a minimal `config/app.json` with matching application ID and firmware version
- [ ] Build and install a minimal PSDK application as a DPK
- [ ] Verify install, start, stop, logs, USB Bulk connectivity, and uninstall before adding capture code

### 2.2: Toolchain and JetPack Sysroot

- [ ] Install crosstool-ng on host (`apt install crosstool-ng` or build from source)
- [ ] Create `toolchain/crosstool-ng/aarch64-manifold3.config`:
  - Target: `aarch64-unknown-linux-gnu`
  - GCC: `11.5.0`
  - glibc: `2.31`
  - Kernel headers: `5.10`
  - CT_PREFIX_DIR: `$MANIFOLD3_TOOLCHAIN_DIR`
- [ ] Build toolchain: `ct-ng build`
- [ ] Write `toolchain/crosstool-ng/README.md` with build instructions
- [ ] Export or provision a JetPack 5.1.3 target sysroot for CUDA, TensorRT, OpenCV, multimedia, and transitive libraries
- [ ] Document target include/library paths, CMake search roots, and runtime library strategy
- [ ] Verify compiler version, hello-world execution, ELF architecture, glibc symbol versions, and dynamic dependencies on Manifold 3

### 2.3: CMake Build System

- [ ] Write `cmake/toolchain-aarch64.cmake` — cross-compile toolchain file
- [ ] Write `cmake/psdk.cmake` — PSDK include/lib linking
- [ ] Write `cmake/platform.cmake` — host vs manifold3 detection
- [ ] Write top-level `CMakeLists.txt` with subproject integration
- [ ] Keep host tests independent from target-only PSDK, CUDA, and TensorRT libraries through facades/fakes
- [ ] Verify host build and target cross-build independently

### 2.4: Platform Port (PSDK HAL/OSAL)

- [ ] Copy the required Linux common platform sources from `third_party/psdk/samples/sample_c/platform/linux/common/`
- [ ] Copy the Manifold 3-specific USB Bulk HAL from `third_party/psdk/samples/sample_c/platform/linux/manifold3/hal/`
- [ ] Adapt and register OSAL, console/logger, file system, socket, and USB Bulk handlers used by the Manifold 3 sample
- [ ] Add application identity and link configuration without committing credentials
- [ ] Document required device nodes, permissions, and selected hardware connection mode
- [ ] Write `src/platform/CMakeLists.txt`

### 2.5: Core PSDK Lifecycle

- [ ] Write `src/core/psdk_core.h` — lifecycle interface
- [ ] Write `src/core/psdk_core.cpp` with an explicit lifecycle:
  - Register platform handlers
  - Fill user information and call `DjiCore_Init`
  - Set alias, firmware version, and serial number
  - Call `DjiLiveview_Init`, initialize other modules, and apply settings that must precede `DjiCore_ApplicationStart`
  - Call `DjiCore_ApplicationStart` and run
  - Stop streams, unregister Liveview callbacks/labels, call `DjiLiveview_Deinit`, and shut down remaining modules in reverse order
- [ ] Write `src/core/CMakeLists.txt`

### 2.6: Capture Layer

- [ ] Write `src/capture/capture_sink.h` — `ISink` abstract interface:
  - `FileSink`: write frames to `.h264` / `.nv12` / `.rgb` files
  - `RingBufferSink`: bounded frame queue backed by owned/preallocated buffers
  - `CallbackSink`: user-defined callback
- [ ] Write `src/capture/sinks.cpp` — sink implementations
- [ ] Write `src/capture/capture.h` — abstract `Capture` interface:
  - `start(position, source, sink)` / `stop()`
  - Camera sources: `M4T_VIS`, `M4T_IR`
  - Pixel formats: `NV12`, `RGB_PLANAR`, `RGB_PACKED`
- [ ] Write `src/capture/capture_h264.h` / `.cpp` — `DjiLiveview_StartH264Stream` impl
- [ ] Write `src/capture/capture_image.h` / `.cpp` — `DjiLiveview_StartImageStream` impl (Manifold 3 only, decoded NV12/RGB)
- [ ] Define callback buffer ownership, frame metadata, queue capacity, drop policy, and shutdown behavior
- [ ] Start with a correct synchronized queue; consider lock-free storage only after profiling demonstrates a need
- [ ] Write `src/capture/CMakeLists.txt`

### 2.7: Application Entry Point and Capability Validation

- [ ] Write `src/app/main.cpp` — init core → create capture instances → start configured streams → run loop
- [ ] Write `src/app/CMakeLists.txt`
- [ ] Validate single VIS H.264, single IR H.264, and single decoded ImageStream first
- [ ] Validate VIS + IR and H.264 + ImageStream combinations as hardware capability tests rather than assumptions
- [ ] Record resolution, FPS, latency, dropped frames, CPU/GPU usage, memory growth, and reconnect behavior

### 2.8: Build & Deploy

- [ ] Write `scripts/build.sh` — cmake invocation with cross-compile args
- [ ] Write `scripts/deploy.sh` — scp binary + libs to Manifold 3, SSH remote start
- [ ] Add deployment checks for ELF architecture, `ldd`, symbol versions, device nodes, and permissions
- [ ] End-to-end verification: DPK runs on Manifold 3 and captures configured H.264 and decoded frames from M4T

## Phase 3 — On-Device Model Inference

### 3.1: Inference Engine

- [ ] Write `src/inference/inference.h` — feed frame → detections
- [ ] Define ONNX-to-engine build flow, TensorRT/CUDA compatibility matrix, bindings, precision, and plugin requirements
- [ ] Build TensorRT engines for the target JetPack/GPU environment; do not treat `.engine` files as portable artifacts
- [ ] Implement TensorRT/CUDA pipeline:
  - [ ] Load optimized TensorRT engine (`.engine` file)
  - [ ] Preprocess NV12/RGB frames → model input tensor
  - [ ] Run inference
  - [ ] Postprocess: parse detections, NMS, confidence threshold
- [ ] Write `src/inference/CMakeLists.txt`

### 3.2: AI Metadata → DJI Pilot

- [ ] Initialize Payload Camera, choose the matching H.264 custom or DJI stream format, and call `DjiPayloadCamera_SetVideoStreamType` before `DjiCore_ApplicationStart`
- [ ] Register detection labels with `DjiLiveview_RegUserAiTargetLableList`
- [ ] Send bounding boxes with `DjiLiveview_SendAiMetaToPilot`
- [ ] Register the H.264 encoder callback with `DjiLiveview_RegEncoderCallback`
- [ ] Encode processed frames with `DjiLiveview_EncodeAFrameToH264`
- [ ] Send callback output to Pilot with `DjiPayloadCamera_SendVideoStream`, splitting writes at 65,000 bytes or less
- [ ] Use `DjiPayloadCamera_GetVideoStreamState` to apply bandwidth/busy backpressure through dropping, pausing, or bitrate reduction
- [ ] Package the development DPK with `is_ai_rendering` set to `true`
- [ ] End-to-end demo: native or processed live video plus detection overlay visible in DJI Pilot

## Phase 4 — DPK Packaging & Production

- [ ] Finalize `config/app.json` metadata, compatibility bounds, and packaged resources
- [ ] Audit dynamic dependencies; optionally link libstdc++/libgcc statically when compatibility testing justifies it
- [ ] Define how CUDA, TensorRT, OpenCV, and other shared libraries are supplied by the target system or package
- [ ] For bundled shared libraries, package them through `userconfig` and verify an `$ORIGIN`-relative RUNPATH or an equivalent launch-time library-path mechanism
- [ ] Write `scripts/package_dpk.sh` — invoke `build_dpk.sh` from PSDK tools
- [ ] Add `readelf`, symbol-version, and `ldd` checks to the release gate
- [ ] Test: install `.dpk` via DJI Pilot on Manifold 3
- [ ] Test: application lifecycle (install → start → stop → update → uninstall)
