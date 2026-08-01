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
    if (DjiLiveview_StartImageStream(DJI_LIVEVIEW_CAMERA_POSITION_NO_1, DJI_LIVEVIEW_CAMERA_SOURCE_M4T_VIS,
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
    if (DjiLiveview_StopImageStream(DJI_LIVEVIEW_CAMERA_POSITION_NO_1, DJI_LIVEVIEW_CAMERA_SOURCE_M4T_VIS) !=
        DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::fprintf(stderr, "DjiLiveview_StopImageStream failed\n");
    }
    started_ = false;
}

void LiveviewCapture::Shutdown() {
    // Wake any WaitTake() waiter first so the main loop cannot hang while the
    // stream is being torn down.
    frame_slot_.Stop();
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

    auto &capture = Get();

    // Reject non-NV12 formats before the slot push; the slot validates
    // dimensions/length itself.
    if (imageInfo.pixFmt != PIXFMT_NV12) {
        std::lock_guard<std::mutex> lock(capture.statsMutex_);
        ++capture.stats_.invalid_frames;
        return;
    }

    const manifold3::capture::FramePushResult pushResult =
        capture.frame_slot_.Push(buf, len, imageInfo.width, imageInfo.height, imageInfo.frameId);

    const int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch())
                              .count();

    // Push is called before taking the stats lock; the two locks are never
    // nested.
    std::lock_guard<std::mutex> lock(capture.statsMutex_);
    Stats &stats = capture.stats_;

    if (pushResult == manifold3::capture::FramePushResult::kInvalid) {
        ++stats.invalid_frames;
        return;
    }

    if (stats.total_frames > 0 && imageInfo.frameId > stats.last_frame_id + 1) {
        stats.source_dropped_frames += imageInfo.frameId - stats.last_frame_id - 1;
    }

    if (capture.lastIntervalUs_ != 0) {
        const double intervalMs = static_cast<double>(nowUs - capture.lastIntervalUs_) * kUsToMs;
        stats.min_interval_ms = (stats.total_frames == 1) ? intervalMs : std::min(stats.min_interval_ms, intervalMs);
        stats.max_interval_ms = std::max(stats.max_interval_ms, intervalMs);
        stats.avg_interval_ms =
            (stats.avg_interval_ms * (stats.total_frames - 1) + intervalMs) / stats.total_frames;
    }
    capture.lastIntervalUs_ = nowUs;

    stats.last_frame_id = imageInfo.frameId;
    stats.width = imageInfo.width;
    stats.height = imageInfo.height;
    stats.total_bytes += len;
    ++stats.total_frames;
    if (pushResult == manifold3::capture::FramePushResult::kReplaced) {
        ++stats.handoff_dropped_frames;
    }
}

bool LiveviewCapture::WaitTake(manifold3::capture::OwnedNv12Frame *frame,
                               std::chrono::milliseconds timeout) {
    return frame_slot_.WaitTake(frame, timeout);
}

} // namespace manifold3
