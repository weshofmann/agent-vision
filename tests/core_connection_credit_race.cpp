#include "core_connection.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace agentvision;
namespace {
using Clock = std::chrono::steady_clock;
struct Gate {
    std::promise<void> entered, release;
    std::shared_future<void> released = release.get_future().share();
    std::atomic<RequestId> admitted{0};
    bool opened = false;
    void open() {
        if (!opened) {
            opened = true;
            release.set_value();
        }
    }
};
std::atomic<SessionEndpoint *> oldEndpoint{nullptr}, currentEndpoint{nullptr};
Gate oldGate, currentGate;
std::atomic<bool> quietHeld{true};
Gate *gate(SessionEndpoint *endpoint) {
    if (endpoint == oldEndpoint.load())
        return &oldGate;
    if (endpoint == currentEndpoint.load())
        return &currentGate;
    return nullptr;
}
void check(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
ConnectionEvent event(const std::shared_ptr<CoreConnection> &connection,
                      ConnectionEvent::Kind kind) {
    auto until = Clock::now() + std::chrono::seconds(4);
    ConnectionEvent result;
    while (Clock::now() < until) {
        if (connection->pollEvent(result)) {
            check(result.kind != ConnectionEvent::Kind::Lost, "unexpected contact loss");
            if (result.kind == kind)
                return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("event deadline");
}
struct Consumption {
    Gate &barrier;
    std::future<void> callback;
    Consumption(Gate &g, const std::shared_ptr<SessionEndpoint> &endpoint, size_t bytes)
        : barrier(g), callback(std::async(std::launch::async,
                                          [endpoint, bytes] { endpoint->consumed(bytes); })) {
        check(barrier.entered.get_future().wait_for(std::chrono::seconds(4)) ==
                  std::future_status::ready,
              "consumption reached pre-connection boundary");
    }
    void resume() {
        barrier.open();
        callback.get();
    }
    ~Consumption() {
        barrier.open();
        if (callback.valid())
            callback.wait();
    }
};
} // namespace
namespace agentvision {
namespace credit_race_test {
void beforeAdmission(SessionEndpoint *endpoint) {
    if (auto *g = gate(endpoint)) {
        g->entered.set_value();
        // Barrier failure cannot leave a blocked worker behind on a failing test.
        if (g->released.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
            std::abort();
    }
}
void afterAdmission(SessionEndpoint *endpoint, RequestId request) {
    if (auto *g = gate(endpoint))
        g->admitted = request;
}
bool holdQuietCredit() noexcept {
    // Called under the writer's locks: this test predicate never blocks.
    return quietHeld.load();
}
} // namespace credit_race_test
} // namespace agentvision
int main() {
    try {
        setenv("AV_CONNECTION_CASE", "stale", 1);
        auto started = CoreConnection::start(FAKE_CORE);
        auto c = started.connection;
        check(bool(c), "connection start");
        event(c, ConnectionEvent::Kind::Ready);
        c->createSession(24, 80);
        auto old = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
        oldEndpoint = old.get();
        auto oldData = old->readChunk();
        check(oldData.bytes == std::vector<char>({'A', '\0', char(0xff)}),
              "independent literal old output");
        Consumption oldConsumption(oldGate, old, oldData.bytes.size());
        check(c->closeSession(old->metadata().id) != 0, "old Close admitted");
        event(c, ConnectionEvent::Kind::Closed);
        auto created = c->createSession(24, 80);
        auto current = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
        check(current && current.get() != old.get() && current->metadata().id == old->metadata().id,
              "fully retired synthetic ID reuse has a new binding");
        currentEndpoint = current.get();
        auto data = current->readChunk();
        check(data.bytes == std::vector<char>({'A', '\0', char(0xff)}),
              "independent literal replacement output");
        Consumption newConsumption(currentGate, current, data.bytes.size());
        oldConsumption.resume();
        check(oldGate.admitted.load() == 0,
              "retired callback must not allocate Credit for the replacement binding");
        check(old->metadata().state == SessionState::Closed &&
                  old->readChunk().kind == TransportChunk::Kind::Lost,
              "retired endpoint stays Closed and cancelled");
        newConsumption.resume();
        check(currentGate.admitted.load() == created + 1,
              "replacement owns unchanged consumption and allocates exactly its own Credit");
        auto end = current->readChunk();
        check(end.kind == TransportChunk::Kind::End, "replacement final output consumed");
        current->flushed(end.sequence);
        event(c, ConnectionEvent::Kind::Exited);
        c->shutdown();
        check(c->join().graceful, "race regression cleanup retains exact Credit correlation");
        std::cout << "identity-bound consumption race passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
