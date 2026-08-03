# MJPEG Browser Liveview Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the low-resolution terminal liveview demo with a full-resolution MJPEG-over-HTTP stream that renders in a browser on the development host.

**Architecture:** The device encodes NV12 frames to JPEG with libjpeg-turbo and serves them over a tiny multipart HTTP server (`multipart/x-mixed-replace`); the dev host opens `http://192.168.42.120:8080/` in Chrome. Pure conversion/framing code is host-testable; the streamer (sockets + libjpeg) is cross-build-only because the host has no libjpeg dev files.

**Tech Stack:** C++17, POSIX sockets, libjpeg-turbo (already in the cross sysroot as `libjpeg.so` + `jpeglib.h`), CMake presets (`manifold3-cross-release`, `host-debug`), ctest.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-08-03-mjpeg-streamer-design.md` (approved).
- Work happens on branch `demo/capture-demo`; commit messages in English with conventional-commit prefixes.
- Code style: C++17, LLVM clang-format, 120-column limit, snake_case files/functions, PascalCase types, no Chinese comments.
- Host build must NOT require libjpeg (host has no `jpeglib.h`); `mjpeg_streamer.cpp` is compiled only when cross-compiling.
- Default stream parameters: port 8080, quality 80, max_fps 25, scale 1.0 (full 1440x1080).
- libjpeg encode uses `jpeg_mem_dest` (libjpeg-turbo, present in the sysroot).
- The demo binary must keep the per-second stats line and clean SIGINT/SIGTERM shutdown.
- No inference overlay. Terminal rendering is removed entirely.

---

### Task 1: NV12->RGB24 conversion + box downscale module (host-testable)

**Files:**
- Create: `src/stream/yuv_to_rgb.h`
- Create: `src/stream/yuv_to_rgb.cpp`
- Create: `tests/stream/CMakeLists.txt`
- Create: `tests/stream/test_yuv_to_rgb.cpp`
- Modify: `src/CMakeLists.txt` (add `add_subdirectory(stream)`)
- Modify: `tests/CMakeLists.txt` (add `add_subdirectory(stream)`)

**Interfaces:**
- Consumes: nothing (pure C++17).
- Produces (used by Task 4):
  - `void manifold3::stream::YuvNv12ToRgb24(const uint8_t *nv12, uint32_t width, uint32_t height, uint8_t *rgbOut);`
    - `width`/`height` must be even; `rgbOut` must hold `width*height*3` bytes; BT.601 limited-range-free integer fixed-point (see code).
  - `void manifold3::stream::DownscaleRgb24(const uint8_t *rgbIn, uint32_t srcW, uint32_t srcH, uint8_t *rgbOut, uint32_t dstW, uint32_t dstH);`
    - `dstW <= srcW`, `dstH <= srcH`, both even; box-average over evenly partitioned source blocks.

- [ ] **Step 1: Write the failing test**

Create `src/stream/yuv_to_rgb.h`:

```cpp
#pragma once

#include <cstdint>

namespace manifold3 {
namespace stream {

// Converts one NV12 frame (Y plane then interleaved UV at half resolution)
// into RGB24 (R,G,B byte triplets, row-major). width/height must be even.
// BT.601 conversion with integer fixed-point arithmetic, 13 fractional bits.
void YuvNv12ToRgb24(const uint8_t *nv12, uint32_t width, uint32_t height, uint8_t *rgbOut);

// Box-average downscale of an RGB24 image. dstW/dstH must be <= srcW/srcH
// and even. Source pixels are partitioned into dstW*dstH equal blocks.
void DownscaleRgb24(const uint8_t *rgbIn, uint32_t srcW, uint32_t srcH, uint8_t *rgbOut,
                    uint32_t dstW, uint32_t dstH);

} // namespace stream
} // namespace manifold3
```

Create `tests/stream/CMakeLists.txt`:

```cmake
add_executable(test_yuv_to_rgb
    test_yuv_to_rgb.cpp
    ${CMAKE_SOURCE_DIR}/src/stream/yuv_to_rgb.cpp
)

target_include_directories(test_yuv_to_rgb PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME yuv_to_rgb COMMAND test_yuv_to_rgb)
```

Create `tests/stream/test_yuv_to_rgb.cpp`:

```cpp
#include <cassert>
#include <cstdint>
#include <cstring>

#include "stream/yuv_to_rgb.h"

using manifold3::stream::DownscaleRgb24;
using manifold3::stream::YuvNv12ToRgb24;

namespace {

// 4x2 NV12 frame: 8 Y bytes, then U plane (2x1) = {64, 192}, then V plane
// (2x1) = {192, 64}. Column pairs share one UV pair.
//   Y:   128  0  255  128
//        128  128 255  0
//   UV:  (64,192) (192,64) per column pair.
const uint8_t kNv12[12] = {128, 0, 255, 128, 128, 128, 255, 0, 64, 192, 192, 64};

// Expected RGB24 (computed with the fixed-point formula, clamped to [0,255]).
const uint8_t kExpected[4 * 2 * 3] = {
    218, 105, 15,   // (0,0): Y=128 U=64 V=192
    90, 0, 0,       // (1,0): Y=0   U=64 V=192 (G,B clamp to 0)
    166, 255, 255,  // (2,0): Y=255 U=192 V=64 (G,B clamp to 255)
    39, 152, 242,   // (3,0): Y=128 U=192 V=64
    218, 105, 15,   // (0,1): same UV as column 0
    218, 105, 15,   // (1,1): Y=128 U=64 V=192
    166, 255, 255,  // (2,1): Y=255 U=192 V=64
    0, 24, 114,     // (3,1): Y=0   U=192 V=64 (R clamps to 0)
};

void TestNv12ToRgb24() {
    uint8_t rgb[4 * 2 * 3];
    YuvNv12ToRgb24(kNv12, 4, 2, rgb);
    assert(std::memcmp(rgb, kExpected, sizeof(kExpected)) == 0);
}

void TestDownscale() {
    // 4x2 -> 2x1: each output pixel averages a 2x2 source block.
    const uint8_t src[4 * 2 * 3] = {
        0, 0, 0,   4, 0, 0,   0, 0, 0,   4, 0, 0,
        0, 0, 0,   4, 0, 0,   0, 0, 0,   4, 0, 0,
    };
    uint8_t dst[2 * 1 * 3];
    DownscaleRgb24(src, 4, 2, dst, 2, 1);
    const uint8_t expected[6] = {2, 0, 0, 2, 0, 0};
    assert(std::memcmp(dst, expected, sizeof(expected)) == 0);
}

} // namespace

