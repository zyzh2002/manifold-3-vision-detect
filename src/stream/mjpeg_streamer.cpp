#include "stream/mjpeg_streamer.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <jpeglib.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "stream/mjpeg_framing.h"
#include "stream/yuv_to_rgb.h"

namespace manifold3 {
namespace stream {

namespace {

constexpr int kPollTimeoutMs = 100;
constexpr int kRecvBufSize = 4096;
constexpr std::chrono::milliseconds kSendTimeoutMs(500);

void SetNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void SetSendTimeout(int fd, std::chrono::milliseconds timeout) {
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

struct JpegErrorState {
    jpeg_error_mgr pub;
    jmp_buf jump;
};

void JpegErrorExit(j_common_ptr cinfo) {
    JpegErrorState *err = reinterpret_cast<JpegErrorState *>(cinfo->err);
    longjmp(err->jump, 1);
}

} // namespace

MjpegStreamer::~MjpegStreamer() {
    Stop();
}

bool MjpegStreamer::Start(uint16_t port, int quality, uint32_t max_fps, double scale, FrameProvider provider) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return false;
    }
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 || listen(fd, 8) < 0) {
        close(fd);
        return false;
    }
    sockaddr_in bound{};
    socklen_t boundLen = sizeof(bound);
    getsockname(fd, reinterpret_cast<sockaddr *>(&bound), &boundLen);
    listen_fd_ = fd;
    port_ = ntohs(bound.sin_port);
    quality_ = quality < 1 ? 1 : (quality > 100 ? 100 : quality);
    max_fps_ = max_fps == 0 ? 1 : max_fps;
    // Clamp scale to the downscale-only contract: an out-of-range value would
    // reach DownscaleRgb24's division-by-zero path (bw/bh = 0 when upscaling).
    scale_ = (scale <= 0.0 || scale > 1.0) ? 1.0 : scale;
    provider_ = std::move(provider);
    stop_requested_ = false;
    running_ = true;
    // A finished-but-unjoined worker (natural provider-false exit) must be
    // joined before spawning a new one: move-assigning a std::thread over a
    // joinable thread calls std::terminate. The worker never takes mutex_
    // again after setting running_ = false, so joining under the lock cannot
    // deadlock.
    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread(&MjpegStreamer::WorkerLoop, this);
    return true;
}

void MjpegStreamer::Stop() {
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true; // idempotent
        if (running_ && listen_fd_ >= 0) {
            shutdown(listen_fd_, SHUT_RDWR);
            close(listen_fd_);
            listen_fd_ = -1;
        }
        if (worker_.joinable()) {
            worker = std::move(worker_);
        }
    }
    // Join unconditionally: a finished-but-unjoined worker thread is still
    // joinable, and destroying it without joining calls std::terminate.
    // The worker's poll and frame-provider calls are bounded, so join
    // returns promptly even when the worker exited via the provider path.
    if (worker.joinable()) {
        worker.join();
    }
}

uint16_t MjpegStreamer::port() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return port_;
}

