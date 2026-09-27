#include "core_connection.h"
#include "startup_adoption.h"
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
using namespace agentvision;
using Clock = std::chrono::steady_clock;
static void check(bool value, const char *reason) { if (!value) throw std::runtime_error(reason); }
static ConnectionEvent next(const std::shared_ptr<CoreConnection> &c, ConnectionEvent::Kind kind) {
    auto until = Clock::now() + std::chrono::seconds(4);
    ConnectionEvent event;
    while (Clock::now() < until) {
        if (c->pollEvent(event)) { if (event.kind == kind) return event; }
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("controlled startup event deadline");
}
int main() {
    std::shared_ptr<CoreConnection> connection;
    StartupAdoption adoption;
    std::mutex mutex;
    std::condition_variable changed;
    bool cleanup = false;
    try {
        check(adoption.mayAdopt(), "healthy startup must admit Created");
        check(setenv("AV_DESKTOP_CASE", "second-created-loss", 1) == 0, "fixture mode");
        check(setenv("AV_DESKTOP_AUDIT", STARTUP_AUDIT, 1) == 0, "fixture audit");
        auto started = CoreConnection::start(DESKTOP_CORE, [&] {
            std::lock_guard<std::mutex> lock(mutex);
            adoption.cleanupBegun();
            cleanup = true;
            changed.notify_all();
        });
        connection = std::move(started.connection);
        check(bool(connection), "controlled synthetic core starts");
        next(connection, ConnectionEvent::Kind::Ready);
        check(connection->createSession(24, 80) != 0, "first Create admitted");
        next(connection, ConnectionEvent::Kind::Created);
        auto request = connection->createSession(24, 80);
        check(request != 0, "second Create admitted");
        {
            std::unique_lock<std::mutex> lock(mutex);
            check(changed.wait_for(lock, std::chrono::seconds(4), [&] { return cleanup; }),
                  "consumer holds adoption until actual contact cleanup callback");
        }
        // No event consumer ran while the second Created and then Lost queued.
        // This deterministically chooses the reviewed failing adoption ordering.
        auto created = next(connection, ConnectionEvent::Kind::Created);
        check(created.request == request && created.session == 2,
              "final Created success remains queued after actual contact loss");
        auto lost = next(connection, ConnectionEvent::Kind::Lost);
        check(lost.contactError == ContactError::EOFReached, "actual EOF loss, not an inferred admission failure");
        auto result = connection->join();
        check(!result.graceful && (result.core.kind == CoreCompletion::Kind::Exited ||
                                   result.core.kind == CoreCompletion::Kind::Signaled),
              "controlled lost core is actually reaped without graceful-success claim");
        check(!adoption.mayAdopt(), "cleanup must reject queued Created and final ready adoption");
        adoption.cleanupBegun();
        check(!adoption.mayAdopt(), "cleanup adoption barrier is irreversible");
        std::cout << "controlled queued Created after actual cleanup rejected\n";
    } catch (const std::exception &error) {
        if (connection) { connection->cancelLocal(); connection->join(); }
        std::cerr << "startup adoption regression: " << error.what() << '\n';
        return 1;
    }
}