int main() {
    TestNv12ToRgb24();
    TestDownscale();
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --preset host-debug && cmake --build --preset host-debug --target test_yuv_to_rgb && ./build-host/tests/stream/test_yuv_to_rgb`
Expected: compile error (no `stream/yuv_to_rgb.h`).

- [ ] **Step 3: Write the implementation**

Create `src/stream/yuv_to_rgb.cpp`:

```cpp
#include "stream/yuv_to_rgb.h"

namespace manifold3 {
namespace stream {

namespace {

constexpr int32_t kScale = 8192;        // 13 fractional bits
constexpr int32_t kHalf = 4096;         // round-to-nearest offset
constexpr int32_t kCvR = 11485;         // 1.402  * 8192
constexpr int32_t kCuG = 2819;          // 0.344136 * 8192
constexpr int32_t kCvG = 5850;          // 0.714136 * 8192
constexpr int32_t kCuB = 14516;         // 1.772  * 8192

uint8_t ClampByte(int32_t v) {
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return static_cast<uint8_t>(v);
}

} // namespace

void YuvNv12ToRgb24(const uint8_t *nv12, uint32_t width, uint32_t height, uint8_t *rgbOut) {
    const uint32_t uvWidth = width / 2;
    const uint8_t *yPlane = nv12;
    const uint8_t *uvPlane = nv12 + static_cast<size_t>(width) * height;
    uint8_t *out = rgbOut;
    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t *uvRow = uvPlane + (row / 2) * uvWidth * 2;
        for (uint32_t col = 0; col < width; ++col) {
            const int32_t y = yPlane[row * width + col];
            const int32_t u = uvRow[(col / 2) * 2] - 128;
            const int32_t v = uvRow[(col / 2) * 2 + 1] - 128;
            const int32_t yBase = y * kScale;
            *out++ = ClampByte((yBase + kCvR * v + kHalf) >> 13);
            *out++ = ClampByte((yBase - kCuG * u - kCvG * v + kHalf) >> 13);
            *out++ = ClampByte((yBase + kCuB * u + kHalf) >> 13);
        }
    }
}

void DownscaleRgb24(const uint8_t *rgbIn, uint32_t srcW, uint32_t srcH, uint8_t *rgbOut,
                    uint32_t dstW, uint32_t dstH) {
    const uint32_t bw = srcW / dstW;
    const uint32_t bh = srcH / dstH;
    uint8_t *out = rgbOut;
    for (uint32_t dy = 0; dy < dstH; ++dy) {
        for (uint32_t dx = 0; dx < dstW; ++dx) {
            int32_t sum[3] = {0, 0, 0};
            for (uint32_t sy = dy * bh; sy < (dy + 1) * bh; ++sy) {
                const uint8_t *row = rgbIn + (sy * srcW + dx * bw) * 3;
                for (uint32_t sx = dx * bw; sx < (dx + 1) * bw; ++sx, row += 3) {
                    sum[0] += row[0];
                    sum[1] += row[1];
                    sum[2] += row[2];
                }
            }
            const uint32_t n = bw * bh;
            *out++ = static_cast<uint8_t>((sum[0] + n / 2) / n);
            *out++ = static_cast<uint8_t>((sum[1] + n / 2) / n);
            *out++ = static_cast<uint8_t>((sum[2] + n / 2) / n);
        }
    }
}

} // namespace stream
} // namespace manifold3
```

Modify `src/CMakeLists.txt`: add `add_subdirectory(stream)` after `add_subdirectory(inference)`.
Modify `tests/CMakeLists.txt`: add `add_subdirectory(stream)` after `add_subdirectory(inference)`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --preset host-debug && cmake --build --preset host-debug --target test_yuv_to_rgb && ./build-host/tests/stream/test_yuv_to_rgb`
Expected: exit 0, no assertion failures.

- [ ] **Step 5: Run the full host test suite**

Run: `cmake --build --preset host-debug && ctest --preset host-debug --output-on-failure`
Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/stream src/CMakeLists.txt tests/stream tests/CMakeLists.txt
git commit -m "feat: add host-testable NV12 to RGB conversion and downscale"
```

---

### Task 2: Multipart MJPEG framing module (host-testable)

**Files:**
- Create: `src/stream/mjpeg_framing.h`
- Create: `src/stream/mjpeg_framing.cpp`
- Modify: `tests/stream/CMakeLists.txt` (add second test executable)
- Create: `tests/stream/test_mjpeg_framing.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces (used by Task 4):
  - `constexpr char manifold3::stream::kMjpegBoundary[] = "frame";`
  - `void manifold3::stream::AppendHttpHeaders(std::vector<uint8_t> *out);`
    - Appends `HTTP/1.0 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n`.
  - `void manifold3::stream::AppendJpegPart(std::vector<uint8_t> *out, const uint8_t *jpeg, size_t jpegLen);`
    - Appends `--frame\r\nContent-Type: image/jpeg\r\nContent-Length: <len>\r\n\r\n<jpeg bytes>\r\n`.

- [ ] **Step 1: Write the failing test**

