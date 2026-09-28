#include "core_connection.h"
#include "window.h"
#define Uses_TText
#include <tvision/ttext.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace agentvision;
using Clock = std::chrono::steady_clock;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void reportCompletion(const char *label, const ConnectionCompletion &completion) {
    std::cerr << "cleanup=" << label << " graceful=" << completion.graceful
              << " contactError=" << static_cast<int>(completion.contactError)
              << " childKind=" << static_cast<int>(completion.core.kind)
              << " childValue=" << completion.core.value
              << " systemError=" << completion.core.systemError
              << " termSent=" << completion.core.termSent
              << " killSent=" << completion.core.killSent << '\n';
}

class ConnectionCleanup {
  public:
    explicit ConnectionCleanup(std::shared_ptr<CoreConnection> connection)
        : connection_(std::move(connection)) {}
    ~ConnectionCleanup() {
        if (!joined_) {
            connection_->cancelLocal();
            reportCompletion("fallback-cancel", connection_->join());
        }
    }
    ConnectionCompletion shutdownAndJoin() noexcept {
        connection_->shutdown();
        auto completion = connection_->join();
        joined_ = true;
        reportCompletion("shutdown-join", completion);
        return completion;
    }

  private:
    std::shared_ptr<CoreConnection> connection_;
    bool joined_ = false;
};

struct BoundedChunk {
    TransportChunk chunk;
    bool ready = false;
    bool collected = false;
};

