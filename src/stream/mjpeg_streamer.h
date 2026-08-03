#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "capture/latest_frame_slot.h"

namespace manifold3 {
namespace stream {

struct StreamerStats {
    uint64_t encoded_frames = 0;
    uint64_t encode_failures = 0;
    uint64_t clients_served = 0; // accepted connections (total)
    uint32_t active_clients = 0; // current snapshot
    double avg_encode_ms = 0.0;
    double avg_frame_interval_ms = 0.0;
};

// Serves NV12 frames as an MJPEG multipart/x-mixed-replace HTTP stream.
// One worker thread: poll() on the listen socket and clients, pull frames via
// the FrameProvider, encode with libjpeg-turbo, broadcast to all clients.
class MjpegStreamer {
  public:
    // Pulls one frame; must return false when the streamer should stop.
    using FrameProvider = std::function<bool(capture::OwnedNv12Frame *frame)>;

    MjpegStreamer() = default;
    ~MjpegStreamer();
    MjpegStreamer(const MjpegStreamer &) = delete;
    MjpegStreamer &operator=(const MjpegStreamer &) = delete;

    // Binds 0.0.0.0:port and starts the worker thread. port 0 requests an
    // OS-assigned port (read it via port()). scale <= 1.0 downscales the
    // output. Returns false if the bind or thread spawn fails.
    bool Start(uint16_t port, int quality, uint32_t max_fps, double scale, FrameProvider provider);

    // Stops the worker thread and closes all sockets. Idempotent.
    void Stop();

    // Actual bound port (valid after Start).
    uint16_t port() const;

    StreamerStats GetStats() const;

  private:
    void WorkerLoop();
    void AcceptClient(int fd);

    mutable std::mutex mutex_;
    int listen_fd_ = -1;
    bool running_ = false;
    bool stop_requested_ = false;
    uint16_t port_ = 0;
    int quality_ = 80;
    uint32_t max_fps_ = 25;
    double scale_ = 1.0;
    FrameProvider provider_;
    std::vector<int> clients_;
    std::thread worker_;
    StreamerStats stats_;
};

} // namespace stream
} // namespace manifold3