Create `src/stream/mjpeg_framing.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace manifold3 {
namespace stream {

constexpr char kMjpegBoundary[] = "frame";

// Appends the multipart/x-mixed-replace HTTP response header for an MJPEG
// stream.
void AppendHttpHeaders(std::vector<uint8_t> *out);

// Appends one JPEG part: boundary line, part headers, payload, trailing CRLF.
void AppendJpegPart(std::vector<uint8_t> *out, const uint8_t *jpeg, size_t jpegLen);

} // namespace stream
} // namespace manifold3
```

Create `tests/stream/test_mjpeg_framing.cpp`:

```cpp
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "stream/mjpeg_framing.h"

using manifold3::stream::AppendHttpHeaders;
using manifold3::stream::AppendJpegPart;

namespace {

void TestHttpHeaders() {
    std::vector<uint8_t> out;
    AppendHttpHeaders(&out);
    const std::string s(out.begin(), out.end());
    const std::string kExpected =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "\r\n";
    assert(s == kExpected);
}

void TestJpegPart() {
    const uint8_t payload[3] = {0xFF, 0xD8, 0xFF};
    std::vector<uint8_t> out;
    AppendJpegPart(&out, payload, 3);
    const std::string s(out.begin(), out.end());
    const std::string kExpected =
        "--frame\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: 3\r\n"
        "\r\n"
        "\xFF\xD8\xFF\r\n";
    assert(s == kExpected);
}

} // namespace

int main() {
    TestHttpHeaders();
    TestJpegPart();
    return 0;
}
```

Modify `tests/stream/CMakeLists.txt` (append):

```cmake
add_executable(test_mjpeg_framing
    test_mjpeg_framing.cpp
    ${CMAKE_SOURCE_DIR}/src/stream/mjpeg_framing.cpp
)

target_include_directories(test_mjpeg_framing PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME mjpeg_framing COMMAND test_mjpeg_framing)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --preset host-debug && cmake --build --preset host-debug --target test_mjpeg_framing`
Expected: compile error (no `stream/mjpeg_framing.h`).

- [ ] **Step 3: Write the implementation**

Create `src/stream/mjpeg_framing.cpp`:

```cpp
#include "stream/mjpeg_framing.h"

#include <cstdio>
#include <cstring>

namespace manifold3 {
namespace stream {

void AppendHttpHeaders(std::vector<uint8_t> *out) {
    const char kHeaders[] =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "\r\n";
    out->insert(out->end(), kHeaders, kHeaders + std::strlen(kHeaders));
}

void AppendJpegPart(std::vector<uint8_t> *out, const uint8_t *jpeg, size_t jpegLen) {
    char header[128];
    const int n = std::snprintf(header, sizeof(header),
                                "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
                                jpegLen);
    out->insert(out->end(), header, header + n);
    out->insert(out->end(), jpeg, jpeg + jpegLen);
    out->push_back('\r');
    out->push_back('\n');
}

} // namespace stream
} // namespace manifold3
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset host-debug --target test_mjpeg_framing && ./build-host/tests/stream/test_mjpeg_framing`
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/stream tests/stream
git commit -m "feat: add multipart MJPEG framing helpers"
```

---

### Task 3: MjpegStreamer (sockets + libjpeg, cross-build only)

**Files:**
- Create: `src/stream/mjpeg_streamer.h`
- Create: `src/stream/mjpeg_streamer.cpp`
- Modify: `src/stream/CMakeLists.txt` (create; `mjpeg_streamer` only when cross-compiling)

**Interfaces:**
- Consumes: `capture::OwnedNv12Frame` (`src/capture/latest_frame_slot.h`), `stream::YuvNv12ToRgb24`, `stream::DownscaleRgb24`, `stream::AppendHttpHeaders`, `stream::AppendJpegPart`.
- Produces (used by Task 4):
  - `struct manifold3::stream::StreamerStats { uint64_t encoded_frames; uint64_t encode_failures; uint64_t clients_served; uint32_t active_clients; double avg_encode_ms; double avg_frame_interval_ms; };`
  - `class manifold3::stream::MjpegStreamer`
    - `using FrameProvider = std::function<bool(capture::OwnedNv12Frame *frame)>;` — called from the worker thread; returns false to stop the loop (provider signals shutdown).
    - `bool Start(uint16_t port, int quality, uint32_t max_fps, double scale, FrameProvider provider);`
      - Binds and listens on `0.0.0.0:port`, spawns the worker thread; `port 0` means "OS-assigned" (query via `port()`); returns false if bind fails.
    - `void Stop();` — idempotent; closes listen socket + all clients, joins the worker thread.
    - `uint16_t port() const;`
    - `StreamerStats GetStats() const;`

- [ ] **Step 1: Write the header**

Create `src/stream/mjpeg_streamer.h`:

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "capture/latest_frame_slot.h"

namespace manifold3 {
namespace stream {

struct StreamerStats {
    uint64_t encoded_frames = 0;
    uint64_t encode_failures = 0;
    uint64_t clients_served = 0; // accepted connections (total)
    uint32_t active_clients = 0; // current snapshot
    double avg_encode_ms = 0.0;
    double avg_frame_interval_ms = 0.0;
};

// Serves NV12 frames as an MJPEG multipart/x-mixed-replace HTTP stream.
// One worker thread: poll() on the listen socket and clients, pull frames via
// the FrameProvider, encode with libjpeg-turbo, broadcast to all clients.
class MjpegStreamer {
  public:
    // Pulls one frame; must return false when the streamer should stop.
    using FrameProvider = std::function<bool(capture::OwnedNv12Frame *frame)>;

    MjpegStreamer() = default;
    ~MjpegStreamer();
    MjpegStreamer(const MjpegStreamer &) = delete;
    MjpegStreamer &operator=(const MjpegStreamer &) = delete;

    // Binds 0.0.0.0:port and starts the worker thread. port 0 requests an
    // OS-assigned port (read it via port()). scale <= 1.0 downscales the
    // output. Returns false if the bind or thread spawn fails.
    bool Start(uint16_t port, int quality, uint32_t max_fps, double scale, FrameProvider provider);

    // Stops the worker thread and closes all sockets. Idempotent.
    void Stop();

    // Actual bound port (valid after Start).
    uint16_t port() const;

    StreamerStats GetStats() const;

  private:
    void WorkerLoop();
    void AcceptClient(int fd);

    mutable std::mutex mutex_;
    int listen_fd_ = -1;
    bool running_ = false;
    bool stop_requested_ = false;
    uint16_t port_ = 0;
    int quality_ = 80;
    uint32_t max_fps_ = 25;
    double scale_ = 1.0;
    FrameProvider provider_;
    std::vector<int> clients_;
    std::thread worker_;
    StreamerStats stats_;
};

} // namespace stream
} // namespace manifold3
```

