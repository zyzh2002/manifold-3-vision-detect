# Architecture

## System Boundary

The application runs on DJI Manifold 3, receives camera data from a Matrice 4E through DJI Payload SDK, and performs
target inference with the JetPack-provided TensorRT runtime.

```text
Matrice 4E camera
    |
    | E-Port V2 and PSDK platform services
    v
Manifold 3
    |
    +-- Platform adaptation
    +-- PSDK core lifecycle
    +-- Liveview capture
    +-- Bounded frame handoff
    +-- TensorRT inference
    +-- Product output selected after the inference path is measured
```

## Primary Data Flow

The planned initial implementation uses decoded image streaming because Manifold 3 exposes this path directly through
PSDK. Target testing must confirm the selected Matrice 4E source and NV12 mode:

```text
DjiLiveview_StartImageStream
    -> NV12 callback buffer
    -> copy or transfer into bounded application-owned storage
    -> preprocessing
    -> TensorRT inference
    -> structured detection result
```

The capture layer will copy callback data into application-owned storage before returning unless target-validated API
semantics establish another safe ownership transfer. Expensive preprocessing or inference will not run in the callback
thread. When the consumer falls behind, the bounded handoff applies an explicit drop policy rather than growing memory
without limit.

## Secondary H.264 Path

`DjiLiveview_StartH264Stream()` will be validated for recording and as a fallback capability. It is not the initial
inference input because that would require a separate decoding dependency and another buffering stage.

H.264 becomes the inference input only if:

- decoded ImageStream is unavailable for the required camera source or mode; or
- the product must preserve or process the compressed stream for another verified requirement.

## Module Boundaries

| Module | Responsibility | Direct dependencies |
|---|---|---|
| `src/platform/` | Register Linux OSAL, logging, filesystem, socket, and Manifold 3 USB Bulk handlers. | PSDK platform headers and Linux APIs |
| `src/core/` | Own PSDK initialization, application start, readiness, and orderly shutdown. | `src/platform/`, PSDK core APIs |
| `src/capture/` | Start and stop one selected Liveview source and expose owned frames through a bounded interface. | `src/core/`, PSDK Liveview APIs |
| `src/inference/` | Preprocess frames, execute TensorRT, and return structured detections. | TensorRT, required CUDA APIs |
| `src/app/` | Select configuration and connect capture, inference, and output. | All application modules |

The scaffold intentionally does not prescribe sink class names, a lock-free queue, a multi-consumer model, or a
specific TensorRT wrapper. Those choices are made when their exact interfaces can be derived from working target data.

## Build Boundary

```text
x86_64 Linux host
    |
    +-- NVIDIA Bootlin GCC 9.3.0
    |
    +-- Jetson Linux r35.5.0 Phase 2 base sysroot
    |     +-- BSP and sample root filesystem
    |     +-- NVIDIA binary overlay and Tegra runtime libraries
    |
    +-- Phase 5 sysroot extension
    |     +-- CUDA 11.4 development files
    |     +-- TensorRT 8.5.2 and cuDNN development files
    |
    +-- PSDK 3.16.0 headers and AArch64 libpayloadsdk.a
    |
    +-- AArch64 application
          -> direct target validation
          -> DPK packaging
```

The standard Jetson Linux r35.5.0 sysroot is the build baseline. Manifold 3 firmware is the runtime authority. Target
files are added as an overlay only after a concrete mismatch is measured. See `docs/build-environment.md` for version,
link, and verification rules.

## Error and Shutdown Model

- Platform registration or PSDK core initialization failure prevents application startup.
- Capture startup failure leaves inference stopped and reports the exact PSDK return code.
- Invalid frame metadata or unsupported formats are rejected before entering inference.
- Inference errors do not block the PSDK callback thread; the application records the error and applies its configured
  stop or retry policy outside the callback.
- Shutdown stops capture first, drains or discards bounded frames, destroys inference resources, deinitializes Liveview,
  and then tears down the remaining PSDK lifecycle in reverse order.

## Deferred Product Output

The inference pipeline initially produces local structured detection results. A later milestone selects one product
output based on actual requirements and verified PSDK support:

- Pilot AI metadata;
- processed H.264 video;
- file or local IPC output;
- a network transport permitted by the application environment.

No output-specific PSDK camera emulation or encoder lifecycle is introduced before that selection.
