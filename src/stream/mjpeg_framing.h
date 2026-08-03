#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace manifold3 {
namespace stream {

constexpr char kMjpegBoundary[] = "frame";

// Appends the multipart/x-mixed-replace HTTP response header for an MJPEG
// stream.
void AppendHttpHeaders(std::vector<uint8_t> *out);

// Appends one JPEG part: boundary line, part headers, payload, trailing CRLF.
void AppendJpegPart(std::vector<uint8_t> *out, const uint8_t *jpeg, size_t jpegLen);

} // namespace stream
} // namespace manifold3
