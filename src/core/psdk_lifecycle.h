#pragma once

namespace manifold3 {

// Owns the DJI Payload SDK lifecycle: handler registration, core init,
// application start, and orderly shutdown. Modules (capture, inference) are
// intentionally not part of this class.
class PsdkLifecycle {
  public:
    static PsdkLifecycle &Get();

    // Registers platform handlers and initializes the PSDK core, including
    // aircraft identity queries and product identity assignment.
    bool Initialize();

    // Transitions the SDK into the running application state.
    bool Start();

    // Deinitializes the SDK core. Safe to call even after failed Start().
    void Shutdown();

    PsdkLifecycle(const PsdkLifecycle &) = delete;
    PsdkLifecycle &operator=(const PsdkLifecycle &) = delete;

  private:
    PsdkLifecycle() = default;
    ~PsdkLifecycle() = default;

    bool initialized_ = false;
    bool started_ = false;
};

} // namespace manifold3
