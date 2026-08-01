#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace manifold3 {
namespace capture {

struct OwnedNv12Frame {
    std::vector<uint8_t> data;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frame_id = 0;
};

enum class FramePushResult {
    kStored,   // slot was empty; frame stored
    kReplaced, // previous frame overwritten (handoff drop)
    kInvalid,  // frame failed validation; not stored
};

bool IsValidNv12Frame(const uint8_t *data, uint32_t len, uint32_t width, uint32_t height);

class LatestFrameSlot {
  public:
    // Thread-safe. Returns kInvalid without storing when validation fails.
    FramePushResult Push(const uint8_t *data, uint32_t len, uint32_t width, uint32_t height,
                         uint32_t frame_id);

    // Blocks up to timeout for a stored frame; moves it out and clears the slot.
    // Returns false on timeout or after Stop().
    bool WaitTake(OwnedNv12Frame *frame, std::chrono::milliseconds timeout);

    // Wakes all waiters; subsequent WaitTake returns false immediately.
    void Stop();

    uint64_t replaced_frames() const;
    uint64_t invalid_frames() const;

  private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_ = false;
    bool has_frame_ = false;
    OwnedNv12Frame frame_;
    uint64_t replaced_frames_ = 0;
    uint64_t invalid_frames_ = 0;
};

} // namespace capture
} // namespace manifold3
