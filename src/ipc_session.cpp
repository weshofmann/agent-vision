#include "ipc_session.h"
#include <utility>
namespace agentvision {
IpcSessionTransport::IpcSessionTransport(std::shared_ptr<CoreConnection> c,
                                       std::shared_ptr<SessionEndpoint> e) noexcept {
    if (c && e && e->belongsTo(*c)) {
        connection_ = std::move(c);
        endpoint_ = std::move(e);
    }
}
tvterm::EnqueueResult IpcSessionTransport::enqueueInput(TSpan<const char> bytes,
                                                      tvterm::InputOrigin origin) noexcept {
    if (!endpoint_) return tvterm::EnqueueResult::Closed;
    try {
        auto result = endpoint_->enqueueInput(std::vector<uint8_t>(bytes.begin(), bytes.end()),
            origin == tvterm::InputOrigin::User ? InputOrigin::User : InputOrigin::EmulatorReply);
        switch (result) {
        case EnqueueResult::Queued: return tvterm::EnqueueResult::Queued;
        case EnqueueResult::Closed: return tvterm::EnqueueResult::Closed;
        case EnqueueResult::Overflow: return tvterm::EnqueueResult::Overflow;
        }
    } catch (...) { return tvterm::EnqueueResult::Overflow; }
    return tvterm::EnqueueResult::Closed;
}
tvterm::TransportChunk IpcSessionTransport::readChunk() noexcept {
    if (!endpoint_) return {};
    auto input = endpoint_->readChunk();
    tvterm::TransportChunk result;
    result.bytes = std::move(input.bytes);
    result.sequence = input.sequence;
    switch (input.kind) {
    case TransportChunk::Kind::Data: result.kind = tvterm::TransportChunk::Kind::Data; break;
    case TransportChunk::Kind::End: result.kind = tvterm::TransportChunk::Kind::End; break;
    case TransportChunk::Kind::Lost: result.kind = tvterm::TransportChunk::Kind::Lost; break;
    }
    return result;
}
void IpcSessionTransport::consumed(size_t n) noexcept { if (endpoint_) endpoint_->consumed(n); }
void IpcSessionTransport::flushed(uint64_t sequence) noexcept { if (endpoint_) endpoint_->flushed(sequence); }
void IpcSessionTransport::resize(TPoint p) noexcept {
    if (!endpoint_) return;
    // Reject conversion overflow before narrowing. Desired local size is separate.
    if (p.x <= 0 || p.y <= 0 || p.x > 65535 || p.y > 65535) return;
    if (endpoint_->resize(uint16_t(p.y), uint16_t(p.x)) == EnqueueResult::Overflow)
        endpoint_->cancelLocal(); // Explicit backend loss; never fabricate exit.
}
void IpcSessionTransport::cancelLocal() noexcept { if (endpoint_) endpoint_->cancelLocal(); }
SessionMetadata IpcSessionTransport::metadata() const noexcept { return endpoint_ ? endpoint_->metadata() : SessionMetadata{}; }
RequestId IpcSessionTransport::requestClose() noexcept { return endpoint_ ? endpoint_->requestClose() : 0; }
}