- [ ] **Step 2: Write the implementation**

Create `src/stream/CMakeLists.txt`:

```cmake
# Pure modules: compiled and tested on host and target.
add_library(stream_core STATIC
    yuv_to_rgb.cpp
    mjpeg_framing.cpp
)

# Socket + libjpeg streamer: cross build only (host has no libjpeg dev files).
if(CMAKE_CROSSCOMPILING)
    add_library(stream STATIC mjpeg_streamer.cpp)
    target_link_libraries(stream PUBLIC stream_core jpeg)
    target_include_directories(stream PUBLIC ${CMAKE_SOURCE_DIR}/src)
else()
    add_library(stream INTERFACE)
    target_link_libraries(stream INTERFACE stream_core)
endif()
```

Create `src/stream/mjpeg_streamer.cpp`:

```cpp
#include "stream/mjpeg_streamer.h"

#include <arpa/inet.h>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <jpeglib.h>

#include "stream/mjpeg_framing.h"
#include "stream/yuv_to_rgb.h"

namespace manifold3 {
namespace stream {

namespace {

constexpr int kPollTimeoutMs = 100;
constexpr int kRecvBufSize = 4096;
constexpr std::chrono::milliseconds kSendTimeoutMs(500);

void SetNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void SetSendTimeout(int fd, std::chrono::milliseconds timeout) {
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

struct JpegErrorState {
    jpeg_error_mgr pub;
    jmp_buf jump;
};

void JpegErrorExit(j_common_ptr cinfo) {
    JpegErrorState *err = reinterpret_cast<JpegErrorState *>(cinfo->err);
    longjmp(err->jump, 1);
}

} // namespace

MjpegStreamer::~MjpegStreamer() {
    Stop();
}

bool MjpegStreamer::Start(uint16_t port, int quality, uint32_t max_fps, double scale,
                          FrameProvider provider) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return false;
    }
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 || listen(fd, 8) < 0) {
        close(fd);
        return false;
    }
    sockaddr_in bound{};
    socklen_t boundLen = sizeof(bound);
    getsockname(fd, reinterpret_cast<sockaddr *>(&bound), &boundLen);
    listen_fd_ = fd;
    port_ = ntohs(bound.sin_port);
    quality_ = quality < 1 ? 1 : (quality > 100 ? 100 : quality);
    max_fps_ = max_fps == 0 ? 1 : max_fps;
    scale_ = scale <= 0.0 ? 1.0 : scale;
    provider_ = std::move(provider);
    stop_requested_ = false;
    running_ = true;
    worker_ = std::thread(&MjpegStreamer::WorkerLoop, this);
    return true;
}

void MjpegStreamer::Stop() {
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        stop_requested_ = true;
        if (listen_fd_ >= 0) {
            shutdown(listen_fd_, SHUT_RDWR);
            close(listen_fd_);
            listen_fd_ = -1;
        }
        worker = std::move(worker_);
    }
    // The worker's poll and frame-provider calls are bounded, so join returns
    // promptly; no detached-thread use-after-free window remains.
    if (worker.joinable()) {
        worker.join();
    }
}

uint16_t MjpegStreamer::port() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return port_;
}

StreamerStats MjpegStreamer::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void MjpegStreamer::AcceptClient(int fd) {
    SetNonBlocking(fd);
    SetSendTimeout(fd, kSendTimeoutMs);
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.push_back(fd);
    ++stats_.clients_served;
}

void MjpegStreamer::WorkerLoop() {
    std::vector<uint8_t> rgb;
    std::vector<uint8_t> jpeg;
    std::vector<pollfd> pollfds;
    auto lastFrameAt = std::chrono::steady_clock::now();
    // Deadline of the next allowed frame pull. Polling waits until this
    // deadline so the poll timeout does not dominate the frame cadence.
    auto nextFrameDue = lastFrameAt;
    const auto frameInterval = std::chrono::milliseconds(1000 / max_fps_);
    uint64_t frameIntervalUs = 0;
    uint64_t frameIntervalCount = 0;
    uint64_t encodeUs = 0;
    uint64_t encodeCount = 0;

    while (true) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_) {
                break;
            }
        }

        // Build the poll set from the current client list.
        pollfds.clear();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pollfds.push_back(pollfd{listen_fd_, POLLIN, 0});
            for (int fd : clients_) {
                pollfds.push_back(pollfd{fd, POLLIN, 0});
            }
        }
        // Sleep until the next frame is due (or until client I/O wakes us).
        const auto pollDeadline = std::chrono::steady_clock::now();
        int pollTimeoutMs = kPollTimeoutMs;
        if (nextFrameDue > pollDeadline) {
            const auto untilDue = std::chrono::duration_cast<std::chrono::milliseconds>(
                nextFrameDue - pollDeadline);
            pollTimeoutMs = std::min(kPollTimeoutMs, static_cast<int>(untilDue.count()));
        }
        const int pollResult = poll(pollfds.data(), pollfds.size(), pollTimeoutMs);
        if (pollResult > 0 && (pollfds[0].revents & (POLLIN | POLLERR | POLLHUP))) {
            const int clientFd = accept(listen_fd_, nullptr, nullptr);
            if (clientFd >= 0) {
                AcceptClient(clientFd);
            }
        }

        // Drain client sockets; drop closed/errored clients.
        std::vector<int> toDrop;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (size_t i = 1; i < pollfds.size(); ++i) {
                const int fd = pollfds[i].fd;
                if (pollfds[i].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) {
                    char buf[kRecvBufSize];
                    const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
                    if (n <= 0) {
                        toDrop.push_back(fd);
                    }
                }
            }
            for (int fd : toDrop) {
                close(fd);
                clients_.erase(std::remove(clients_.begin(), clients_.end(), fd), clients_.end());
            }
        }

        // Throttled frame pull.
        const auto now = std::chrono::steady_clock::now();
        if (now < nextFrameDue) {
            continue;
        }

        capture::OwnedNv12Frame frame;
        if (!provider_(frame)) {
            break;
        }
        const auto frameNow = std::chrono::steady_clock::now();
        frameIntervalUs +=
            std::chrono::duration_cast<std::chrono::microseconds>(frameNow - lastFrameAt).count();
        ++frameIntervalCount;
        lastFrameAt = frameNow;

        // Convert and optionally downscale.
        const uint32_t outW =
            std::max<uint32_t>(2, static_cast<uint32_t>(frame.width * scale_) & ~1u);
        const uint32_t outH =
            std::max<uint32_t>(2, static_cast<uint32_t>(frame.height * scale_) & ~1u);
        rgb.resize(static_cast<size_t>(frame.width) * frame.height * 3);
        YuvNv12ToRgb24(frame.data.data(), frame.width, frame.height, rgb.data());
        if (outW != frame.width || outH != frame.height) {
            std::vector<uint8_t> small(static_cast<size_t>(outW) * outH * 3);
            DownscaleRgb24(rgb.data(), frame.width, frame.height, small.data(), outW, outH);
            rgb.swap(small);
        }

        // Encode to JPEG with libjpeg-turbo's memory destination.
        const auto encodeStart = std::chrono::steady_clock::now();
        jpeg.clear();
        jpeg_compress_struct cinfo;
        JpegErrorState jerr;
        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = JpegErrorExit;
        bool encoded = false;
        unsigned char *mem = nullptr;
        unsigned long memLen = 0;
        if (setjmp(jerr.jump) == 0) {
            jpeg_create_compress(&cinfo);
            jpeg_mem_dest(&cinfo, &mem, &memLen);
            cinfo.image_width = outW;
            cinfo.image_height = outH;
            cinfo.input_components = 3;
            cinfo.in_color_space = JCS_RGB;
            jpeg_set_defaults(&cinfo);
            jpeg_set_quality(&cinfo, quality_, TRUE);
            jpeg_start_compress(&cinfo, TRUE);
            uint8_t *row = rgb.data();
            const size_t stride = static_cast<size_t>(outW) * 3;
            while (cinfo.next_scanline < cinfo.image_height) {
                JSAMPROW rows[1] = {row};
                jpeg_write_scanlines(&cinfo, rows, 1);
                row += stride;
            }
            jpeg_finish_compress(&cinfo);
            jpeg_destroy_compress(&cinfo);
            if (mem != nullptr && memLen > 0) {
                jpeg.assign(mem, mem + memLen);
                encoded = true;
            }
            std::free(mem);
        } else {
            jpeg_destroy_compress(&cinfo);
        }
        const auto encodeEnd = std::chrono::steady_clock::now();
        encodeUs += std::chrono::duration_cast<std::chrono::microseconds>(encodeEnd - encodeStart).count();
        ++encodeCount;

        if (!encoded) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.encode_failures;
            continue;
        }

        // Build one multipart frame and broadcast to all clients.
        std::vector<uint8_t> part;
        AppendJpegPart(&part, jpeg.data(), jpeg.size());
        std::vector<int> dead;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (int fd : clients_) {
                ssize_t sent = 0;
                while (sent < static_cast<ssize_t>(part.size())) {
                    const ssize_t n = send(fd, part.data() + sent, part.size() - sent, MSG_NOSIGNAL);
                    if (n <= 0) {
                        dead.push_back(fd);
                        break;
                    }
                    sent += n;
                }
            }
            for (int fd : dead) {
                close(fd);
                clients_.erase(std::remove(clients_.begin(), clients_.end(), fd), clients_.end());
            }
            ++stats_.encoded_frames;
            stats_.active_clients = static_cast<uint32_t>(clients_.size());
            // Publish live averages so GetStats() is meaningful while running.
            if (encodeCount > 0) {
                stats_.avg_encode_ms = static_cast<double>(encodeUs) / encodeCount / 1000.0;
            }
            if (frameIntervalCount > 0) {
                stats_.avg_frame_interval_ms =
                    static_cast<double>(frameIntervalUs) / frameIntervalCount / 1000.0;
            }
        }
        // Advance the cadence deadline; never burst-catch-up after a slow frame.
        nextFrameDue += frameInterval;
        if (nextFrameDue < now) {
            nextFrameDue = now;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int fd : clients_) {
            close(fd);
        }
        clients_.clear();
        stats_.active_clients = 0;
        running_ = false;
    }
}

} // namespace stream
} // namespace manifold3
```

