#include "frontend_cleanup.h"
#include <iostream>
#include <stdexcept>
using agentvision::RestorationTarget;
using Time = RestorationTarget::Clock::time_point;
static Time at(int milliseconds) { return Time{} + std::chrono::milliseconds(milliseconds); }
static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        RestorationTarget late;
        late.begin(at(0)); late.acknowledge(at(101));
        check(late.missed(false), "late ack must be recorded even if wait predicate succeeds");
        RestorationTarget synchronous;
        synchronous.begin(at(20)); synchronous.acknowledge(at(121));
        check(synchronous.missed(false), "synchronous UI stop must account for its real elapsed target");
        RestorationTarget preack;
        preack.acknowledge(at(0)); preack.begin(at(200));
        check(!preack.missed(), "prior restoration proof meets a later callback target");
        preack.acknowledge(at(999));
        check(!preack.missed(), "duplicate acknowledgement cannot replace the first proof");
        RestorationTarget boundary;
        boundary.begin(at(10)); boundary.acknowledge(at(110));
        check(!boundary.missed(), "actual proof at target is within target");
        RestorationTarget delayedLock;
        delayedLock.begin(at(0)); delayedLock.begin(at(200)); delayedLock.acknowledge(at(150));
        check(delayedLock.missed(), "mutex scheduling cannot restart the original callback target");
        check(late.missed(true), "observed wait timeout remains a target miss");
        std::cout << "restoration target timestamp cases passed\n";
    } catch (const std::exception &error) {
        std::cerr << "restoration target regression: " << error.what() << '\n';
        return 1;
    }
}
