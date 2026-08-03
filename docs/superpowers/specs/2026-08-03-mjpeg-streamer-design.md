# MJPEG Browser Liveview Display Design

Date: 2026-08-03
Status: Approved

## Problem

The current customer demonstration renders the Manifold 3 NV12 liveview stream in a
terminal using ANSI truecolor half-block characters (`capture_demo`, 100x25 chars,
roughly 200x50 effective pixels at ~10 fps). The client considers the resolution too
low. Terminal rendering is abandoned.

## Goals

- Full-color live camera display at full resolution (1440x1080) on the development
  host screen during in-person customer demonstrations.
- Zero new dependencies on the target: libjpeg-turbo (`libjpeg.so.8` + `jpeglib.h`) is
  already present in the cross sysroot (copied from the device).
- Zero host-side installation: the picture is shown in a plain browser tab.
- No inference overlay is required now; the design must not preclude adding one later
  (device-side drawing before encode).

## Approach: MJPEG over HTTP

The device encodes NV12 frames to JPEG with libjpeg-turbo and serves them over a tiny
multipart HTTP server (`multipart/x-mixed-replace`). The development host opens the
stream URL in Chrome and presses F11.

- Resolution: full 1440x1080, quality 80 by default; `--scale` can downsample to
  1280x960 to keep 30 fps.
- Frame rate: capped at 25 fps by default (`--max-fps`), driven by `WaitTake`.
- Bandwidth: ~3-6 MB/s over the USB link; no concern.
- Latency: ~100-300 ms; imperceptible for a live demo.

## Architecture

```
LiveviewCapture (NV12 1440x1080)
        |  WaitTake (LatestFrameSlot)
        v
MjpegStreamer worker thread --every ~40ms--> NV12->RGB (integer fixed-point) -> libjpeg-turbo -> multipart broadcast
        |                                                                                |
        |  poll() on listen socket + client list                                         v
        +----------------------- browsers: http://192.168.42.120:8081 (F11 fullscreen)
```

### New module: `src/stream/mjpeg_streamer.{h,cpp}` (C++17)

- Pure POSIX sockets + libjpeg; no PSDK dependency; consumes `OwnedNv12Frame`.
- Public API: `Start(port, quality, max_fps, scale)`, `Stop()`, `GetStats()`.
- One worker thread:
  1. `poll()` on the listen socket and all client sockets (timeout ~100 ms).
  2. Accept new connections; read and discard request bytes; drop clients on
     read-close/error.
  3. `WaitTake` a frame (reuse `LiveviewCapture`), throttle to `max_fps`, convert
     NV12->RGB with integer fixed-point, optionally downscale, encode with
     libjpeg-turbo, broadcast to all clients.
  4. Drop clients whose socket write fails or stalls (multipart reconnect is
     transparent to the browser); keep encoding while zero clients are connected.
- Reuse a persistent JPEG buffer to keep RSS stable.

### Demo binary: `src/app/stream_demo.cpp`

Replaces `capture_demo.cpp`; terminal rendering is removed entirely.

- Initializes `PsdkLifecycle` + `LiveviewCapture` (same flow, Smart3DExplore must be
  stopped as before), starts `MjpegStreamer`.
- Prints one per-second statistics line: fps / encoded fps / client count / source and
  handoff drops / invalid frames / encode latency / RSS.
- Options: `--port=8081`, `--scale=0.89` (about 1280x960), `--quality=80`, `--max-fps=25`.
- SIGINT/SIGTERM exit (note: the PSDK library installs its own signal handler and
  takes over both signals; the process exits and frees the port, but the app's
  teardown path is not reached — same behavior as the main application).

## Performance budget (4-core A57, single streamer thread)

- NV12->RGB integer fixed-point: ~5-8 ms for 1440x1080.
- libjpeg-turbo NEON encode at q80: ~15-25 ms.
- Result: 20-25 fps sustained at full resolution; downsample to 1280x960 keeps 30 fps.

## Error handling

- Port bind failure: startup fails with a clear message (distinct from the
  Smart3DExplore PSDK-channel error).
- Encode failure: drop the frame, increment a counter, keep streaming.
- Client socket error/timeout: close and remove that client.
- Signals: SIGINT/SIGTERM stop the worker thread first, then capture, then lifecycle.

## Testing

- Host unit tests (host-debug preset, no libjpeg dependency):
  - NV12->RGB conversion against golden values for known input.
  - Multipart header assembly (boundary, Content-Type, Content-Length framing).
- Device smoke script `scripts/run_mjpeg_smoke.sh`:
  - Builds with real credentials, deploys, starts `stream_demo`, pulls the stream
    host-side (`curl --noproxy '*' http://<ip>:8081/`), verifies the HTTP 200
    multipart response, JPEG SOI marker and plausible payload, checks the
    statistics line, and stops the demo on every exit path.
- Manual validation: open `http://192.168.42.120:8081/` in Chrome on the dev host,
  F11 fullscreen, confirm picture and smoothness; verify clean Ctrl-C exit via SSH.
- `docs/capture-demo-guide.md` was replaced by the browser-based `docs/stream-demo-guide.md`.

## Build

- `src/stream/CMakeLists.txt`: static library `mjpeg_streamer`, links `jpeg` from the
  sysroot (`libjpeg.so` dev symlink and `jpeglib.h` are present).
- `src/app/CMakeLists.txt`: `stream_demo` executable (cross build only, no TensorRT
  dependency, same pattern as `capture_demo`).
- Host build: `mjpeg_streamer` is cross-build-only (the host has no libjpeg dev
  files); host builds compile `stream_core` (`yuv_to_rgb` + `mjpeg_framing`) and
  unit tests cover the pure conversion/framing parts only.

## Out of scope

- H.264/RTSP streaming (coupled to device-installed ffmpeg; not needed for the demo).
- Remote access for the client (free bonus later: just hand out the URL).
- Inference overlay rendering.

## Recorded deviations (implementation vs this spec)

- **Default port is 8081, not 8080**: the device's port 8080 is held by an orphaned
  system listener (no owning process, cannot be killed); the demo defaults to 8081.
- **`mjpeg_streamer` is cross-build-only**: the host has no libjpeg dev files, so
  host builds compile `stream_core` only (see Build section).
- **Measured performance exceeds the budget**: 25.0 fps sustained (budget said
  20-25), avg encode 8.73 ms at 1440x1080 q80 (budget said 15-25 ms), RSS stable
  after a one-time ~11 MB startup burst (~0.8 KB/min SDK periodic creep, no app leak).
- **Signals are owned by the PSDK library**: SIGINT/SIGTERM exit the process via
  PSDK's handler ("Captured signal 15, quit!"); the demo's teardown path is not
  reached (same as the main application). Port and sockets are freed by the kernel.
