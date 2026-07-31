#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

#include "core/psdk_lifecycle.h"

namespace {
volatile std::sig_atomic_t g_stopRequested = 0;

void OnStopSignal(int signalNum) {
    (void)signalNum;
    g_stopRequested = 1;
}
} // namespace

int main() {
    std::signal(SIGINT, OnStopSignal);
    std::signal(SIGTERM, OnStopSignal);

    auto &lifecycle = manifold3::PsdkLifecycle::Get();

    if (!lifecycle.Initialize()) {
        std::fprintf(stderr, "PSDK initialization failed\n");
        return 1;
    }
    std::printf("PSDK initialized\n");

    if (!lifecycle.Start()) {
        std::fprintf(stderr, "PSDK application start failed\n");
        lifecycle.Shutdown();
        return 1;
    }
    std::printf("PSDK application started\n");

    while (!g_stopRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::printf("Shutting down\n");
    lifecycle.Shutdown();
    std::printf("PSDK deinitialized\n");
    return 0;
}