Note: `jpeg_mem_dest` is called once after `jpeg_create_compress` with
`unsigned char **outbuffer` / `unsigned long *outsize`; after
`jpeg_finish_compress` the buffer is owned by libjpeg-turbo and must be freed
with `free(mem)`.

- [ ] **Step 3: Cross-compile check**

Run: `source scripts/setup_env.sh && cmake --preset manifold3-cross-release && cmake --build --preset manifold3-cross-release --target stream`
Expected: `stream` static library builds without errors; the `jpeg` link target resolves against the sysroot (`libjpeg.so` + `jpeglib.h`).

- [ ] **Step 4: Commit**

```bash
git add src/stream
git commit -m "feat: add MJPEG streamer with libjpeg encoding"
```

---

### Task 4: stream_demo application

**Files:**
- Delete: `src/app/capture_demo.cpp`
- Create: `src/app/stream_demo.cpp`
- Modify: `src/app/CMakeLists.txt` (replace the `capture_demo` target with `stream_demo`)

**Interfaces:**
- Consumes: `PsdkLifecycle`, `LiveviewCapture`, `MjpegStreamer` (all signatures from earlier tasks).
- Produces: the `stream_demo` executable deployed to `~/vision-detect/stream_demo`.

- [ ] **Step 1: Write the app**

