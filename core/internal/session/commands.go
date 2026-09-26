package session

import (
	"context"
	"errors"
	"syscall"

	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
)

// commandIO uses only the captured descriptor; readiness honors cancellation.
type commandIO interface {
	Write(int, []byte) (int, error)
	Resize(int, uint16, uint16) error
	Wait(context.Context, int) error
}
type command struct {
	request    protocol.RequestID
	input      []byte
	rows, cols uint16
}

// admitCommandLocked bounds both allocation bytes and metadata including the
// worker's in-flight command. mu protects accounting, never PTY I/O/readiness.
func (m *Manager) admitCommandLocked(f protocol.Frame, msg protocol.Message) error {
	r := m.records[f.Header.Session]
	if r == nil {
		return reject(protocol.ErrorUnknownSession)
	}
	if !r.published || r.state != live || r.commandCtx.Err() != nil {
		return reject(protocol.ErrorState)
	}
	c := command{request: f.Header.Request}
	switch v := msg.(type) {
	case protocol.InputBytes:
		c.input = v.Bytes
	case protocol.ResizeSession:
		c.rows, c.cols = v.Rows, v.Cols
	}
	if m.commandCount >= policy.OrdinarySlots || r.inputBytes+len(c.input) > policy.InputBytesPerSession {
		return reject(protocol.ErrorLimit)
	}
	// Decode has already copied Input into an owned payload, independent of
	// the caller's frame and future decoder buffer reuse. Retain only that copy.
	pos := (r.commandHead + r.commandQueued) % len(r.commands)
	r.commands[pos] = c
	r.commandQueued++
	r.inputBytes += len(c.input)
	m.commandCount++
	select {
	case r.commandWake <- struct{}{}:
	default:
	}
	return nil
}

func (m *Manager) runCommands(r *reservation, fd int) {
	defer r.ioWorkers.Done()
	for {
		m.mu.Lock()
		if r.commandQueued == 0 {
			cancelled := r.commandCtx.Err() != nil
			m.mu.Unlock()
			if cancelled {
				return
			}
			select {
			case <-r.commandWake:
			case <-r.commandCtx.Done():
			}
			continue
		}
		c := r.commands[r.commandHead]
		r.commands[r.commandHead] = command{}
		r.commandHead = (r.commandHead + 1) % len(r.commands)
		r.commandQueued--
		m.mu.Unlock()

		var written uint32
		var code protocol.ErrorCode
		var fatal bool
		typ := protocol.TypeResizeSession
		if c.input != nil {
			typ = protocol.TypeInputBytes
			written, code = m.writeInput(r, fd, c.input)
			fatal = code == protocol.ErrorInputTimeout || code == protocol.ErrorPTYIO
		} else if r.commandCtx.Err() != nil {
			code = protocol.ErrorState
		} else if err := m.commandIO.Resize(fd, c.rows, c.cols); err != nil {
			code = protocol.ErrorResize
		}

		m.mu.Lock()
		// Cancellation is an admission/publication barrier as well as an I/O wakeup.
		// A complete write that loses this race reports its exact full prefix.
		if r.commandCtx.Err() != nil {
			code = protocol.ErrorState
			fatal = false
		}
		r.inputBytes -= len(c.input)
		m.commandCount--
		if code == 0 {
			m.emitLocked(Event{Session: r.id, Request: c.request, Message: protocol.Ack{CompletedType: typ}})
		} else {
			m.emitLocked(Event{Session: r.id, Request: c.request, Message: protocol.ErrorMessage{Code: code, PartialInputBytes: written, Message: "PTY command failed"}})
		}
		if fatal && r.state != closing {
			r.state = closing
			r.cancel()
			go m.closeSession(r)
		}
		m.mu.Unlock()
	}
}

// The timeout begins when this command makes its first attempt, never while it
// waits in the ordered queue. All retries consume the same deadline and prefix.
func (m *Manager) writeInput(r *reservation, fd int, b []byte) (uint32, protocol.ErrorCode) {
	ctx, cancel := context.WithTimeout(r.commandCtx, m.policy.InputTimeout)
	defer cancel()
	written := 0
	failure := func() (uint32, protocol.ErrorCode) {
		if r.commandCtx.Err() != nil {
			return uint32(written), protocol.ErrorState
		}
		if errors.Is(ctx.Err(), context.DeadlineExceeded) {
			return uint32(written), protocol.ErrorInputTimeout
		}
		return uint32(written), protocol.ErrorPTYIO
	}
	for written < len(b) {
		if ctx.Err() != nil {
			return failure()
		}
		n, err := m.commandIO.Write(fd, b[written:])
		if n < 0 || n > len(b)-written {
			return uint32(written), protocol.ErrorPTYIO
		}
		written += n
		if err == syscall.EINTR {
			continue
		}
		if err != nil && err != syscall.EAGAIN && err != syscall.EWOULDBLOCK {
			return failure()
		}
		if written == len(b) {
			return uint32(written), 0
		}
		if err == syscall.EAGAIN || err == syscall.EWOULDBLOCK || n == 0 {
			if err = m.commandIO.Wait(ctx, fd); err != nil {
				return failure()
			}
		}
	}
	return uint32(written), 0
}
