#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>

#include "capture/latest_frame_slot.h"

using manifold3::capture::FramePushResult;
using manifold3::capture::IsValidNv12Frame;
using manifold3::capture::LatestFrameSlot;
using manifold3::capture::OwnedNv12Frame;

namespace {

// 2x2 NV12 frame: Y=0..3, UV half-size (1x1) = 128, 129 (6 bytes).
const uint8_t kNv12Frame[6] = {0, 1, 2, 3, 128, 129};

void TestValidPushAndTake() {
    LatestFrameSlot slot;
    const FramePushResult result = slot.Push(kNv12Frame, 6, 2, 2, 7);
    assert(result == FramePushResult::kStored);

    OwnedNv12Frame frame;
    const bool ok = slot.WaitTake(&frame, std::chrono::milliseconds(100));
    assert(ok);
    assert(frame.data.size() == 6);
    assert(frame.width == 2);
    assert(frame.height == 2);
    assert(frame.frame_id == 7);
    assert(frame.data[0] == 0 && frame.data[5] == 129);
}

void TestValidation() {
    assert(IsValidNv12Frame(kNv12Frame, 6, 2, 2));
    assert(!IsValidNv12Frame(nullptr, 6, 2, 2));
    assert(!IsValidNv12Frame(kNv12Frame, 6, 0, 2));
    assert(!IsValidNv12Frame(kNv12Frame, 6, 2, 0));
    assert(!IsValidNv12Frame(kNv12Frame, 6, 3, 2)); // odd width
    assert(!IsValidNv12Frame(kNv12Frame, 6, 2, 3)); // odd height
    assert(!IsValidNv12Frame(kNv12Frame, 5, 2, 2)); // len == expected - 1
    assert(!IsValidNv12Frame(kNv12Frame, 7, 2, 2)); // len == expected + 1

    LatestFrameSlot slot;
    assert(slot.Push(nullptr, 6, 2, 2, 1) == FramePushResult::kInvalid);
    assert(slot.Push(kNv12Frame, 6, 0, 2, 1) == FramePushResult::kInvalid);
    assert(slot.Push(kNv12Frame, 6, 2, 0, 1) == FramePushResult::kInvalid);
    assert(slot.Push(kNv12Frame, 6, 3, 2, 1) == FramePushResult::kInvalid);
    assert(slot.Push(kNv12Frame, 6, 2, 3, 1) == FramePushResult::kInvalid);
    assert(slot.Push(kNv12Frame, 5, 2, 2, 1) == FramePushResult::kInvalid);
    assert(slot.Push(kNv12Frame, 7, 2, 2, 1) == FramePushResult::kInvalid);
    assert(slot.invalid_frames() == 7);
    assert(slot.replaced_frames() == 0);
}

void TestReplaceSemantics() {
    LatestFrameSlot slot;
    assert(slot.Push(kNv12Frame, 6, 2, 2, 1) == FramePushResult::kStored);
    assert(slot.Push(kNv12Frame, 6, 2, 2, 2) == FramePushResult::kReplaced);
    assert(slot.replaced_frames() == 1);

    OwnedNv12Frame frame;
    assert(slot.WaitTake(&frame, std::chrono::milliseconds(100)));
    assert(frame.frame_id == 2);

    // Slot is now empty; next take times out and no further replacement occurs.
    assert(!slot.WaitTake(&frame, std::chrono::milliseconds(50)));
    assert(slot.replaced_frames() == 1);
}

void TestTimeout() {
    LatestFrameSlot slot;
    OwnedNv12Frame frame;
    const auto start = std::chrono::steady_clock::now();
    const bool ok = slot.WaitTake(&frame, std::chrono::milliseconds(50));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(!ok);
    assert(elapsed >= std::chrono::milliseconds(50));
}

void TestWakeOnPush() {
    LatestFrameSlot slot;
    OwnedNv12Frame frame;
    const auto start = std::chrono::steady_clock::now();
    assert(slot.Push(kNv12Frame, 6, 2, 2, 1) == FramePushResult::kStored);
    const bool ok = slot.WaitTake(&frame, std::chrono::milliseconds(500));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(ok);
    assert(elapsed < std::chrono::milliseconds(500));
}

void TestWakeOnStop() {
    LatestFrameSlot slot;
    slot.Stop();
    OwnedNv12Frame frame;
    const auto start = std::chrono::steady_clock::now();
    const bool ok = slot.WaitTake(&frame, std::chrono::milliseconds(500));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(!ok);
    assert(elapsed < std::chrono::milliseconds(100));
}

void TestManyOverwritesKeepOneFrame() {
    LatestFrameSlot slot;
    for (uint32_t i = 0; i < 1000; ++i) {
        const FramePushResult result = slot.Push(kNv12Frame, 6, 2, 2, i);
        assert(result != FramePushResult::kInvalid);
    }
    assert(slot.replaced_frames() == 999);
    assert(slot.invalid_frames() == 0);

    OwnedNv12Frame frame;
    assert(slot.WaitTake(&frame, std::chrono::milliseconds(100)));
    assert(frame.data.size() == 6);
    assert(frame.frame_id == 999);

    // Exactly one frame was held; the slot is empty now.
    assert(!slot.WaitTake(&frame, std::chrono::milliseconds(50)));
}

void TestInvalidCounterIsolation() {
    LatestFrameSlot slot;
    assert(slot.Push(kNv12Frame, 6, 2, 2, 1) == FramePushResult::kStored);
    assert(slot.Push(nullptr, 6, 2, 2, 2) == FramePushResult::kInvalid);
    assert(slot.Push(kNv12Frame, 5, 2, 2, 3) == FramePushResult::kInvalid);
    assert(slot.invalid_frames() == 2);
    assert(slot.replaced_frames() == 0);
}

void TestMultiThreadSmoke() {
    LatestFrameSlot slot;
    std::thread producer([&slot]() {
        for (uint32_t i = 0; i < 100; ++i) {
            slot.Push(kNv12Frame, 6, 2, 2, i);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        slot.Stop();
    });

    uint64_t taken = 0;
    OwnedNv12Frame frame;
    while (slot.WaitTake(&frame, std::chrono::milliseconds(100))) {
        ++taken;
    }
    producer.join();
    assert(taken >= 1);
}

} // namespace

int main() {
    TestValidPushAndTake();
    TestValidation();
    TestReplaceSemantics();
    TestTimeout();
    TestWakeOnPush();
    TestWakeOnStop();
    TestManyOverwritesKeepOneFrame();
    TestInvalidCounterIsolation();
    TestMultiThreadSmoke();
    return 0;
}