Create `src/app/stream_demo.cpp`:

```cpp
// Liveview demo for customer demonstration.
//
// Serves the Manifold 3 NV12 liveview stream as MJPEG over HTTP:
// open http://192.168.42.120:8080/ in a browser (F11 for fullscreen).
// Prints one per-second statistics line to stdout. Throwaway demo binary:
// it does not load any inference engine.
//
// Usage (on the device, or via ssh):
//   ./stream_demo [--port=8080] [--quality=80] [--max-fps=25] [--scale=1.0]

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "capture/liveview_capture.h"
#include "core/psdk_lifecycle.h"
#include "stream/mjpeg_streamer.h"

namespace {

volatile std::sig_atomic_t g_stopRequested = 0;

void OnStopSignal(int) {
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

} // namespace

int main(int argc, char **argv) {
    std::signal(SIGINT, OnStopSignal);
    std::signal(SIGTERM, OnStopSignal);

    uint16_t port = 8080;
    int quality = 80;
    uint32_t maxFps = 25;
    double scale = 1.0;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--port=", 7) == 0) {
            if (std::sscanf(argv[i] + 7, "%hu", &port) != 1 || port == 0) {
                std::fprintf(stderr, "bad --port value\n");
                return 2;
            }
        } else if (std::strncmp(argv[i], "--quality=", 10) == 0) {
            if (std::sscanf(argv[i] + 10, "%d", &quality) != 1 || quality < 1 || quality > 100) {
                std::fprintf(stderr, "bad --quality value; expected 1..100\n");
                return 2;
            }
        } else if (std::strncmp(argv[i], "--max-fps=", 10) == 0) {
            if (std::sscanf(argv[i] + 10, "%u", &maxFps) != 1 || maxFps == 0 || maxFps > 60) {
                std::fprintf(stderr, "bad --max-fps value; expected 1..60\n");
                return 2;
            }
        } else if (std::strncmp(argv[i], "--scale=", 8) == 0) {
            if (std::sscanf(argv[i] + 8, "%lf", &scale) != 1 || scale <= 0.0 || scale > 1.0) {
                std::fprintf(stderr, "bad --scale value; expected 0<scale<=1\n");
                return 2;
            }
        } else {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 2;
        }
    }

    auto &lifecycle = manifold3::PsdkLifecycle::Get();
    auto &capture = manifold3::LiveviewCapture::Get();
    manifold3::stream::MjpegStreamer streamer;

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

    const bool streamerOk = streamer.Start(
        port, quality, maxFps, scale,
        [&capture](manifold3::capture::OwnedNv12Frame *frame) {
            return capture.WaitTake(frame, std::chrono::milliseconds(100));
        });
    if (!streamerOk) {
        std::fprintf(stderr, "MJPG streamer bind failed on port %u (in use?)\n", port);
        capture.Shutdown();
        lifecycle.Shutdown();
        return 1;
    }

    std::printf("stream demo: http://192.168.42.120:%u/  (Ctrl-C to quit)\n", streamer.port());
    std::fflush(stdout);

    uint64_t waitedFrames = 0;
    auto lastStatsPrint = std::chrono::steady_clock::now();
    while (!g_stopRequested) {
        // The stats loop must NOT WaitTake: the streamer thread is the sole
        // frame consumer, and a competing WaitTake would steal frames and
        // halve the stream rate. Sleep instead and poll the counters.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto now = std::chrono::steady_clock::now();
        if (now - lastStatsPrint >= std::chrono::seconds(1)) {
            const manifold3::LiveviewCapture::Stats s = capture.GetStats();
            const manifold3::stream::StreamerStats st = streamer.GetStats();
            const double fps = st.avg_frame_interval_ms > 0.0 ? 1000.0 / st.avg_frame_interval_ms
                                                              : 0.0;
            std::printf("[stats] fps=%.1f size=%ux%u source_drop=%llu handoff_drop=%llu "
                        "invalid=%llu enc_frames=%llu enc_fail=%llu clients=%u "
                        "avg_encode_ms=%.2f avg_interval_ms=%.2f rss_kb=%ld\n",
                        fps, s.width, s.height,
                        static_cast<unsigned long long>(s.source_dropped_frames),
                        static_cast<unsigned long long>(s.handoff_dropped_frames),
                        static_cast<unsigned long long>(s.invalid_frames),
                        static_cast<unsigned long long>(st.encoded_frames),
                        static_cast<unsigned long long>(st.encode_failures),
                        st.active_clients, st.avg_encode_ms, st.avg_frame_interval_ms,
                        ReadRssKb());
            std::fflush(stdout);
            lastStatsPrint = now;
        }
    }

    std::printf("Shutting down\n");
    streamer.Stop();
    capture.Shutdown();
    lifecycle.Shutdown();
    std::printf("stream demo stopped\n");
    return 0;
}
```

Modify `src/app/CMakeLists.txt`: delete the `capture_demo` block (lines 51-62) and append:

```cmake
# Capture-only demo for customer demonstration: MJPEG stream for a browser.
# No TensorRT dependency; links only stream/capture/core/platform + PSDK.
add_executable(stream_demo stream_demo.cpp)
target_link_options(stream_demo PRIVATE -pthread)
target_link_libraries(stream_demo PRIVATE
    stream
    capture
    core
    platform
    ${CMAKE_SOURCE_DIR}/third_party/psdk/psdk_lib/lib/aarch64-linux-gnu-gcc/libpayloadsdk.a
    m
    dl
)
```

Note: `stream` must appear before `jpeg`-dependent code is linked; the `stream` library itself links `jpeg`, so no extra link line is needed here.

- [ ] **Step 2: Cross-compile the demo**

Run: `source scripts/setup_env.sh && cmake --preset manifold3-cross-release && cmake --build --preset manifold3-cross-release --target stream_demo`
Expected: builds cleanly; `build-cross/src/app/stream_demo` is an AArch64 ELF.

- [ ] **Step 3: Host build still clean**

Run: `cmake --preset host-debug && cmake --build --preset host-debug`
Expected: builds cleanly; `stream_demo` is not built on host (app dir returns early), unit tests still pass.

- [ ] **Step 4: Verify ELF and dynamic dependencies**

Run: `readelf -h build-cross/src/app/stream_demo | grep -E "Class|Machine" && readelf -d build-cross/src/app/stream_demo | grep -E "NEEDED" | grep -E "jpeg|payload"` 
Expected: ELF64 AArch64; NEEDED includes `libjpeg.so.8` and `libpayloadsdk.a` symbols resolve (the demo links the PSDK archive, which pulls its static members).

- [ ] **Step 5: Commit**

```bash
git add src/app
git commit -m "feat: replace terminal capture demo with MJPEG stream demo"
```

---

### Task 5: Smoke script and demo guide

**Files:**
- Create: `scripts/run_mjpeg_smoke.sh`
- Delete: `docs/capture-demo-guide.md`
- Create: `docs/stream-demo-guide.md`
- Modify: `scripts/deploy.sh` only if it references `capture_demo` (it does not; skip)

**Interfaces:**
- Consumes: the built `stream_demo` binary; SSH access to the Manifold 3.
- Produces: `scripts/run_mjpeg_smoke.sh` (target-side stream validation), `docs/stream-demo-guide.md`.

- [ ] **Step 1: Write the smoke script**

Create `scripts/run_mjpeg_smoke.sh`:

```bash
#!/usr/bin/env bash
# Target-side smoke test for the MJPEG stream demo.
# Usage: scripts/run_mjpeg_smoke.sh <manifold3-ip> [--no-build]
# Prereqs: drone + Pilot online (liveview stream active), Smart3DExplore stopped.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SSH_KEY="${REPO_ROOT}/config/manifold3_id_rsa"
REMOTE_DIR="~/vision-detect"
REMOTE_BIN="${REMOTE_DIR}/stream_demo"
SSH_OPTS=(-i "${SSH_KEY}" -o StrictHostKeyChecking=no -o ConnectTimeout=10)

TARGET_IP="$1"
DO_BUILD=true
[ "${2:-}" = "--no-build" ] && DO_BUILD=false

if [ "${DO_BUILD}" = true ]; then
    source "${REPO_ROOT}/scripts/setup_env.sh"
    cmake --build "${REPO_ROOT}/build-cross" --target stream_demo -j"$(nproc)"
fi

scp "${SSH_OPTS[@]}" "${REPO_ROOT}/build-cross/src/app/stream_demo" "dji@${TARGET_IP}:${REMOTE_BIN}"
ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "chmod +x ${REMOTE_BIN}"

# Start the demo, capture a few seconds of stream, then stop it.
ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" \
  "pkill -f stream_demo 2>/dev/null; sleep 1; nohup ${REMOTE_BIN} --port=8080 >/tmp/stream_demo.log 2>&1 & sleep 4"

# Pull 1 MB of the stream and validate multipart + JPEG magic.
BYTES="$(ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" \
  \"curl -sN --max-time 5 http://127.0.0.1:8080/ | head -c 1048576 | base64 -w0\")"

echo "${BYTES}" | base64 -d > /tmp/stream_demo_capture.bin

grep -q -- "--frame" <(head -c 200 /tmp/stream_demo_capture.bin) || {
    echo "FAIL: multipart boundary not found"; exit 1; }
grep -q -- "Content-Type: image/jpeg" <(head -c 200 /tmp/stream_demo_capture.bin) || {
    echo "FAIL: JPEG content type missing"; exit 1; }

# First JPEG frame starts with FF D8; scan for the first SOI marker.
python3 - <<'EOF'
data = open("/tmp/stream_demo_capture.bin", "rb").read()
idx = data.find(b"\xff\xd8")
assert idx >= 0, "no JPEG SOI marker in stream"
# Find the matching EOI; crude: require at least 10k bytes after SOI.
assert len(data) - idx > 10240, "JPEG frame suspiciously small"
print("OK: JPEG frame starts at byte %d, payload %d bytes" % (idx, len(data) - idx))
EOF

ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "tail -5 /tmp/stream_demo.log"
ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "pkill -f stream_demo; sleep 1; echo stopped"
echo "MJPG smoke test passed"
```

- [ ] **Step 2: Write the demo guide**

Create `docs/stream-demo-guide.md` (replaces `docs/capture-demo-guide.md`; content in Chinese, this is the README-class end-user doc):

```markdown
# Stream Demo - 手动演示说明

## 前置条件

- 无人机已开机，Pilot 已连接（PSDK 需要飞机在线）
- Manifold 3 通过 USB 连接开发主机（IP 192.168.42.120）
- 演示分支 `demo/capture-demo`（含 `stream_demo` 可执行文件，部署到设备 `~/vision-detect/stream_demo`）

## 演示流程

### 1. 停掉占用 PSDK 通道的 Smart3DExplore

```bash
ssh -i config/manifold3_id_rsa -o StrictHostKeyChecking=no dji@192.168.42.120 \
  "dji_app_ctl stop Smart3DExplore; pkill -f Smart3DExplore 2>/dev/null; sleep 1"
