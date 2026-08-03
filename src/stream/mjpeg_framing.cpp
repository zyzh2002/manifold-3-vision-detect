#include "stream/mjpeg_framing.h"

#include <cstdio>
#include <cstring>

namespace manifold3 {
namespace stream {

void AppendHttpHeaders(std::vector<uint8_t> *out) {
    const char kHeaders[] =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "\r\n";
    out->insert(out->end(), kHeaders, kHeaders + std::strlen(kHeaders));
}

void AppendJpegPart(std::vector<uint8_t> *out, const uint8_t *jpeg, size_t jpegLen) {
    char header[128];
    const int n = std::snprintf(header, sizeof(header),
                                "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
                                jpegLen);
    out->insert(out->end(), header, header + n);
    out->insert(out->end(), jpeg, jpeg + jpegLen);
    out->push_back('\r');
    out->push_back('\n');
}

} // namespace stream
} // namespace manifold3
