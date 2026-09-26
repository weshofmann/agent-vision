#pragma once
#include "core_connection.h"
#include <tvterm/termctrl.h>
namespace agentvision {
// Strong lifetime bindings. Admission uses endpoint identity, never ID lookup.
class IpcSessionTransport final : public tvterm::SessionTransport {
    std::shared_ptr<CoreConnection> connection_;
    std::shared_ptr<SessionEndpoint> endpoint_;
public:
    IpcSessionTransport(std::shared_ptr<CoreConnection>, std::shared_ptr<SessionEndpoint>) noexcept;
    tvterm::EnqueueResult enqueueInput(TSpan<const char>, tvterm::InputOrigin) noexcept override;
    tvterm::TransportChunk readChunk() noexcept override;
    void consumed(size_t) noexcept override;
    void flushed(uint64_t) noexcept override;
    void resize(TPoint) noexcept override;
    void cancelLocal() noexcept override;
    SessionMetadata metadata() const noexcept;
    RequestId requestClose() noexcept;
};
}
