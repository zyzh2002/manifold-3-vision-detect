#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "stream/mjpeg_framing.h"

using manifold3::stream::AppendHttpHeaders;
using manifold3::stream::AppendJpegPart;

namespace {

void TestHttpHeaders() {
    std::vector<uint8_t> out;
    AppendHttpHeaders(&out);
    const std::string s(out.begin(), out.end());
    const std::string kExpected =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "\r\n";
    assert(s == kExpected);
}

void TestJpegPart() {
    const uint8_t payload[3] = {0xFF, 0xD8, 0xFF};
    std::vector<uint8_t> out;
    AppendJpegPart(&out, payload, 3);
    const std::string s(out.begin(), out.end());
    const std::string kExpected =
        "--frame\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: 3\r\n"
        "\r\n"
        "\xFF\xD8\xFF\r\n";
    assert(s == kExpected);
}

} // namespace

int main() {
    TestHttpHeaders();
    TestJpegPart();
    return 0;
}
