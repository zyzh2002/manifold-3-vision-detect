#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

#include <dji_liveview.h>

#include "capture/latest_frame_slot.h"

namespace manifold3 {

// Starts one Matrice 4T visible-light NV12 image stream and reports frame
// statistics (count, source/handoff drops, invalids, size, callback interval).
// The PSDK image callback runs on an SDK-owned thread; the PSDK buffer is only
// valid during the callback, so each validated frame is copied into a bounded
// latest-wins slot that a consumer drains via WaitTake().
class LiveviewCapture {
  public:
    static LiveviewCapture &Get();

    // Initializes the liveview module. Must be called after
    // PsdkLifecycle::Initialize() and before PsdkLifecycle::Start().
    bool Initialize();

    // Starts the NV12 image stream. Must be called after PsdkLifecycle::Start().
    bool Start();

    // Stops the image stream.
    void Stop();

    // Deinitializes the liveview module and wakes any WaitTake() waiter.
    void Shutdown();

    struct Stats {
        uint64_t total_frames = 0;           // valid NV12 frames stored
        uint64_t source_dropped_frames = 0;  // PSDK frameId gaps
        uint64_t handoff_dropped_frames = 0; // latest-wins replacements
        uint64_t invalid_frames = 0;         // validation failures
        uint32_t last_frame_id = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        uint64_t total_bytes = 0;
        double min_interval_ms = 0.0;
        double max_interval_ms = 0.0;
        double avg_interval_ms = 0.0;
    };

    // Thread-safe snapshot of the callback counters.
    Stats GetStats() const;

    // Waits up to timeout for the latest NV12 frame, moves it out and clears
    // the slot. Returns false on timeout or after Shutdown(). The buffer is
    // owned by the caller and the latest-wins slot never accumulates memory.
    bool WaitTake(manifold3::capture::OwnedNv12Frame *frame, std::chrono::milliseconds timeout);

    LiveviewCapture(const LiveviewCapture &) = delete;
    LiveviewCapture &operator=(const LiveviewCapture &) = delete;

  private:
    LiveviewCapture() = default;
    ~LiveviewCapture() = default;

    static void OnImage(E_DjiLiveViewCameraPosition position, const uint8_t *buf, uint32_t len,
                        T_DjiLiveviewImageInfo imageInfo);

    bool initialized_ = false;
    bool started_ = false;
    mutable std::mutex statsMutex_;
    Stats stats_;
    uint64_t lastIntervalUs_ = 0;
    capture::LatestFrameSlot frame_slot_;
};

} // namespace manifold3
