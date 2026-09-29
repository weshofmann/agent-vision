#pragma once
#include <chrono>
namespace agentvision {
// Timestamp bookkeeping only. The application's gate still requires actual
// UI stop/restoration acknowledgement before core escalation can proceed.
class RestorationTarget {
public:
    using Clock = std::chrono::steady_clock;
    void begin(Clock::time_point entry) noexcept {
        if (!begun_) { begun_ = true; deadline_ = entry + std::chrono::milliseconds(100); }
    }
    void acknowledge(Clock::time_point proof) noexcept {
        if (!acknowledged_) { acknowledged_ = true; proof_ = proof; }
    }
    Clock::time_point deadline() const noexcept { return deadline_; }
    bool missed(bool waitTimedOut = false) const noexcept { return waitTimedOut || (begun_ && acknowledged_ && proof_ > deadline_); }
private:
    bool begun_ = false, acknowledged_ = false;
    Clock::time_point deadline_ {}, proof_ {};
};
}
