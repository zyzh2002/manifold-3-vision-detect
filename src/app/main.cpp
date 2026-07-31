#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

#include "capture/liveview_capture.h"
#include "core/psdk_lifecycle.h"

namespace {
volatile std::sig_atomic_t g_stopRequested = 0;

void OnStopSignal(int signalNum) {
    (void)signalNum;
    g_stopRequested = 1;
}

// Prints the resident set size of this process from /proc/self/status.
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

void PrintStats(const manifold3::LiveviewCapture::Stats &stats) {
    std::printf("frames=%llu dropped=%llu size=%ux%u bytes=%llu "
                "interval[ms] min=%.1f avg=%.1f max=%.1f rss_kb=%ld\n",
                static_cast<unsigned long long>(stats.totalFrames),
                static_cast<unsigned long long>(stats.droppedFrames), stats.width, stats.height,
                static_cast<unsigned long long>(stats.totalBytes), stats.minIntervalMs, stats.avgIntervalMs,
                stats.maxIntervalMs, ReadRssKb());
}
} // namespace

int main() {
    std::signal(SIGINT, OnStopSignal);
    std::signal(SIGTERM, OnStopSignal);

    auto &lifecycle = manifold3::PsdkLifecycle::Get();
    auto &capture = manifold3::LiveviewCapture::Get();

    if (!lifecycle.Initialize()) {
        std::fprintf(stderr, "PSDK initialization failed\n");
        return 1;
    }
    std::printf("PSDK initialized\n");

    if (!capture.Initialize()) {
        std::fprintf(stderr, "Liveview initialization failed\n");
        lifecycle.Shutdown();
        return 1;
    }
    std::printf("Liveview initialized\n");

    if (!lifecycle.Start()) {
        std::fprintf(stderr, "PSDK application start failed\n");
        capture.Shutdown();
        lifecycle.Shutdown();
        return 1;
    }
    std::printf("PSDK application started\n");

    if (!capture.Start()) {
        std::fprintf(stderr, "Liveview capture start failed\n");
        capture.Shutdown();
        lifecycle.Shutdown();
        return 1;
    }
    std::printf("Liveview capture started\n");

    while (!g_stopRequested) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        PrintStats(capture.GetStats());
    }

    std::printf("Shutting down\n");
    capture.Shutdown();
    lifecycle.Shutdown();
    std::printf("PSDK deinitialized\n");
    return 0;
}
