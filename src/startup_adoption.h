#pragma once
namespace agentvision {
// Access is serialized by the application's cleanup mutex. Once local cleanup
// starts, a queued success cannot authorize another presentation or readiness.
class StartupAdoption {
    bool cleanup_ = false;
public:
    void cleanupBegun() noexcept { cleanup_ = true; }
    bool mayAdopt() const noexcept { return !cleanup_; }
};
}
