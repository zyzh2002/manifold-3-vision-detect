#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <vector>

#include "capture/liveview_capture.h"
#include "core/psdk_lifecycle.h"
#include "inference/inference_types.h"
#include "inference/pipeline_metrics.h"
#include "inference/postprocess.h"
#include "inference/preprocess.h"
#include "inference/synthetic_engine_contract.h"
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

    manifold3::inference::PipelineWindowStats window;
    auto lastReport = std::chrono::steady_clock::now();

    while (!g_stopRequested) {
        manifold3::capture::OwnedNv12Frame frame;
        if (!capture.WaitTake(&frame, std::chrono::milliseconds(500))) {
            if (g_stopRequested) {
                break;
            }
            continue;
        }
        const uint32_t w = frame.width;
        const uint32_t h = frame.height;
        const auto frameStart = std::chrono::steady_clock::now();

        const auto preStart = std::chrono::steady_clock::now();
        std::vector<float> nchw;
        if (!manifold3::inference::PreprocessNv12ToNchw(frame.data.data(), w, h, manifold3::inference::kInputSize,
                                                        manifold3::inference::kInputSize, &nchw)) {
            std::fprintf(stderr, "preprocess failed\n");
            continue;
        }
        const auto preEnd = std::chrono::steady_clock::now();

        manifold3::inference::SyntheticOutputs outputs;
        manifold3::inference::EngineTiming timing;
        if (!engine.Infer(nchw, &outputs, &timing)) {
            std::fprintf(stderr, "infer failed\n");
            continue;
        }

        const auto postStart = std::chrono::steady_clock::now();
        std::vector<manifold3::inference::Detection> dets;
        if (!manifold3::inference::DecodeSyntheticSeg(outputs, &dets)) {
            std::fprintf(stderr, "decode failed\n");
            continue;
        }
        const auto frameEnd = std::chrono::steady_clock::now();

        window.preprocess_us.Add(std::chrono::duration_cast<std::chrono::microseconds>(preEnd - preStart).count());
        window.host_to_device_us.Add(timing.host_to_device_us);
        window.execute_us.Add(timing.execute_us);
        window.device_to_host_us.Add(timing.device_to_host_us);
        window.engine_total_us.Add(timing.total_us);
        window.postprocess_us.Add(std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - postStart).count());
        window.end_to_end_us.Add(std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart).count());
        ++window.frames;
        window.detections += dets.size();

        const auto now = std::chrono::steady_clock::now();
        if (now - lastReport >= std::chrono::seconds(1)) {
            const double seconds = std::chrono::duration<double>(now - lastReport).count();
            const double fps = seconds > 0.0 ? static_cast<double>(window.frames) / seconds : 0.0;
            const manifold3::LiveviewCapture::Stats stats = capture.GetStats();
            std::printf("pipeline synthetic=true fps=%.1f frames=%llu detections=%llu "
                        "pre_avg_us=%lld pre_p95_us=%lld "
                        "h2d_avg_us=%lld exec_avg_us=%lld d2h_avg_us=%lld eng_avg_us=%lld eng_p95_us=%lld "
                        "post_avg_us=%lld post_p95_us=%lld "
                        "e2e_avg_us=%lld e2e_p95_us=%lld e2e_max_us=%lld "
                        "source_drop=%llu handoff_drop=%llu invalid=%llu rss_kb=%ld\n",
                        fps, static_cast<unsigned long long>(window.frames),
                        static_cast<unsigned long long>(window.detections),
                        static_cast<long long>(window.preprocess_us.average_us()),
                        static_cast<long long>(window.preprocess_us.percentile_us(0.95)),
                        static_cast<long long>(window.host_to_device_us.average_us()),
                        static_cast<long long>(window.execute_us.average_us()),
                        static_cast<long long>(window.device_to_host_us.average_us()),
                        static_cast<long long>(window.engine_total_us.average_us()),
                        static_cast<long long>(window.engine_total_us.percentile_us(0.95)),
                        static_cast<long long>(window.postprocess_us.average_us()),
                        static_cast<long long>(window.postprocess_us.percentile_us(0.95)),
                        static_cast<long long>(window.end_to_end_us.average_us()),
                        static_cast<long long>(window.end_to_end_us.percentile_us(0.95)),
                        static_cast<long long>(window.end_to_end_us.max_us()),
                        static_cast<unsigned long long>(stats.source_dropped_frames),
                        static_cast<unsigned long long>(stats.handoff_dropped_frames),
                        static_cast<unsigned long long>(stats.invalid_frames), ReadRssKb());
            window.Clear();
            lastReport = now;
        }
    }

    capture.Shutdown();
    lifecycle.Shutdown();
    std::printf("PSDK deinitialized\n");
    return 0;
}
