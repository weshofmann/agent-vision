// Package protocol implements the language-neutral AVCP v1 wire format.
package protocol

type SessionID uint64
type RequestID uint64
type MessageType = uint16

const (
	TypeHello MessageType = iota + 1
	TypeHelloAck
	TypeCreateSession
	TypeSessionCreated
	TypeInputBytes
	TypeResizeSession
	TypeCloseSession
	TypeShutdown
	TypeOutputBytes
	TypeSessionExited
	TypeSessionClosed
	TypeError
	TypeAck
	TypeOutputCredit
)

type ErrorCode uint16

const (
	ErrorVersion ErrorCode = iota + 1
	ErrorProtocol
	ErrorUnknownSession
	ErrorState
	ErrorLimit
	ErrorSpawn
	ErrorPTYIO
	ErrorInputTimeout
	ErrorResize
	ErrorStatusUnavailable
	ErrorInternal
)

type DrainReason uint8

const (
	DrainEOF DrainReason = iota + 1
	DrainNoData
	DrainByteCap
	DrainTimeCap
	DrainCreditCap
	DrainIOError
	DrainExplicitClose
)
const (
	ExitNormal      uint8 = 1
	ExitSignal      uint8 = 2
	ExitUnavailable uint8 = 3
)

type ExitStatus struct {
	Kind     uint8
	Value    uint32
	CoreDump bool
}

// Message is implemented only by the fourteen typed v1 payloads.
type Message interface{ messageType() MessageType }
type Hello struct{ MinMajor, MaxMajor uint16 }
type HelloAck struct {
	SelectedMajor            uint16
	MaxPayload, Capabilities uint32
	Epoch                    [16]byte
}
type CreateSession struct {
	Rows, Cols    uint16
	ReceiveWindow uint32
}
type SessionCreated struct {
	Rows, Cols     uint16
	AcceptedWindow uint32
}
type InputBytes struct{ Bytes []byte }
type ResizeSession struct{ Rows, Cols uint16 }
type CloseSession struct{}
type Shutdown struct{}
type OutputBytes struct {
	Sequence uint64
	Bytes    []byte
}
type SessionExited struct {
	Status             ExitStatus
	LastOutputSequence uint64
	DrainReason        DrainReason
}
type SessionClosed struct{ LastOutputSequence uint64 }
type ErrorMessage struct {
	Code              ErrorCode
	PartialInputBytes uint32
	Message           string
}
type Ack struct{ CompletedType MessageType }
type OutputCredit struct{ RawBytes uint32 }

func (Hello) messageType() MessageType          { return TypeHello }
func (HelloAck) messageType() MessageType       { return TypeHelloAck }
func (CreateSession) messageType() MessageType  { return TypeCreateSession }
func (SessionCreated) messageType() MessageType { return TypeSessionCreated }
func (InputBytes) messageType() MessageType     { return TypeInputBytes }
func (ResizeSession) messageType() MessageType  { return TypeResizeSession }
func (CloseSession) messageType() MessageType   { return TypeCloseSession }
func (Shutdown) messageType() MessageType       { return TypeShutdown }
func (OutputBytes) messageType() MessageType    { return TypeOutputBytes }
func (SessionExited) messageType() MessageType  { return TypeSessionExited }
func (SessionClosed) messageType() MessageType  { return TypeSessionClosed }
func (ErrorMessage) messageType() MessageType   { return TypeError }
func (Ack) messageType() MessageType            { return TypeAck }
func (OutputCredit) messageType() MessageType   { return TypeOutputCredit }
