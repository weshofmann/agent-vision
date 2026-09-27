#include "core_connection.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace agentvision;
using Clock = std::chrono::steady_clock;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

ConnectionEvent nextEvent(const std::shared_ptr<CoreConnection> &connection,
                          ConnectionEvent::Kind expected, bool forbidExited = false) {
    const auto deadline = Clock::now() + std::chrono::seconds(4);
    ConnectionEvent event;
    while (Clock::now() < deadline) {
        if (!connection->pollEvent(event)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (forbidExited)
            check(event.kind != ConnectionEvent::Kind::Exited,
                  "contact loss cannot publish a fabricated exit");
        if (event.kind == expected)
            return event;
        check(event.kind != ConnectionEvent::Kind::Lost ||
                  expected == ConnectionEvent::Kind::Lost,
              "unexpected core contact loss");
    }
    throw std::runtime_error("expected metadata event deadline");
}

std::shared_ptr<CoreConnection> start(const char *mode) {
    check(setenv("AV_CONNECTION_CASE", mode, 1) == 0, "select synthetic peer case");
    auto result = CoreConnection::start(FAKE_CORE);
    check(bool(result.connection), "launch synthetic core peer");
    nextEvent(result.connection, ConnectionEvent::Kind::Ready);
    return result.connection;
}

std::shared_ptr<SessionEndpoint> create(const std::shared_ptr<CoreConnection> &connection) {
    check(connection->createSession(24, 80) != 0, "Create request accepted");
    const auto created = nextEvent(connection, ConnectionEvent::Kind::Created);
    check(created.request != 0 && created.session != 0 &&
              created.metadata.id == created.session &&
              created.metadata.state == SessionState::Running,
          "Created event carries running authoritative identity");
    auto endpoint = connection->endpoint(created.session);
    check(bool(endpoint) && endpoint->metadata().state == SessionState::Running,
          "Created registers a running endpoint");
    return endpoint;
}

void checkSignalStatus(const SessionMetadata &metadata, SessionState state) {
    check(metadata.state == state, "metadata state matches authoritative transition");
    check(metadata.status.kind == ExitKind::Signal && metadata.status.value == 15 &&
              metadata.status.coreDump,
          "signal kind, value, and core-dump flag remain distinct");
    check(metadata.drainReason == DrainReason::ByteCap && metadata.lastSequence == 1,
          "drain reason and final output sequence remain attached to status");
}

int lifecycle() {
    auto connection = start("metadata-lifecycle");
    auto endpoint = create(connection);
    auto data = endpoint->readChunk();
    check(data.kind == TransportChunk::Kind::Data &&
              data.bytes == std::vector<char>({'t', 'a', 'i', 'l'}) && data.sequence == 1,
          "synthetic tail precedes final status");
    endpoint->consumed(data.bytes.size());
    auto end = endpoint->readChunk();
    check(end.kind == TransportChunk::Kind::End && end.sequence == 1,
          "End reports the peer's final sequence");
    check(endpoint->metadata().state == SessionState::Running,
          "Exited stays hidden until presentation flush");
    endpoint->flushed(0);
    check(endpoint->metadata().state == SessionState::Running,
          "wrong flush sequence cannot publish exit metadata");
    endpoint->flushed(end.sequence);
    const auto exited = nextEvent(connection, ConnectionEvent::Kind::Exited);
    checkSignalStatus(endpoint->metadata(), SessionState::Exited);
    checkSignalStatus(exited.metadata, SessionState::Exited);

    const auto close = endpoint->requestClose();
    check(close != 0, "confirmed Close receives a correlation");
    check(endpoint->metadata().state == SessionState::Exited,
          "request admission does not destroy retained presentation");
    check(endpoint->enqueueInput({'x'}, InputOrigin::User) == EnqueueResult::Closed,
          "confirmed Close suppresses further session input");
    check(connection->createSession(24, 80) != 0,
          "unrelated request remains usable while Close awaits its correlated response");
    const auto unrelated = nextEvent(connection, ConnectionEvent::Kind::Created);
    check(unrelated.session != endpoint->metadata().id,
          "unrelated session is created before the held Closed response");
    const auto closed = nextEvent(connection, ConnectionEvent::Kind::Closed);
    check(closed.request == close && closed.session == endpoint->metadata().id,
          "destruction boundary is correlated Closed");
    checkSignalStatus(endpoint->metadata(), SessionState::Closed);
    checkSignalStatus(closed.metadata, SessionState::Closed);
    check(endpoint->requestClose() == 0 &&
              endpoint->readChunk().kind == TransportChunk::Kind::Lost,
          "retired Closed endpoint has no new close or read authority");
    connection->shutdown();
    check(connection->join().graceful, "synthetic lifecycle peer shuts down cleanly");
    return 0;
}

int unavailable() {
    auto connection = start("metadata-unavailable");
    auto endpoint = create(connection);
    auto end = endpoint->readChunk();
    check(end.kind == TransportChunk::Kind::End && end.sequence == 0,
          "unavailable status carries an empty output seal");
    endpoint->flushed(end.sequence);
    const auto exited = nextEvent(connection, ConnectionEvent::Kind::Exited);
    const auto status = endpoint->metadata();
    check(status.state == SessionState::Exited &&
              status.status.kind == ExitKind::Unavailable && status.status.value == 0 &&
              !status.status.coreDump && status.lastSequence == 0 &&
              status.drainReason == DrainReason::IOError,
          "unavailable exit remains typed and is never a fabricated success");
    check(exited.metadata.status.kind == ExitKind::Unavailable &&
              exited.metadata.status.value == 0,
          "Exited notification carries unavailable metadata");
    const auto close = endpoint->requestClose();
    check(close != 0, "unavailable exited session remains explicitly closable");
    const auto closed = nextEvent(connection, ConnectionEvent::Kind::Closed);
    check(closed.request == close && closed.metadata.state == SessionState::Closed &&
              closed.metadata.status.kind == ExitKind::Unavailable &&
              closed.metadata.status.value == 0,
          "Closed retains unavailable status rather than inventing an exit code");
    connection->shutdown();
    check(connection->join().graceful, "unavailable peer shuts down cleanly");
    return 0;
}

int lossBeforeStatus() {
    auto connection = start("metadata-loss");
    auto endpoint = create(connection);
    auto data = endpoint->readChunk();
    check(data.kind == TransportChunk::Kind::Data &&
              data.bytes == std::vector<char>({'t', 'a', 'i', 'l'}),
          "already published output is available before contact loss");
    endpoint->consumed(data.bytes.size());
    check(endpoint->enqueueInput(std::vector<uint8_t>{'c', 'o', 'n', 't', 'a', 'c',
                                                       't', '-', 'l', 'o', 's', 's'},
                                 InputOrigin::User) == EnqueueResult::Queued,
          "synthetic peer receives a deterministic EOF barrier");
    const auto lost = nextEvent(connection, ConnectionEvent::Kind::Lost, true);
    check(lost.contactError == ContactError::EOFReached,
          "EOF is reported as loss of core contact");
    const auto metadata = endpoint->metadata();
    check(metadata.state == SessionState::Lost &&
              metadata.status.kind == ExitKind::Unavailable && metadata.status.value == 0 &&
              !metadata.status.coreDump,
          "EOF before status never fabricates a shell exit");
    check(endpoint->readChunk().kind == TransportChunk::Kind::Lost,
          "contact loss wakes the endpoint without an exit marker");
    const auto result = connection->join();
    check(!result.graceful && result.contactError == ContactError::EOFReached,
          "EOF cannot be reported as graceful backend cleanup");
    return 0;
}

int lossAfterStatus() {
    auto connection = start("metadata-exit-loss");
    auto endpoint = create(connection);
    auto data = endpoint->readChunk();
    check(data.kind == TransportChunk::Kind::Data &&
              data.bytes == std::vector<char>({'t', 'a', 'i', 'l'}),
          "output arrives before authoritative status");
    endpoint->consumed(data.bytes.size());
    auto end = endpoint->readChunk();
    check(end.kind == TransportChunk::Kind::End && end.sequence == 1,
          "final sequence reaches the presentation");
    endpoint->flushed(end.sequence);
    nextEvent(connection, ConnectionEvent::Kind::Exited);
    const auto before = endpoint->metadata();
    checkSignalStatus(before, SessionState::Exited);
    connection->shutdown();
    const auto lost = nextEvent(connection, ConnectionEvent::Kind::Lost);
    check(lost.contactError == ContactError::EOFReached,
          "post-exit EOF still reports connection loss");
    checkSignalStatus(endpoint->metadata(), SessionState::Exited);
    const auto result = connection->join();
    check(!result.graceful && result.contactError == ContactError::EOFReached,
          "post-exit contact loss does not turn shutdown into success");
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected one synthetic metadata case");
        const std::string mode(argv[1]);
        if (mode == "lifecycle")
            return lifecycle();
        if (mode == "unavailable")
            return unavailable();
        if (mode == "loss-before-status")
            return lossBeforeStatus();
        if (mode == "loss-after-status")
            return lossAfterStatus();
        throw std::runtime_error("unknown synthetic metadata case");
    } catch (const std::exception &error) {
        std::cerr << "session metadata regression: " << error.what() << '\n';
        return 1;
    }
}