BoundedChunk readChunkBounded(const char *label,
                              const std::shared_ptr<SessionEndpoint> &endpoint) {
    auto read = std::async(std::launch::async, [endpoint] { return endpoint->readChunk(); });
    const bool ready = read.wait_for(std::chrono::milliseconds(500)) ==
                       std::future_status::ready;
    if (!ready)
        endpoint->cancelLocal();
    auto chunk = read.get();
    std::cerr << "reader=" << label << " ready=" << ready << " collected=1 kind="
              << static_cast<int>(chunk.kind) << " sequence=" << chunk.sequence
              << " bytes=" << chunk.bytes.size() << '\n';
    return {std::move(chunk), ready, true};
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

int captions() {
    SessionMetadata m;
    m.id = 42;
    m.state = SessionState::Running;
    auto live = TerminalWindow::captionFor('A', m);
    check(live.find("[live]") != std::string::npos && live.find("pid") == std::string::npos,
          "live caption exposes typed state rather than a shell PID");
    check(TerminalWindow::captionFor('A', m, true).find("[closing]") != std::string::npos,
          "confirmed close is visible while authoritative state remains live");
    m.state = SessionState::Exited;
    m.status = {ExitKind::Signal, 15, true};
    m.drainReason = DrainReason::ByteCap;
    auto signal = TerminalWindow::captionFor('A', m);
    check(signal.find("signal 15") != std::string::npos &&
          signal.find("core dump") != std::string::npos && signal.find("tail: byte cap") != std::string::npos,
          "typed signal, core flag, and bounded unread-tail reason reach the UI");
    m.status = {ExitKind::Unavailable, 0, false};
    m.drainReason = DrainReason::IOError;
    auto unavailable = TerminalWindow::captionFor('A', m);
    check(unavailable.find("status unavailable") != std::string::npos &&
          unavailable.find("exited 0") == std::string::npos,
          "unavailable status never becomes successful exit");
    m.state = SessionState::Lost;
    check(TerminalWindow::captionFor('B', m).find("backend lost") != std::string::npos,
          "lost authority is distinguished from shell exit");
    m.state = SessionState::Closed;
    check(TerminalWindow::captionFor('B', m).find("[closed]") != std::string::npos,
          "closed remains a typed lifecycle state");
    check(TerminalWindow::captionFor("C", m).find("Terminal C [closed]") != std::string::npos,
          "dynamic terminal label remains independent of session identity");
    m.state = SessionState::Exited;
    m.status = {ExitKind::Exit, 7, false};
    m.drainReason = DrainReason::EOFReached;
    check(TerminalWindow::captionFor('B', m, false, true).find("[exited 7] [resize failed]") != std::string::npos,
          "exact exit and failed resize operation are independently visible");
    check(TerminalWindow::validDisplayTitle("Work \xE6\x97\xA5"), "valid Unicode title is accepted");
    check(!TerminalWindow::validDisplayTitle(std::string(49, 'a')) &&
          !TerminalWindow::validDisplayTitle("bad\nname") &&
          !TerminalWindow::validDisplayTitle("bad\x7f") &&
          !TerminalWindow::validDisplayTitle("bad\xC2\x80") &&
          !TerminalWindow::validDisplayTitle("bad\xC3"),
          "overlong, controls, DEL, C1 and malformed UTF-8 are rejected");
    check(TerminalWindow::captionFor("Work", m, false, false, true).find("[exited 7] [backend lost]") != std::string::npos,
          "known exit remains visible with later contact loss");
    m.state = SessionState::Running;
    check(TerminalWindow::captionFor("Work", m, true, false, true).find("[backend lost]") != std::string::npos &&
          TerminalWindow::captionFor("Work", m, true, false, true).find("closing") == std::string::npos,
          "loss during close never claims a completed process state");
    m.state = SessionState::Exited;
    m.status = {ExitKind::Exit, 7, false};
    const auto compact = TerminalWindow::formatCaption("Work", m, false, false, true, 14);
    check(TText::width(compact.c_str()) <= 14 && compact.find("7") != std::string::npos &&
          compact.find("lost") != std::string::npos,
          "narrow frame keeps both known exit and later loss visible");
    m.state = SessionState::Running;
    const auto narrowDefault = TerminalWindow::captionFor('B', m, false, false, false, 14);
    check(TText::width(narrowDefault.c_str()) <= 14 &&
          narrowDefault.find('B') != std::string::npos &&
          narrowDefault.find("[live]") != std::string::npos,
          "narrow default frame retains immutable view label and truthful state");
    auto clipped = TerminalWindow::captionFor("\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C", m, false, false, false, 20);
    check(clipped.find("[live]") != std::string::npos && TText::width(clipped.c_str()) <= 20,
          "narrow frame reserves state and clips wide Unicode at a whole character");
    return 0;
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

int closePendingLive() {
    auto connection = start("metadata-live-close");
    ConnectionCleanup cleanup(connection);
    auto endpoint = create(connection);
    const auto id = endpoint->metadata().id;

    // The peer acknowledges and echoes this input only while the endpoint is
    // live. This establishes an active input path before Close is admitted.
    check(endpoint->enqueueInput({'p'}, InputOrigin::User) == EnqueueResult::Queued,
          "live session admits positive user input before Close");
    auto acceptedRead = readChunkBounded("pre-close-ack", endpoint);
    if (!acceptedRead.ready) {
        cleanup.shutdownAndJoin();
        throw std::runtime_error("positive input acknowledgement output deadline");
    }
    auto accepted = std::move(acceptedRead.chunk);
    check(accepted.kind == TransportChunk::Kind::Data &&
              accepted.bytes == std::vector<char>({'a', 'c', 'k'}) &&
              accepted.sequence == 1,
          "fake peer acknowledges the live input with output");
    endpoint->consumed(accepted.bytes.size());

    const auto close = endpoint->requestClose();
    const auto whileClosing = endpoint->metadata();
    const auto userInput = endpoint->enqueueInput({'u'}, InputOrigin::User);
    const auto emulatorInput = endpoint->enqueueInput({'e'}, InputOrigin::EmulatorReply);
    const auto duplicateClose = endpoint->requestClose();
    const bool closePendingStayedLive = close != 0 &&
                                        whileClosing.id == id &&
                                        whileClosing.state == SessionState::Running;

    // A second Create is the fake peer's barrier: it proves unrelated transport
    // progress, then releases the final output and typed status for session A.
    check(connection->createSession(24, 80) != 0,
          "unrelated Create progresses while live Close is pending");
    const auto unrelated = nextEvent(connection, ConnectionEvent::Kind::Created);
    check(unrelated.session != id, "unrelated session is distinct from closing session");
    auto tailRead = readChunkBounded("final-tail", endpoint);
    if (!tailRead.ready) {
        cleanup.shutdownAndJoin();
        throw std::runtime_error("pending Close final-tail deadline");
    }
    auto tail = std::move(tailRead.chunk);
    check(tail.kind == TransportChunk::Kind::Data &&
              tail.bytes == std::vector<char>({'t', 'a', 'i', 'l'}) && tail.sequence == 2,
          "pending Close preserves final output through sequence two");
    endpoint->consumed(tail.bytes.size());
    auto endRead = readChunkBounded("final-seal", endpoint);
    if (!endRead.ready) {
        cleanup.shutdownAndJoin();
        throw std::runtime_error("pending Close final-seal deadline");
    }
    const auto end = std::move(endRead.chunk);
    check(end.kind == TransportChunk::Kind::End && end.sequence == 2,
          "pending Close preserves the final output seal");
    endpoint->flushed(end.sequence);
    const auto exited = nextEvent(connection, ConnectionEvent::Kind::Exited);
    check(exited.metadata.state == SessionState::Exited &&
              exited.metadata.lastSequence == 2 &&
              exited.metadata.status.kind == ExitKind::Exit &&
              exited.metadata.status.value == 7 &&
              exited.metadata.drainReason == DrainReason::ByteCap,
          "exact exit is published after the pending-close flush barrier");

    // A third Create releases the held correlated Closed only after the test
    // has observed the exact Exited snapshot above.
    check(connection->createSession(24, 80) != 0,
          "second unrelated Create releases the held Closed response");
    const auto secondUnrelated = nextEvent(connection, ConnectionEvent::Kind::Created);
    check(secondUnrelated.session != id, "second unrelated session is distinct");
    const auto closed = nextEvent(connection, ConnectionEvent::Kind::Closed);
    const auto retained = endpoint->metadata();
    const bool closeWasCorrelated = closed.request == close && closed.session == id &&
                                    closed.metadata.state == SessionState::Closed &&
                                    closed.metadata.lastSequence == 2 &&
                                    closed.metadata.status.kind == ExitKind::Exit &&
                                    closed.metadata.status.value == 7 &&
                                    retained.state == SessionState::Closed &&
                                    retained.lastSequence == 2 &&
                                    retained.status.kind == ExitKind::Exit &&
                                    retained.status.value == 7;

    const auto completion = cleanup.shutdownAndJoin();
    check(completion.graceful, "pending-close peer shuts down after owned cleanup");
    check(closePendingStayedLive, "live endpoint retains identity/state while Close is pending");
    check(userInput == EnqueueResult::Closed && emulatorInput == EnqueueResult::Closed,
          "pending live Close suppresses user and emulator input");
    check(duplicateClose == 0, "pending live Close rejects duplicate Close admission");
    check(closeWasCorrelated, "final Closed retains exact status and close correlation");
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
        if (mode == "ui-captions") return captions();
        if (mode == "lifecycle")
            return lifecycle();
        if (mode == "close-pending-live")
            return closePendingLive();
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
