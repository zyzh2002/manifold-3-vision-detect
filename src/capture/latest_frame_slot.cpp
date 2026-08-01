#include "capture/latest_frame_slot.h"

#include <algorithm>

namespace manifold3 {
namespace capture {

bool IsValidNv12Frame(const uint8_t *data, uint32_t len, uint32_t width, uint32_t height) {
    if (data == nullptr) {
        return false;
    }
    if (width == 0 || height == 0) {
        return false;
    }
    if ((width & 1) != 0 || (height & 1) != 0) {
        return false;
    }
    const uint64_t expected = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 3 / 2;
    if (expected > UINT32_MAX) {
        return false;
    }
    return len == static_cast<uint32_t>(expected);
}

FramePushResult LatestFrameSlot::Push(const uint8_t *data, uint32_t len, uint32_t width, uint32_t height,
                                      uint32_t frame_id) {
    if (!IsValidNv12Frame(data, len, width, height)) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++invalid_frames_;
        return FramePushResult::kInvalid;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    FramePushResult result = has_frame_ ? FramePushResult::kReplaced : FramePushResult::kStored;
    if (result == FramePushResult::kReplaced) {
        ++replaced_frames_;
    }
    frame_.data.assign(data, data + len);
    frame_.width = width;
    frame_.height = height;
    frame_.frame_id = frame_id;
    has_frame_ = true;
    cv_.notify_all();
    return result;
}

bool LatestFrameSlot::WaitTake(OwnedNv12Frame *frame, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] { return stopped_ || has_frame_; })) {
        return false;
    }
    if (stopped_ && !has_frame_) {
        return false;
    }
    *frame = std::move(frame_);
    frame_ = OwnedNv12Frame();
    has_frame_ = false;
    return true;
}

void LatestFrameSlot::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    cv_.notify_all();
}

uint64_t LatestFrameSlot::replaced_frames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return replaced_frames_;
}

uint64_t LatestFrameSlot::invalid_frames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return invalid_frames_;
}

} // namespace capture
} // namespace manifold3
