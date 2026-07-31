#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "capture/liveview_capture.h"
#include "core/psdk_lifecycle.h"
#include "inference/inference_types.h"
#include "inference/postprocess.h"
#include "inference/preprocess.h"
#include "inference/tensorrt_engine.h"

namespace {
volatile std::sig_atomic_t g_stopRequested = 0;

void OnStopSignal(int signalNum) {
    (void)signalNum;
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
}  // namespace

int main(int argc, char **argv) {
    std::signal(SIGINT, OnStopSignal);
    std::signal(SIGTERM, OnStopSignal);

    const std::string enginePath =
        argc > 1 ? argv[1] : std::string("/home/dji/vision-detect/dummy_yolo11_seg.engine");

    auto &lifecycle = manifold3::PsdkLifecycle::Get();
    auto &capture = manifold3::LiveviewCapture::Get();
    manifold3::inference::TensorRtEngine engine;

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
    if (!engine.Load(enginePath)) {
        std::fprintf(stderr, "engine load failed: %s\n", enginePath.c_str());
        capture.Shutdown();
        lifecycle.Shutdown();
        return 1;
    }
    std::printf("PSDK + capture + engine ready\n");

    uint64_t inferenceCount = 0;
    int64_t latencySumUs = 0;
    int64_t latencyMaxUs = 0;
    std::vector<int64_t> latencySamples;
    uint64_t detectionsTotal = 0;
    auto lastReport = std::chrono::steady_clock::now();

    while (!g_stopRequested) {
        std::vector<uint8_t> frame;
        uint32_t w = 0, h = 0;
        if (!capture.TakeFrame(&frame, &w, &h)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        std::vector<float> nchw;
        if (!manifold3::inference::PreprocessNv12ToNchw(frame.data(), w, h,
                                                        manifold3::inference::kInputSize,
                                                        manifold3::inference::kInputSize, &nchw)) {
            std::fprintf(stderr, "preprocess failed\n");
            continue;
        }
        std::vector<float> out0, out1, out2;
        int64_t latencyUs = 0;
        if (!engine.Infer(nchw, &out0, &out1, &out2, &latencyUs)) {
            std::fprintf(stderr, "infer failed\n");
            continue;
        }
        constexpr uint32_t kChannels =
            4 + manifold3::inference::kNumSpecies + manifold3::inference::kNumAgeBins + 32;
        const uint32_t anchors = static_cast<uint32_t>(out0.size() / kChannels);
        std::vector<manifold3::inference::Detection> dets;
        manifold3::inference::DecodeYolo11Seg(out0, out1, out2, anchors, 160, 160, &dets);

        ++inferenceCount;
        latencySumUs += latencyUs;
        latencyMaxUs = std::max(latencyMaxUs, latencyUs);
        latencySamples.push_back(latencyUs);
        detectionsTotal += dets.size();

        const auto now = std::chrono::steady_clock::now();
        if (now - lastReport >= std::chrono::seconds(1)) {
            std::sort(latencySamples.begin(), latencySamples.end());
            const int64_t p95 = latencySamples.empty()
                                    ? 0
                                    : latencySamples[static_cast<size_t>(latencySamples.size() * 0.95f)];
            std::printf("infer frames=%llu avg_us=%lld p95_us=%lld max_us=%lld dets=%llu rss_kb=%ld\n",
                        static_cast<unsigned long long>(inferenceCount),
                        static_cast<long long>(inferenceCount ? latencySumUs / static_cast<int64_t>(inferenceCount)
                                                              : 0),
                        static_cast<long long>(p95), static_cast<long long>(latencyMaxUs),
                        static_cast<unsigned long long>(detectionsTotal), ReadRssKb());
            lastReport = now;
            latencySamples.clear();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    capture.Shutdown();
    lifecycle.Shutdown();
    std::printf("PSDK deinitialized\n");
    return 0;
}
