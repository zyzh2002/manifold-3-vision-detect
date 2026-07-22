# Architecture

## System Overview

```
Matrice 4T Camera
  |
  |  E-Port V2 (PSDK core over USB Bulk; socket/network services for data paths)
  v
Manifold 3 (Jetson-class, JetPack 5.1.3)
  |
  +-- PSDK Core (DjiCore_Init, DjiPlatform, HAL/OSAL)
  |
  +-- Liveview API
  |   +-- DjiLiveview_StartH264Stream()  -> H.264 NALU callback
  |   +-- DjiLiveview_StartImageStream() -> decoded NV12/RGB callback (M3 only)
  |
  +-- Capture Layer (abstraction)
  |   +-- CaptureH264   -> ISink -> FileSink (.h264 file)
  |   +-- CaptureImage  -> ISink -> RingBufferSink
  |   +-- ISink: FileSink | CallbackSink | RingBufferSink
  |
  +-- Inference Engine (Phase 3)
  |   +-- Read frame from RingBufferSink
  |   +-- Preprocess (NV12 -> model input tensor)
  |   +-- Run TensorRT inference
  |   +-- Postprocess (parse detections, NMS, threshold)
  |   +-- Send AI meta to Pilot: DjiLiveview_SendAiMetaToPilot
  |   +-- Optional processed-video path:
  |       +-- DjiLiveview_EncodeAFrameToH264
  |       +-- Encoder callback
  |       +-- Stream-state backpressure + <= 65,000-byte writes
  |       +-- DjiPayloadCamera_SendVideoStream
  |
  +-- Output
      +-- H.264 files (captured streams)
      +-- Annotated liveview (Pilot display)
      +-- Detection metadata (bounding boxes, labels)
```

## Key Design Decisions

### Why Both H.264 and Decoded Image Streams?

PSDK provides two liveview modes:

| Mode | API | Output | Platform |
|---|---|---|---|
| H.264 NALU | `DjiLiveview_StartH264Stream` | Compressed H.264 bitstream | All platforms |
| Decoded image | `DjiLiveview_StartImageStream` | Raw NV12/RGB frames | Manifold 3 only |

The capture layer abstracts both so that:
- H.264 mode is available for recording and all-platform compatibility
- Decoded image mode feeds directly into the inference pipeline (Phase 3) without a separate decoder, leveraging Manifold 3's unique capability
- Both share the same `ISink` interface, making sinks interchangeable

### Capture Abstraction (ISink)

```
Capture
  +-- start(position, cameraSource, sink)
  +-- stop()
  +-- ISink (injected):
      +-- FileSink: writes frames to disk (.h264, .nv12, .rgb)
      +-- RingBufferSink: lock-free ring buffer, multi-consumer
      +-- CallbackSink: user-defined callback function
```

This decouples the PSDK API from frame consumers. The inference engine subscribes to `RingBufferSink` without knowing PSDK internals.

The image callback buffer is not retained by the capture layer. Frames are copied into owned, bounded storage before the callback returns. The initial implementation uses a synchronized queue with an explicit drop policy; a lock-free implementation is considered only if profiling demonstrates a bottleneck.

### Camera Sources (Matrice 4T)

| Source | Enum Value | Description |
|---|---|---|
| `DJI_LIVEVIEW_CAMERA_SOURCE_M4T_VIS` | 1 | Visible light camera |
| `DJI_LIVEVIEW_CAMERA_SOURCE_M4T_IR` | 2 | Infrared thermal camera |

Camera positions: `NO_1`, `NO_2`, `NO_3` (payload ports), `FPV=7`.

### Cross-Compilation Strategy

```
Host (x86_64 Linux)
  |
  +-- crosstool-ng toolchain
  |   +-- gcc 11.5.0
  |   +-- glibc 2.31 (matching Manifold 3)
  |   +-- kernel headers 5.10
  |
  +-- JetPack 5.1.3 target sysroot
  |   +-- CUDA 11.4.19
  |   +-- TensorRT 8.5.2
  |   +-- target OpenCV/multimedia libraries
  |
  +-- CMake + toolchain-aarch64.cmake
  |   +-- Link libpayloadsdk.a (static, from PSDK submodule)
  |   +-- Produce aarch64 ELF binary
  |
  +-- scripts/deploy.sh
      +-- scp binary to Manifold 3
      +-- SSH remote execution
```

**Why gcc 11.5.0 + glibc 2.31?**

Manifold 3 (JetPack 5.1.3 / Ubuntu 20.04) uses the target system ABI and NVIDIA runtime stack. The proposed cross-compilation toolchain uses gcc 11.5.0, but that choice must be validated against the actual target environment and exported sysroot.

| Concern | Strategy |
|---|---|
| **glibc ABI** | Build against a target sysroot matching the deployed Manifold 3 image, then inspect required symbol versions with `readelf`. |
| **libstdc++ ABI** | Prefer the target runtime when compatible. Static libstdc++/libgcc is an optional compatibility choice that requires device testing; it is not a DPK requirement. |
| **NVIDIA libraries** | Resolve CUDA, TensorRT, OpenCV, and multimedia headers and shared libraries from the JetPack 5.1.3 target sysroot. Crosstool-ng alone does not supply them. |
| **C++17** | GCC 9 already provides a non-experimental C++17 implementation and does not require `-lstdc++fs` for `std::filesystem`. GCC 11 may still be selected for toolchain consistency and diagnostics. |
| **Packaging** | Audit all dynamic dependencies and define whether each library is supplied by Manifold 3 or copied through DPK `userconfig`. Bundled libraries require a verified `$ORIGIN` RUNPATH or equivalent launch-time library path because `build_dpk.sh` does not configure the dynamic loader. |

No compiler version or static C++ runtime policy is assumed valid until the resulting ELF has been checked and executed on the target firmware.

### Module Boundaries

| Module | Responsibility | Dependencies |
|---|---|---|
| `src/platform/` | Linux common OSAL/filesystem/socket plus Manifold 3 USB Bulk HAL and link config | `third_party/psdk/psdk_lib` |
| `src/core/` | PSDK lifecycle: init, start, shutdown | `src/platform/` |
| `src/capture/` | Video stream abstraction, ISink framework | `src/core/`, `dji_liveview.h` |
| `src/inference/` | TensorRT engine, pre/postprocess, AI meta | `src/capture/` (RingBufferSink) |
| `src/app/` | Entry point, wiring, config | all above |

### Extension Points

1. **New sinks** — implement `ISink` for new frame destinations (ROS topic, RTSP, MQTT)
2. **New camera sources** — add verified SDK enum mappings for future aircraft; camera selection remains config-driven
3. **Model swap** — rebuild an engine compatible with the target TensorRT/CUDA/GPU environment and the configured binding schema
4. **DPK packaging** — Phase 2 establishes the minimal manifest and install loop; Phase 4 adds production dependency and lifecycle gates