StreamerStats MjpegStreamer::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void MjpegStreamer::AcceptClient(int fd) {
    SetNonBlocking(fd);
    SetSendTimeout(fd, kSendTimeoutMs);
    // The HTTP response must precede the first multipart part, otherwise
    // clients (browsers, curl) cannot parse the stream and wait forever.
    std::vector<uint8_t> headers;
    AppendHttpHeaders(&headers);
    ssize_t sent = 0;
    while (sent < static_cast<ssize_t>(headers.size())) {
        const ssize_t n = send(fd, headers.data() + sent, headers.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            close(fd);
            return;
        }
        sent += n;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.push_back(fd);
    ++stats_.clients_served;
}

void MjpegStreamer::WorkerLoop() {
    std::vector<uint8_t> rgb;
    std::vector<uint8_t> jpeg;
    std::vector<pollfd> pollfds;
    auto lastFrameAt = std::chrono::steady_clock::now();
    // Deadline of the next allowed frame pull. Polling waits until this
    // deadline so the poll timeout does not dominate the frame cadence.
    auto nextFrameDue = lastFrameAt;
    const auto frameInterval = std::chrono::milliseconds(1000 / max_fps_);
    uint64_t frameIntervalUs = 0;
    uint64_t frameIntervalCount = 0;
    uint64_t encodeUs = 0;
    uint64_t encodeCount = 0;

    while (true) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_) {
                break;
            }
        }

        // Build the poll set from the current client list.
        pollfds.clear();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pollfds.push_back(pollfd{listen_fd_, POLLIN, 0});
            for (int fd : clients_) {
                pollfds.push_back(pollfd{fd, POLLIN, 0});
            }
        }
        // Sleep until the next frame is due (or until client I/O wakes us).
        // When the deadline is already past (slow frame), poll with timeout 0:
        // a full kPollTimeoutMs stall per cycle would collapse the frame rate
        // to ~1/kPollTimeoutMs once frames take longer than the interval.
        const auto pollDeadline = std::chrono::steady_clock::now();
        int pollTimeoutMs = kPollTimeoutMs;
        if (nextFrameDue > pollDeadline) {
            const auto untilDue = std::chrono::duration_cast<std::chrono::milliseconds>(
                nextFrameDue - pollDeadline);
            pollTimeoutMs = std::min(kPollTimeoutMs, static_cast<int>(untilDue.count()));
        } else {
            pollTimeoutMs = 0;
        }
        const int pollResult = poll(pollfds.data(), pollfds.size(), pollTimeoutMs);
        if (pollResult > 0 && (pollfds[0].revents & (POLLIN | POLLERR | POLLHUP))) {
            const int clientFd = accept(listen_fd_, nullptr, nullptr);
            if (clientFd >= 0) {
                AcceptClient(clientFd);
            }
        }

        // Drain client sockets; drop closed/errored clients.
        std::vector<int> toDrop;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (size_t i = 1; i < pollfds.size(); ++i) {
                const int fd = pollfds[i].fd;
                if (pollfds[i].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) {
                    char buf[kRecvBufSize];
                    const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
                    if (n <= 0) {
                        toDrop.push_back(fd);
                    }
                }
            }
            for (int fd : toDrop) {
                close(fd);
                clients_.erase(std::remove(clients_.begin(), clients_.end(), fd), clients_.end());
            }
        }

        // Throttled frame pull.
        const auto now = std::chrono::steady_clock::now();
        if (now < nextFrameDue) {
            continue;
        }

        capture::OwnedNv12Frame frame;
        if (!provider_(&frame)) {
            break;
        }
        // The provider may hand over a default-constructed frame (timeout
        // without data). Skipping it keeps DownscaleRgb24's divide-by-zero
        // path unreachable; the browser simply holds the last frame.
        if (frame.width == 0 || frame.height == 0) {
            continue;
        }
        const auto frameNow = std::chrono::steady_clock::now();
        frameIntervalUs += std::chrono::duration_cast<std::chrono::microseconds>(frameNow - lastFrameAt).count();
        ++frameIntervalCount;
        lastFrameAt = frameNow;

        // Convert and optionally downscale.
        const uint32_t outW = std::max<uint32_t>(2, static_cast<uint32_t>(frame.width * scale_) & ~1u);
        const uint32_t outH = std::max<uint32_t>(2, static_cast<uint32_t>(frame.height * scale_) & ~1u);
        rgb.resize(static_cast<size_t>(frame.width) * frame.height * 3);
        YuvNv12ToRgb24(frame.data.data(), frame.width, frame.height, rgb.data());
        if (outW != frame.width || outH != frame.height) {
            std::vector<uint8_t> small(static_cast<size_t>(outW) * outH * 3);
            DownscaleRgb24(rgb.data(), frame.width, frame.height, small.data(), outW, outH);
            rgb.swap(small);
        }

        // Encode to JPEG with libjpeg-turbo's memory destination.
        const auto encodeStart = std::chrono::steady_clock::now();
        jpeg.clear();
        jpeg_compress_struct cinfo;
        JpegErrorState jerr;
        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = JpegErrorExit;
        bool encoded = false;
        unsigned char *mem = nullptr;
        unsigned long memLen = 0;
        if (setjmp(jerr.jump) == 0) {
            jpeg_create_compress(&cinfo);
            jpeg_mem_dest(&cinfo, &mem, &memLen);
            cinfo.image_width = outW;
            cinfo.image_height = outH;
            cinfo.input_components = 3;
            cinfo.in_color_space = JCS_RGB;
            jpeg_set_defaults(&cinfo);
            jpeg_set_quality(&cinfo, quality_, TRUE);
            jpeg_start_compress(&cinfo, TRUE);
            uint8_t *row = rgb.data();
            const size_t stride = static_cast<size_t>(outW) * 3;
            while (cinfo.next_scanline < cinfo.image_height) {
                JSAMPROW rows[1] = {row};
                jpeg_write_scanlines(&cinfo, rows, 1);
                row += stride;
            }
            jpeg_finish_compress(&cinfo);
            jpeg_destroy_compress(&cinfo);
            if (mem != nullptr && memLen > 0) {
                jpeg.assign(mem, mem + memLen);
                encoded = true;
            }
            std::free(mem);
        } else {
            jpeg_destroy_compress(&cinfo);
        }
        const auto encodeEnd = std::chrono::steady_clock::now();
        encodeUs += std::chrono::duration_cast<std::chrono::microseconds>(encodeEnd - encodeStart).count();
        ++encodeCount;

        if (!encoded) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.encode_failures;
            continue;
        }

        // Build one multipart frame and broadcast to all clients.
        std::vector<uint8_t> part;
        AppendJpegPart(&part, jpeg.data(), jpeg.size());
        std::vector<int> dead;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (int fd : clients_) {
                ssize_t sent = 0;
                while (sent < static_cast<ssize_t>(part.size())) {
                    const ssize_t n = send(fd, part.data() + sent, part.size() - sent, MSG_NOSIGNAL);
                    if (n <= 0) {
                        dead.push_back(fd);
                        break;
                    }
                    sent += n;
                }
            }
            for (int fd : dead) {
                close(fd);
                clients_.erase(std::remove(clients_.begin(), clients_.end(), fd), clients_.end());
            }
            ++stats_.encoded_frames;
            stats_.active_clients = static_cast<uint32_t>(clients_.size());
            // Publish live averages so GetStats() is meaningful while running.
            if (encodeCount > 0) {
                stats_.avg_encode_ms = static_cast<double>(encodeUs) / encodeCount / 1000.0;
            }
            if (frameIntervalCount > 0) {
                stats_.avg_frame_interval_ms = static_cast<double>(frameIntervalUs) / frameIntervalCount / 1000.0;
            }
        }
        // Advance the cadence deadline; never burst-catch-up after a slow frame.
        nextFrameDue += frameInterval;
        if (nextFrameDue < now) {
            nextFrameDue = now;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int fd : clients_) {
            close(fd);
        }
        clients_.clear();
        // Close the listener too: on a natural provider-false exit Stop()
        // skips its running_ guard, so without this the bound socket would
        // leak and block a same-port restart with EADDRINUSE.
        if (listen_fd_ >= 0) {
            close(listen_fd_);
            listen_fd_ = -1;
        }
        stats_.active_clients = 0;
        running_ = false;
    }
}

} // namespace stream
} // namespace manifold3
