package protocol

import (
	"encoding/binary"
	"strings"
	"unicode/utf8"
)

// Direction is validated separately because a structural frame does not identify
// its sender. Handshake phase, session state, accounting and response correlation
// belong to the connection/session owners, not the codec.
type Direction uint8

const (
	FrontendToBackend Direction = iota + 1
	BackendToFrontend
)

func ValidateDirection(f Frame, d Direction) error {
	if _, e := Decode(f); e != nil {
		return e
	}
	front := false
	switch f.Header.Type {
	case TypeHello, TypeCreateSession, TypeInputBytes, TypeResizeSession, TypeCloseSession, TypeShutdown, TypeOutputCredit:
		front = true
	}
	if (d == FrontendToBackend && front) || (d == BackendToFrontend && !front) {
		return nil
	}
	return invalid("message direction")
}

// RequestIDs is a constant-storage monotonic frontend request validator. The
// serialized connection admission boundary owns it; invalid IDs do not advance
// its watermark. Outstanding lanes and completions are separate state.
type RequestIDs struct{ last RequestID }

func (r *RequestIDs) Accept(id RequestID) error {
	if id == 0 || id <= r.last {
		return invalid("request must strictly increase")
	}
	r.last = id
	return nil
}
func validateIDs(h Header) error {
	switch h.Type {
	case TypeOutputBytes, TypeSessionExited:
		if h.Request != 0 {
			return invalid("async event request must be zero")
		}
	case TypeSessionClosed, TypeError: // correlated reply or async event
	default:
		if h.Request == 0 {
			return invalid("request/reply must have nonzero request")
		}
	}
	switch h.Type {
	case TypeHello, TypeHelloAck, TypeCreateSession, TypeShutdown:
		if h.Session != 0 {
			return invalid("connection message session must be zero")
		}
	case TypeError: // connection or session error
	case TypeAck: // payload determines whether this is Shutdown
	default:
		if h.Session == 0 {
			return invalid("session message must have nonzero session")
		}
	}
	return nil
}
func dimensions(rows, cols uint16) bool {
	return rows > 0 && rows <= MaxDimension && cols > 0 && cols <= MaxDimension
}
func Decode(f Frame) (Message, error) {
	h, b := f.Header, f.Body
	if e := validateHeader(h); e != nil {
		return nil, e
	}
	if len(b) != int(h.Length) {
		return nil, invalid("header/body length mismatch")
	}
	if e := validateIDs(h); e != nil {
		return nil, e
	}
	u16 := binary.BigEndian.Uint16
	u32 := binary.BigEndian.Uint32
	u64 := binary.BigEndian.Uint64
	switch h.Type {
	case TypeHello:
		if len(b) != 4 {
			return nil, invalid("Hello length")
		}
		m := Hello{u16(b[:2]), u16(b[2:])}
		if m.MinMajor > m.MaxMajor {
			return nil, invalid("Hello range")
		}
		return m, nil
	case TypeHelloAck:
		if len(b) != 26 {
			return nil, invalid("HelloAck length")
		}
		m := HelloAck{SelectedMajor: u16(b[:2]), MaxPayload: u32(b[2:6]), Capabilities: u32(b[6:10])}
		copy(m.Epoch[:], b[10:])
		if m.SelectedMajor != 1 || m.MaxPayload != MaxPayload || m.Capabilities != 0 {
			return nil, invalid("HelloAck negotiation")
		}
		return m, nil
	case TypeCreateSession, TypeSessionCreated:
		if len(b) != 8 {
			return nil, invalid("Create/Created length")
		}
		r, c, w := u16(b[:2]), u16(b[2:4]), u32(b[4:])
		if !dimensions(r, c) || w != ReceiveWindow {
			return nil, invalid("Create/Created size/window")
		}
		if h.Type == TypeCreateSession {
			return CreateSession{r, c, w}, nil
		}
		return SessionCreated{r, c, w}, nil
	case TypeInputBytes:
		if len(b) < 1 || len(b) > MaxRawBytes {
			return nil, invalid("InputBytes length")
		}
		return InputBytes{append([]byte(nil), b...)}, nil
	case TypeResizeSession:
		if len(b) != 4 {
			return nil, invalid("ResizeSession length")
		}
		m := ResizeSession{u16(b[:2]), u16(b[2:])}
		if !dimensions(m.Rows, m.Cols) {
			return nil, invalid("ResizeSession dimensions")
		}
		return m, nil
	case TypeCloseSession:
		if len(b) != 0 {
			return nil, invalid("CloseSession length")
		}
		return CloseSession{}, nil
	case TypeShutdown:
		if len(b) != 0 {
			return nil, invalid("Shutdown length")
		}
		return Shutdown{}, nil
	case TypeOutputBytes:
		if len(b) < 9 || len(b) > 8+MaxRawBytes {
			return nil, invalid("OutputBytes length")
		}
		s := u64(b[:8])
		if s == 0 {
			return nil, invalid("OutputBytes sequence")
		}
		return OutputBytes{s, append([]byte(nil), b[8:]...)}, nil
	case TypeSessionExited:
		if len(b) != 15 {
			return nil, invalid("SessionExited length")
		}
		s := ExitStatus{b[0], u32(b[1:5]), b[5] == 1}
		d := DrainReason(b[14])
		if b[5] > 1 || d < DrainEOF || d > DrainExplicitClose {
			return nil, invalid("SessionExited enum")
		}
		switch s.Kind {
		case ExitNormal:
			if s.Value > 255 || s.CoreDump {
				return nil, invalid("exit status")
			}
		case ExitSignal:
			if s.Value == 0 {
				return nil, invalid("signal status")
			}
		case ExitUnavailable:
			if s.Value != 0 || s.CoreDump {
				return nil, invalid("unavailable status")
			}
		default:
			return nil, invalid("exit kind")
		}
		return SessionExited{s, u64(b[6:14]), d}, nil
	case TypeSessionClosed:
		if len(b) != 8 {
			return nil, invalid("SessionClosed length")
		}
		return SessionClosed{u64(b)}, nil
	case TypeError:
		if len(b) < 8 {
			return nil, invalid("Error length")
		}
		m := ErrorMessage{ErrorCode(u16(b[:2])), u32(b[2:6]), string(b[8:])}
		if m.Code < ErrorVersion || m.Code > ErrorInternal || int(u16(b[6:8])) != len(b)-8 || len(m.Message) > MaxErrorMessageBytes || !utf8.ValidString(m.Message) || strings.ContainsRune(m.Message, 0) {
			return nil, invalid("Error code/message")
		}
		if m.PartialInputBytes > MaxRawBytes {
			return nil, invalid("partial input prefix")
		}
		return m, nil
	case TypeAck:
		if len(b) != 2 {
			return nil, invalid("Ack length")
		}
		m := Ack{MessageType(u16(b))}
		switch m.CompletedType {
		case TypeInputBytes, TypeResizeSession, TypeOutputCredit:
			if h.Session == 0 {
				return nil, invalid("session Ack")
			}
		case TypeShutdown:
			if h.Session != 0 {
				return nil, invalid("Shutdown Ack")
			}
		default:
			return nil, invalid("Ack completed type")
		}
		return m, nil
	case TypeOutputCredit:
		if len(b) != 4 {
			return nil, invalid("OutputCredit length")
		}
		m := OutputCredit{u32(b)}
		return m, nil
	}
	return nil, invalid("unknown type")
}

