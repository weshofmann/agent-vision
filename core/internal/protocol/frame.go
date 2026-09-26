package protocol

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"syscall"
)

const (
	HeaderSize                  = 32
	MaxPayload                  = 65536
	MaxRawBytes                 = 32768
	MaxErrorMessageBytes        = 1024
	MaxDimension                = 4096
	ReceiveWindow               = 256 * 1024
	MajorVersion         uint16 = 1
)

type Header struct {
	Version         uint16
	Type            MessageType
	Flags, Reserved uint16
	Length          uint32
	Request         RequestID
	Session         SessionID
}

// Frame owns its Body. ReadFrame never returns storage shared with another frame.
type Frame struct {
	Header Header
	Body   []byte
}

var ErrInvalid = errors.New("invalid AVCP frame")

func invalid(reason string) error { return fmt.Errorf("%w: %s", ErrInvalid, reason) }
func validateHeader(h Header) error {
	if h.Length > MaxPayload {
		return invalid("body length exceeds v1 maximum")
	}
	if h.Flags != 0 || h.Reserved != 0 {
		return invalid("flags/reserved must be zero")
	}
	if h.Type < TypeHello || h.Type > TypeOutputCredit {
		return invalid("unknown message type")
	}
	switch h.Type {
	case TypeHello, TypeHelloAck:
		if h.Version != 0 {
			return invalid("handshake version must be zero")
		}
	case TypeError:
		if h.Version != 0 && h.Version != MajorVersion {
			return invalid("unsupported version")
		}
	default:
		if h.Version != MajorVersion {
			return invalid("unsupported version")
		}
	}
	return nil
}

// ReadFrame validates the header before allocating a bounded body. It does not
// impose deadlines: the connection owns first-byte/handshake timing and failure.
// EOF is clean only before the first header byte; all later EOF is truncated.
func ReadFrame(r io.Reader) (Frame, error) {
	var raw [HeaderSize]byte
	if e := readExact(r, raw[:], true); e != nil {
		return Frame{}, e
	}
	if string(raw[:4]) != "AVCP" {
		return Frame{}, invalid("magic")
	}
	h := Header{Version: binary.BigEndian.Uint16(raw[4:6]), Type: MessageType(binary.BigEndian.Uint16(raw[6:8])), Flags: binary.BigEndian.Uint16(raw[8:10]), Reserved: binary.BigEndian.Uint16(raw[10:12]), Length: binary.BigEndian.Uint32(raw[12:16]), Request: RequestID(binary.BigEndian.Uint64(raw[16:24])), Session: SessionID(binary.BigEndian.Uint64(raw[24:32]))}
	if e := validateHeader(h); e != nil {
		return Frame{}, e
	}
	b := make([]byte, int(h.Length))
	if e := readExact(r, b, false); e != nil {
		return Frame{}, e
	}
	return Frame{h, b}, nil
}
func readExact(r io.Reader, b []byte, cleanEOF bool) error {
	offset := 0
	for offset < len(b) {
		n, e := r.Read(b[offset:])
		if n < 0 || n > len(b)-offset {
			return invalid("reader byte count")
		}
		offset += n
		if offset == len(b) {
			return nil
		}
		if e != nil {
			if errors.Is(e, syscall.EINTR) {
				continue
			}
			if errors.Is(e, io.EOF) {
				if offset == 0 && cleanEOF {
					return io.EOF
				}
				return io.ErrUnexpectedEOF
			}
			return e
		}
		if n == 0 {
			return io.ErrNoProgress
		}
	}
	return nil
}

// WriteFrame emits one complete structurally valid frame. Caller serializes
// writers; any error after a prefix makes contact unusable, never resumable.
func WriteFrame(w io.Writer, f Frame) error {
	if e := validateHeader(f.Header); e != nil {
		return e
	}
	if len(f.Body) != int(f.Header.Length) {
		return invalid("header/body length mismatch")
	}
	var b [HeaderSize]byte
	copy(b[:4], "AVCP")
	h := f.Header
	binary.BigEndian.PutUint16(b[4:6], h.Version)
	binary.BigEndian.PutUint16(b[6:8], uint16(h.Type))
	binary.BigEndian.PutUint16(b[8:10], h.Flags)
	binary.BigEndian.PutUint16(b[10:12], h.Reserved)
	binary.BigEndian.PutUint32(b[12:16], h.Length)
	binary.BigEndian.PutUint64(b[16:24], uint64(h.Request))
	binary.BigEndian.PutUint64(b[24:32], uint64(h.Session))
	if e := writeExact(w, b[:]); e != nil {
		return e
	}
	return writeExact(w, f.Body)
}
func writeExact(w io.Writer, b []byte) error {
	for len(b) > 0 {
		n, e := w.Write(b)
		if n < 0 || n > len(b) {
			return invalid("writer byte count")
		}
		b = b[n:]
		if e != nil {
			if errors.Is(e, syscall.EINTR) {
				continue
			}
			return e
		}
		if n == 0 {
			return io.ErrNoProgress
		}
	}
	return nil
}
