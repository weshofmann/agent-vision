#pragma once
#include <cstdint>
#include <vector>
namespace agentvision {
using RequestId = uint64_t;
using SessionId = uint64_t;
enum class MessageType : uint16_t { Hello=1, HelloAck, CreateSession, SessionCreated, InputBytes, ResizeSession, CloseSession, Shutdown, OutputBytes, SessionExited, SessionClosed, Error, Ack, OutputCredit };
enum class Direction { FrontendToBackend, BackendToFrontend };
enum class DecodeError { None, Truncated, Magic, Version, Type, Flags, Oversize, Length, IDs, Schema, Direction };
struct Frame { uint16_t version=1; MessageType type=MessageType::Hello; RequestId request=0; SessionId session=0; std::vector<uint8_t> payload; };
struct DecodeResult { bool success=false; Frame frame; DecodeError error=DecodeError::None; };
DecodeResult decodeFrame(const std::vector<uint8_t> &bytes);
// Invalid semantic frames encode to an empty vector; no partial wire data.
std::vector<uint8_t> encodeFrame(const Frame &frame);
DecodeError validateDirection(const Frame &frame, Direction direction);
class RequestIDs { uint64_t last_=0; public: bool accept(RequestId id) noexcept; };
enum class SessionState : uint8_t { Starting, Running, Exited, Closed, Lost };
enum class ExitKind : uint8_t { Exit=1, Signal=2, Unavailable=3 };
struct ExitStatus { ExitKind kind=ExitKind::Unavailable; uint32_t value=0; bool coreDump=false; };
enum class DrainReason : uint8_t { EOFReached=1, NoData, ByteCap, TimeCap, CreditCap, IOError, ExplicitClose };
struct SessionMetadata { SessionId id=0; SessionState state=SessionState::Starting; ExitStatus status; DrainReason drainReason=DrainReason::NoData; uint64_t lastSequence=0; };
} // namespace agentvision
