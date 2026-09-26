#pragma once
#include "core_process.h"
#include "core_protocol.h"
#include <chrono>
#include <functional>
#include <memory>
namespace agentvision {
namespace connection_detail {
// Fixed-duration progress policy shared by framing, writes and lifecycle stages.
// Passing observation time explicitly keeps boundary behavior deterministic.
class Deadline {
  public:
    using Clock = std::chrono::steady_clock;
    void arm(Clock::time_point) noexcept;
    void disarm() noexcept;
    bool expired(Clock::time_point) const noexcept;
    Clock::time_point expiresAt() const noexcept;

  private:
    bool active_ = false;
    Clock::time_point since_{};
};
} // namespace connection_detail
enum class InputOrigin { User, EmulatorReply };
enum class EnqueueResult { Queued, Closed, Overflow };
struct TransportChunk {
    enum class Kind { Data, End, Lost };
    Kind kind = Kind::Lost;
    std::vector<char> bytes;
    uint64_t sequence = 0;
};
enum class ContactError {
    None,
    EOFReached,
    Truncated,
    Protocol,
    FrameTimeout,
    WriteTimeout,
    CreditTimeout,
    HandshakeTimeout,
    ShutdownTimeout,
    Cancelled,
    ResourceLimit,
    IO
};
struct ConnectionEvent {
    enum class Kind { Ready, Created, Exited, Closed, RequestError, Lost };
    Kind kind = Kind::Lost;
    RequestId request = 0;
    SessionId session = 0;
    SessionMetadata metadata;
    ContactError contactError = ContactError::None;
    uint16_t errorCode = 0;
    uint32_t partialInputBytes = 0;
};
struct ConnectionCompletion {
    CoreCompletion core;
    ContactError contactError = ContactError::None;
    bool graceful = false;
};
class CoreConnection;
struct StartResult {
    std::shared_ptr<CoreConnection> connection;
    LaunchError error = LaunchError::None;
    int systemError = 0;
};
// One controller reader owns each chunk until consumed() reports released bytes.
// cancelLocal wakes/detaches only this presentation. It sends no Close and grants
// no credit for discarded or still-borrowed bytes. Retained Closed/Lost handles
// cannot issue credit/flush to a future endpoint with the same opaque ID.
class SessionEndpoint {
  public:
    TransportChunk readChunk() noexcept;
    void consumed(size_t) noexcept;
    SessionMetadata metadata() const noexcept;
    void flushed(uint64_t) noexcept;
    void cancelLocal() noexcept;

  private:
    friend class CoreConnection;
    struct State;
    SessionEndpoint(std::shared_ptr<State>, std::weak_ptr<CoreConnection>);
    std::shared_ptr<State> state_;
    std::weak_ptr<CoreConnection> connection_;
};
class CoreConnection {
  public:
    // Callback runs once outside locks after IPC users stop, before escalation.
    // Complete bounded presentation stop/restoration before returning (100ms
    // userspace target); never destroy/join this connection or wait on core I/O.
    // UI-thread restoration needs a bounded acknowledgement, not just a signal.
    // Endpoint handles own payloads
    // and metadata, but only a live connection can submit credit. The transport
    // owns both strong handles, avoiding a connection/endpoint reference cycle.
    static StartResult start(const std::string &absolutePath,
                             std::function<void()> localStopped = {});
    ~CoreConnection();
    // Zero RequestId means admission failed. Queued Input/Resize means bounded
    // local staging, not a PTY write Ack: these private emissions receive wire IDs
    // at dispatch and can be discarded by confirmed Close/Shutdown. Numbered
    // wire correlations remain owned until exact reply or contact loss.
    RequestId createSession(uint16_t rows, uint16_t cols);
    EnqueueResult submitInput(SessionId, const std::vector<uint8_t> &, InputOrigin);
    EnqueueResult submitResize(SessionId, uint16_t, uint16_t);
    RequestId closeSession(SessionId);
    RequestId returnCredit(SessionId, uint32_t);
    void shutdown() noexcept;
    void cancelLocal() noexcept;
    ConnectionCompletion join() noexcept;
    bool pollEvent(ConnectionEvent &) noexcept;
    std::shared_ptr<SessionEndpoint> endpoint(SessionId);

  private:
    friend class SessionEndpoint;
    struct State;
    explicit CoreConnection(std::unique_ptr<CoreProcess>, std::function<void()>);
    void reader() noexcept;
    void writer() noexcept;
    void lifecycle() noexcept;
    void lose(ContactError) noexcept;
    bool dispatch(const Frame &);
    void endpointFlushed(SessionEndpoint &, uint64_t) noexcept;
    RequestId admitCredit(SessionId, uint32_t, const SessionEndpoint *source);
    std::unique_ptr<State> state_;
};
} // namespace agentvision