```

> 若 `dji_app_ctl stop` 报错 257 属正常，`pkill` 兜底会生效。

### 2. 运行 demo（MJPEG 推流 + 统计）

```bash
ssh -i config/manifold3_id_rsa -o StrictHostKeyChecking=no dji@192.168.42.120 \
  "cd ~/vision-detect && ./stream_demo"
```

效果：设备在 8080 端口提供 MJPEG 流。开发主机浏览器打开
`http://192.168.42.120:8080/`，F11 全屏观看（1440x1080 全彩，约 25 fps）。
SSH 会话每秒一行统计：
`fps / size / source_drop / handoff_drop / invalid / enc_frames / enc_fail / clients / avg_encode_ms / avg_interval_ms / rss_kb`

`Ctrl-C` 干净退出。

### 3. 可选参数

| 参数 | 说明 |
|---|---|
| `--port=8080` | 推流端口（默认 8080） |
| `--quality=80` | JPEG 质量 1..100（默认 80） |
| `--max-fps=25` | 最大帧率 1..60（默认 25） |
| `--scale=0.66` | 输出缩放（默认 1.0；0.66 输出 1280x960） |

### 4. 演示结束恢复设备

```bash
ssh -i config/manifold3_id_rsa -o StrictHostKeyChecking=no dji@192.168.42.120 \
  "dji_app_ctl start Smart3DExplore"
```

## 常见问题

| 现象 | 原因与处理 |
|---|---|
| 浏览器画面卡住/空白 | 等 1-2 秒自动恢复（MJPEG 丢帧后浏览器等待下一帧）；确认飞机在线 |
| 启动报 "bind failed on port 8080" | 端口被占用，换 `--port=8081` 或检查残留进程 |
| 提示 "Address already in use"（PSDK） | Smart3DExplore 未停干净，重跑第 1 步 |
| "PSDK credentials not configured" | 凭据未注入，先运行 `scripts/configure_cross_with_credentials.sh` 再重新构建部署 |
| 统计行 fps=0 持续 | 飞机/Pilot 不在线，确认无人机开机并连接 Pilot |

## 重新构建部署（如需重编）

```bash
# 在 demo/capture-demo 分支上
source scripts/setup_env.sh
scripts/configure_cross_with_credentials.sh   # 注入真实凭据
cmake --build --preset manifold3-cross-release --target stream_demo
scp -i config/manifold3_id_rsa -o StrictHostKeyChecking=no \
  build-cross/src/app/stream_demo dji@192.168.42.120:~/vision-detect/
```

## 代码位置

- `src/app/stream_demo.cpp`（demo 分支新增，`main.cpp` 未改动）
- `src/stream/`（`mjpeg_streamer` 推流实现；`yuv_to_rgb`/`mjpeg_framing` 有宿主单元测试）
- `src/app/CMakeLists.txt` 末尾新增 `stream_demo` 目标（仅 cross 构建，无 TensorRT 依赖）
```

- [ ] **Step 3: Run the smoke script**

Run: `scripts/run_mjpeg_smoke.sh 192.168.42.120`
Expected: prints `OK: JPEG frame starts at byte N...`, demo stats tail, `MJPG smoke test passed`.

- [ ] **Step 4: Manual browser validation**

On the dev host: open `http://192.168.42.120:8080/` in Chrome, F11, confirm smooth full-resolution picture; then Ctrl-C the SSH demo and confirm clean shutdown lines.

- [ ] **Step 5: Commit**

```bash
git add scripts/run_mjpeg_smoke.sh docs/stream-demo-guide.md
git rm docs/capture-demo-guide.md
git commit -m "feat: add MJPEG stream smoke script and demo guide"
```

---

### Task 6: End-to-end target validation on Manifold 3

**Files:** none (validation only).

**Interfaces:** consumes the deployed `stream_demo` from Task 4/5.

- [ ] **Step 1: Deploy and run**

Run the smoke script per Task 5 Step 3. Expected: stream OK, JPEG frames present.

- [ ] **Step 2: Measure encode performance**

With the demo running under SSH, let it run 60+ seconds; confirm from the stats line:
- `avg_encode_ms` around 15-30 ms at 1440x1080 q80;
- steady `enc_frames`, no `enc_fail` growth;
- RSS stable (no unbounded growth);
- browser shows a smooth picture.

- [ ] **Step 3: Verify clean shutdown and device restore**

Ctrl-C the SSH session, confirm `stream demo stopped`; then run the restore command from the guide and confirm `dji_app_ctl start Smart3DExplore` succeeds.

- [ ] **Step 4: Full host test suite final pass**

Run: `cmake --build --preset host-debug && ctest --preset host-debug --output-on-failure`
Expected: all tests pass.

- [ ] **Step 5: Commit validation results**

If any findings: fix, re-run, commit with `fix:` prefix. No commit needed when everything passes.

---

## Self-Review

- **Spec coverage:** conversion (Task 1), framing (Task 2), streamer (Task 3), demo + removal of terminal render (Task 4), smoke + guide (Task 5), device validation incl. performance/clean shutdown (Task 6). All spec sections mapped.
- **Placeholders:** all steps carry concrete code and commands; the only open item is Task 6 Step 5 (contingent on findings), which is explicitly a no-op when validation passes.
- **Type consistency:** `MjpegStreamer::Start(port, quality, max_fps, scale, provider)`, `StreamerStats` fields, `YuvNv12ToRgb24`, `DownscaleRgb24`, `AppendHttpHeaders`, `AppendJpegPart`, `kMjpegBoundary` are defined in Task 1/2/3 and consumed identically in Task 3/4.
