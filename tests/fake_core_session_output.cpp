#include "core_connection.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace agentvision;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void reportCompletion(const ConnectionCompletion &completion,
                      bool aReady, bool aCollected, bool bReady, bool bCollected) {
    std::cerr << "cleanup graceful=" << completion.graceful
              << " contactError=" << static_cast<int>(completion.contactError)
              << " childKind=" << static_cast<int>(completion.core.kind)
              << " childValue=" << completion.core.value
              << " systemError=" << completion.core.systemError
              << " termSent=" << completion.core.termSent
              << " killSent=" << completion.core.killSent
              << " readerAReady=" << aReady << " readerACollected=" << aCollected
              << " readerBReady=" << bReady << " readerBCollected=" << bCollected << '\n';
}

ConnectionEvent nextEvent(const std::shared_ptr<CoreConnection> &connection,
                          ConnectionEvent::Kind expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    ConnectionEvent event;
    while (std::chrono::steady_clock::now() < deadline) {
        if (connection->pollEvent(event)) {
            if (event.kind == expected)
                return event;
            check(event.kind != ConnectionEvent::Kind::Lost,
                  "synthetic streams peer retains contact");
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    throw std::runtime_error("synthetic streams event deadline");
}

std::shared_ptr<SessionEndpoint> createSession(
    const std::shared_ptr<CoreConnection> &connection) {
    check(connection->createSession(24, 80) != 0, "Create admitted");
    const auto created = nextEvent(connection, ConnectionEvent::Kind::Created);
    auto endpoint = connection->endpoint(created.session);
    check(bool(endpoint), "Created installs endpoint");
    return endpoint;
}
} // namespace

int main() {
    try {
        check(setenv("AV_CONNECTION_CASE", "streams", 1) == 0,
              "select existing two-session peer mode");
        auto started = CoreConnection::start(FAKE_CORE);
        check(bool(started.connection), "start synthetic peer");
        auto connection = started.connection;
        nextEvent(connection, ConnectionEvent::Kind::Ready);
        auto a = createSession(connection);
        auto b = createSession(connection);

        auto aRead = std::async(std::launch::async, [&] { return a->readChunk(); });
        auto bRead = std::async(std::launch::async, [&] { return b->readChunk(); });
        constexpr auto timeout = std::chrono::milliseconds(500);
        const bool aReady = aRead.wait_for(timeout) == std::future_status::ready;
        const bool bReady = bRead.wait_for(timeout) == std::future_status::ready;

        // A missing output must wake its waiter locally. Then Shutdown and join
        // the synthetic peer before asserting, so the RED run leaves no child.
        if (!aReady)
            a->cancelLocal();
        if (!bReady)
            b->cancelLocal();
        connection->shutdown();
        const auto completion = connection->join();
        const auto aChunk = aRead.get();
        const auto bChunk = bRead.get();
        reportCompletion(completion, aReady, true, bReady, true);

        check(completion.graceful, "peer cleanup completes before output assertions");
        check(aReady && bReady, "both sessions publish their bounded initial output");
        check(aChunk.kind == TransportChunk::Kind::Data &&
                  aChunk.bytes == std::vector<char>({'A', 0, char(255)}) &&
                  aChunk.sequence == 1,
              "session A keeps its exact initial bytes");
        check(bChunk.kind == TransportChunk::Kind::Data &&
                  bChunk.bytes == std::vector<char>({'B', 'x', 'y'}) &&
                  bChunk.sequence == 1,
              "session B keeps its exact initial bytes");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "synthetic session output regression: " << error.what() << '\n';
        return 1;
    }
}
