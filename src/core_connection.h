#pragma once
#include "core_protocol.h"
#include "core_process.h"
#include <functional>
#include <memory>
namespace agentvision {
enum class InputOrigin { User, EmulatorReply };
enum class EnqueueResult { Queued, Closed, Overflow };
struct TransportChunk {
    enum class Kind { Data, End, Lost };
    Kind kind=Kind::Lost;
    std::vector<char> bytes;
    uint64_t sequence=0;
};
enum class ContactError { None, EOFReached, Truncated, Protocol, FrameTimeout, WriteTimeout, CreditTimeout, HandshakeTimeout, ShutdownTimeout, Cancelled, ResourceLimit, IO };
struct ConnectionEvent {
    enum class Kind { Ready, Created, Exited, Closed, RequestError, Lost };
    Kind kind=Kind::Lost;
    RequestId request=0;
    SessionId session=0;
    SessionMetadata metadata;
    ContactError contactError=ContactError::None;
    uint16_t errorCode=0;
    uint32_t partialInputBytes=0;
};
struct ConnectionCompletion { CoreCompletion core; ContactError contactError=ContactError::None; bool graceful=false; };
class CoreConnection;
struct StartResult { std::shared_ptr<CoreConnection> connection; LaunchError error=LaunchError::None; int systemError=0; };
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
    SessionEndpoint(std::shared_ptr<State>,std::weak_ptr<CoreConnection>);
    std::shared_ptr<State> state_;
    std::weak_ptr<CoreConnection> connection_;
};
class CoreConnection : public std::enable_shared_from_this<CoreConnection> {
public:
    // Callback runs once outside locks after IPC users stop, before escalation.
    // It must not block or destroy/join this connection; signal the UI restoration
    // owner when terminal APIs require the UI thread. Endpoint handles own payloads
    // and metadata, but only a live connection can submit credit. The transport
    // owns both strong handles, avoiding a connection/endpoint reference cycle.
    static StartResult start(const std::string &absolutePath, std::function<void()> localStopped={});
    ~CoreConnection();
    RequestId createSession(uint16_t rows,uint16_t cols);
    EnqueueResult submitInput(SessionId,const std::vector<uint8_t>&,InputOrigin);
    EnqueueResult submitResize(SessionId,uint16_t,uint16_t);
    RequestId closeSession(SessionId);
    RequestId returnCredit(SessionId,uint32_t);
    void shutdown() noexcept;
    void cancelLocal() noexcept;
    ConnectionCompletion join() noexcept;
    bool pollEvent(ConnectionEvent&) noexcept;
    std::shared_ptr<SessionEndpoint> endpoint(SessionId);
private:
    friend class SessionEndpoint;
    struct State;
    explicit CoreConnection(std::unique_ptr<CoreProcess>,std::function<void()>);
    void reader() noexcept;
    void writer() noexcept;
    void lifecycle() noexcept;
    void lose(ContactError) noexcept;
    bool dispatch(const Frame&);
    void endpointFlushed(SessionId) noexcept;
    std::unique_ptr<State> state_;
};
} // namespace agentvision
