#pragma once

#include <cstdint>
#include <mutex>

#include <dji_liveview.h>

namespace manifold3 {

// Starts one Matrice 4E visible-light NV12 image stream and reports frame
// statistics (count, drops, size, callback interval). The PSDK image callback
// runs on an SDK-owned thread; the buffer is only valid during the callback
// and is never retained here.
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

    // Deinitializes the liveview module.
    void Shutdown();

    struct Stats {
        uint64_t totalFrames = 0;
        uint64_t droppedFrames = 0;
        uint32_t lastFrameId = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        uint64_t totalBytes = 0;
        double minIntervalMs = 0.0;
        double maxIntervalMs = 0.0;
        double avgIntervalMs = 0.0;
    };

    // Thread-safe snapshot of the callback counters.
    Stats GetStats() const;

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
};

} // namespace manifold3
