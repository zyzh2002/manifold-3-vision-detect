#include "capture/liveview_capture.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace manifold3 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr double kUsToMs = 1e-3;

} // namespace

LiveviewCapture &LiveviewCapture::Get() {
    static LiveviewCapture instance;
    return instance;
}

bool LiveviewCapture::Initialize() {
    if (initialized_) {
        return true;
    }
    if (DjiLiveview_Init() != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::fprintf(stderr, "DjiLiveview_Init failed\n");
        return false;
    }
    initialized_ = true;
    return true;
}

bool LiveviewCapture::Start() {
    if (!initialized_) {
        std::fprintf(stderr, "LiveviewCapture::Start called before Initialize\n");
        return false;
    }
    if (started_) {
        return true;
    }
    if (DjiLiveview_StartImageStream(DJI_LIVEVIEW_CAMERA_POSITION_NO_1, DJI_LIVEVIEW_CAMERA_SOURCE_M4E_VIS,
                                     PIXFMT_NV12, LiveviewCapture::OnImage) !=
        DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::fprintf(stderr, "DjiLiveview_StartImageStream failed\n");
        return false;
    }
    started_ = true;
    return true;
}

void LiveviewCapture::Stop() {
    if (!started_) {
        return;
    }
    if (DjiLiveview_StopImageStream(DJI_LIVEVIEW_CAMERA_POSITION_NO_1, DJI_LIVEVIEW_CAMERA_SOURCE_M4E_VIS) !=
        DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::fprintf(stderr, "DjiLiveview_StopImageStream failed\n");
    }
    started_ = false;
}

void LiveviewCapture::Shutdown() {
    Stop();
    if (initialized_) {
        if (DjiLiveview_Deinit() != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            std::fprintf(stderr, "DjiLiveview_Deinit failed\n");
        }
    }
    initialized_ = false;
}

LiveviewCapture::Stats LiveviewCapture::GetStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void LiveviewCapture::OnImage(E_DjiLiveViewCameraPosition position, const uint8_t *buf, uint32_t len,
                              T_DjiLiveviewImageInfo imageInfo) {
    (void)position;
    (void)buf;

    auto &capture = Get();
    const int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch())
                              .count();

    std::lock_guard<std::mutex> lock(capture.statsMutex_);
    Stats &stats = capture.stats_;

    if (stats.totalFrames > 0 && imageInfo.frameId > stats.lastFrameId + 1) {
        stats.droppedFrames += imageInfo.frameId - stats.lastFrameId - 1;
    }

    if (capture.lastIntervalUs_ != 0) {
        const double intervalMs = static_cast<double>(nowUs - capture.lastIntervalUs_) * kUsToMs;
        stats.minIntervalMs = (stats.totalFrames == 1) ? intervalMs : std::min(stats.minIntervalMs, intervalMs);
        stats.maxIntervalMs = std::max(stats.maxIntervalMs, intervalMs);
        stats.avgIntervalMs = (stats.avgIntervalMs * (stats.totalFrames - 1) + intervalMs) / stats.totalFrames;
    }
    capture.lastIntervalUs_ = nowUs;

    stats.lastFrameId = imageInfo.frameId;
    stats.width = imageInfo.width;
    stats.height = imageInfo.height;
    stats.totalBytes += len;
    stats.totalFrames++;
}

} // namespace manifold3
