// Capture demo for customer demonstration.
//
// Shows the Manifold 3 NV12 video stream live in a terminal using ANSI
// truecolor half-block characters, plus one per-second statistics line.
// This is a throwaway demo binary: it does not load any inference engine.
//
// Usage (on the device, or via ssh -t):
//   ./capture_demo [--no-render] [--grid WxH]
//
// --no-render disables the terminal picture (statistics only).
// --grid WxH sets the render grid in characters (default 100x25).

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "capture/liveview_capture.h"
#include "core/psdk_lifecycle.h"

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

// Converts one NV12 pixel pair (Y, U, V) into RGB in [0, 255].
void YuvToRgb(uint8_t y, uint8_t u, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    const float fy = static_cast<float>(y);
    const float fu = static_cast<float>(u) - 128.0f;
    const float fv = static_cast<float>(v) - 128.0f;
    const float fr = fy + 1.402f * fv;
    const float fg = fy - 0.344136f * fu - 0.714136f * fv;
    const float fb = fy + 1.772f * fu;
    const auto clamp255 = [](float v) {
        const float c = v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
        return static_cast<uint8_t>(c + 0.5f);
    };
    *r = clamp255(fr);
    *g = clamp255(fg);
    *b = clamp255(fb);
}

// Renders one NV12 frame into the terminal at the cursor home position using
// ANSI truecolor half-block characters. Each character row holds two pixel
// rows (upper half = foreground color, lower half = background color).
void RenderFrame(const uint8_t *nv12, uint32_t width, uint32_t height, uint32_t gridW, uint32_t gridH) {
    const uint32_t pixelH = gridH * 2;
    const float sx = static_cast<float>(width) / static_cast<float>(gridW);
    const float sy = static_cast<float>(height) / static_cast<float>(pixelH);
    const uint32_t uvWidth = width / 2;

    std::string out;
    out.reserve(static_cast<size_t>(gridW) * gridH * 32 + 32);
    out += "\033[H";
    for (uint32_t row = 0; row < gridH; ++row) {
        for (uint32_t col = 0; col < gridW; ++col) {
            const uint32_t px0 = static_cast<uint32_t>(static_cast<float>(col) * sx);
            const uint32_t px1 = static_cast<uint32_t>(static_cast<float>(col + 1) * sx - 1.0f);
            const uint32_t py0 = static_cast<uint32_t>(static_cast<float>(row * 2) * sy);
            const uint32_t py1 = static_cast<uint32_t>(static_cast<float>(row * 2 + 1) * sy);

            auto pixel = [&](uint32_t px, uint32_t py, uint8_t *r, uint8_t *g, uint8_t *b) {
                const uint8_t y = nv12[py * width + px];
                const uint32_t uvIndex = height * width + (py / 2) * uvWidth + (px / 2) * 2;
                YuvToRgb(y, nv12[uvIndex], nv12[uvIndex + 1], r, g, b);
            };

            uint8_t r0, g0, b0, r1, g1, b1;
            pixel(px0, py0, &r0, &g0, &b0);
            pixel(px1, py1, &r1, &g1, &b1);

            char cell[64];
            std::snprintf(cell, sizeof(cell), "\033[38;2;%u;%u;%um\033[48;2;%u;%u;%um\u2580", r0, g0, b0, r1, g1, b1);
            out += cell;
        }
        out += "\033[0m\n";
    }
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char **argv) {
    std::signal(SIGINT, OnStopSignal);
    std::signal(SIGTERM, OnStopSignal);

    bool render = true;
    uint32_t gridW = 100;
    uint32_t gridH = 25;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-render") == 0) {
            render = false;
        } else if (std::strncmp(argv[i], "--grid=", 7) == 0) {
            if (std::sscanf(argv[i] + 7, "%ux%u", &gridW, &gridH) != 2 || gridW == 0 || gridH == 0) {
                std::fprintf(stderr, "bad --grid value; expected WxH\n");
                return 2;
            }
        } else {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 2;
        }
    }

    auto &lifecycle = manifold3::PsdkLifecycle::Get();
    auto &capture = manifold3::LiveviewCapture::Get();

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

    if (render) {
        std::printf("\033[2J\033[Hcapture demo: render %ux%u chars, Ctrl-C to quit\n", gridW, gridH);
        std::fflush(stdout);
    } else {
        std::printf("capture demo (statistics only), Ctrl-C to quit\n");
    }

    uint64_t renderedFrames = 0;
    uint64_t waitedFrames = 0;
    auto lastStatsPrint = std::chrono::steady_clock::now();
    auto lastRender = std::chrono::steady_clock::now();

    while (!g_stopRequested) {
        manifold3::capture::OwnedNv12Frame frame;
        if (!capture.WaitTake(&frame, std::chrono::milliseconds(100))) {
            continue;
        }
        ++waitedFrames;

        const auto now = std::chrono::steady_clock::now();
        if (render && now - lastRender >= std::chrono::milliseconds(100)) {
            RenderFrame(frame.data.data(), frame.width, frame.height, gridW, gridH);
            lastRender = now;
            ++renderedFrames;
        }

        if (now - lastStatsPrint >= std::chrono::seconds(1)) {
            const manifold3::LiveviewCapture::Stats s = capture.GetStats();
            const double seconds = std::chrono::duration<double>(now - lastStatsPrint).count();
            std::printf("\033[0m[stats] fps=%.1f size=%ux%u source_drop=%llu handoff_drop=%llu "
                        "invalid=%llu waited=%llu rendered=%llu rss_kb=%ld\n",
                        static_cast<double>(waitedFrames) / seconds, frame.width, frame.height,
                        static_cast<unsigned long long>(s.source_dropped_frames),
                        static_cast<unsigned long long>(s.handoff_dropped_frames),
                        static_cast<unsigned long long>(s.invalid_frames),
                        static_cast<unsigned long long>(waitedFrames),
                        static_cast<unsigned long long>(renderedFrames), ReadRssKb());
            std::fflush(stdout);
            lastStatsPrint = now;
            waitedFrames = 0;
        }
    }

    std::printf("\033[0mShutting down\n");
    capture.Shutdown();
    lifecycle.Shutdown();
    std::printf("capture demo stopped\n");
    return 0;
}
