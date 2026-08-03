// Liveview demo for customer demonstration.
//
// Serves the Manifold 3 NV12 liveview stream as MJPEG over HTTP:
// open http://192.168.42.120:8081/ in a browser (F11 for fullscreen).
// Prints one per-second statistics line to stdout. Throwaway demo binary:
// it does not load any inference engine.
//
// Usage (on the device, or via ssh):
//   ./stream_demo [--port=8081] [--quality=80] [--max-fps=25] [--scale=1.0]

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

    uint16_t port = 8081; // device port 8080 is taken
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
            // WaitTake returns false on timeout too; a transient frame gap
            // must not stop the stream. Only the stop signal ends it.
            const bool got = capture.WaitTake(frame, std::chrono::milliseconds(100));
            return got || !g_stopRequested;
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