// Encode owns the resulting body and applies exactly the same stateless schema
// validation as Decode. No shell/environment/native wait data is serialized.
func Encode(m Message, request RequestID, session SessionID, version uint16) (Frame, error) {
	if m == nil {
		return Frame{}, invalid("nil message")
	}
	var b []byte
	put16 := func(v uint16) { b = binary.BigEndian.AppendUint16(b, v) }
	put32 := func(v uint32) { b = binary.BigEndian.AppendUint32(b, v) }
	put64 := func(v uint64) { b = binary.BigEndian.AppendUint64(b, v) }
	switch m := m.(type) {
	case Hello:
		put16(m.MinMajor)
		put16(m.MaxMajor)
	case HelloAck:
		put16(m.SelectedMajor)
		put32(m.MaxPayload)
		put32(m.Capabilities)
		b = append(b, m.Epoch[:]...)
	case CreateSession:
		put16(m.Rows)
		put16(m.Cols)
		put32(m.ReceiveWindow)
	case SessionCreated:
		put16(m.Rows)
		put16(m.Cols)
		put32(m.AcceptedWindow)
	case InputBytes:
		if len(m.Bytes) > MaxRawBytes {
			return Frame{}, invalid("InputBytes length")
		}
		b = append(b, m.Bytes...)
	case ResizeSession:
		put16(m.Rows)
		put16(m.Cols)
	case CloseSession, Shutdown:
		b = []byte{}
	case OutputBytes:
		if len(m.Bytes) > MaxRawBytes {
			return Frame{}, invalid("OutputBytes length")
		}
		put64(m.Sequence)
		b = append(b, m.Bytes...)
	case SessionExited:
		b = append(b, m.Status.Kind)
		put32(m.Status.Value)
		core := byte(0)
		if m.Status.CoreDump {
			core = 1
		}
		b = append(b, core)
		put64(m.LastOutputSequence)
		b = append(b, byte(m.DrainReason))
	case SessionClosed:
		put64(m.LastOutputSequence)
	case ErrorMessage:
		if len(m.Message) > MaxErrorMessageBytes {
			return Frame{}, invalid("Error message length")
		}
		put16(uint16(m.Code))
		put32(m.PartialInputBytes)
		put16(uint16(len(m.Message)))
		b = append(b, m.Message...)
	case Ack:
		put16(uint16(m.CompletedType))
	case OutputCredit:
		put32(m.RawBytes)
	default:
		return Frame{}, invalid("unsupported message value")
	}
	f := Frame{Header{Version: version, Type: m.messageType(), Length: uint32(len(b)), Request: request, Session: session}, b}
	if _, e := Decode(f); e != nil {
		return Frame{}, e
	}
	return f, nil
}
