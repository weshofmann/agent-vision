package server

import (
	"bytes"
	"context"
	"errors"
	"io"
	"net"
	"sync"
	"syscall"
	"time"

	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"github.com/weshofmann/agent-vision/core/internal/session"
)

var ErrContact = errors.New("core contact failed")

// Serve owns the sole decoder and writer. The scheduler must be the sink used
// when constructing m; setup occurs before any request is admitted.
func Serve(ctx context.Context, c net.Conn, m *session.Manager, p policy.Policy) error {
	s, ok := m.DeliverySink().(*Scheduler)
	if !ok {
		c.Close()
		m.Abort(ErrContact)
		return errors.Join(ErrContact, m.Shutdown(ctx))
	}
	raw, err := validateRawConn(c)
	if err != nil {
		c.Close()
		m.Abort(err)
		cleanup, cancel := context.WithTimeout(context.Background(), p.CleanupTimeout)
		defer cancel()
		return errors.Join(err, m.Shutdown(cleanup))
	}
	x := &connection{conn: c, raw: raw, manager: m, scheduler: s, policy: p, changed: make(chan struct{})}
	run, cancel := context.WithCancel(ctx)
	defer cancel()
	read := make(chan error, 1)
	write := make(chan error, 1)
	x.workers.Add(2)
	go func() { read <- x.decode(run) }()
	go func() { write <- x.write(run) }()
	err = nil
	select {
	case err = <-write:
	case err = <-read:
	case <-ctx.Done():
		err = ctx.Err()
	case <-m.Done():
		// Manager.Done is local cleanup, not response delivery. If contact remains
		// usable the writer still owes accepted completions, including held Credit.
		x.mu.Lock()
		diagnostic := x.fatal != 0
		x.mu.Unlock()
		if !m.ContactUsable() && !diagnostic {
			err = ErrContact
		} else {
			select {
			case err = <-write:
			case err = <-read:
			case <-ctx.Done():
				err = ctx.Err()
			}
		}
	}
	cancel()
	c.Close()
	s.Close(ErrContact)
	m.Abort(err)
	// Buffered result channels permit both workers to finish without this select
	// consuming a particular result twice.
	x.workers.Wait()
	cleanup, cancelCleanup := context.WithTimeout(context.Background(), p.CleanupTimeout)
	defer cancelCleanup()
	return errors.Join(err, m.Shutdown(cleanup))
}

type correlation struct {
	request protocol.RequestID
	session protocol.SessionID
	typ     protocol.MessageType
}

const acceptedSlots = policy.OrdinarySlots + policy.CreditSlots + policy.CloseSlots + policy.ShutdownSlots
const rejectionSlots = 128

type connection struct {
	conn        net.Conn
	raw         syscall.RawConn
	manager     *session.Manager
	scheduler   *Scheduler
	policy      policy.Policy
	workers     sync.WaitGroup
	mu          sync.Mutex
	accepted    [acceptedSlots]correlation
	rejected    [rejectionSlots]protocol.RequestID
	changed     chan struct{}
	shutdown    protocol.RequestID
	fatal       protocol.RequestID
	finished    bool
	established bool
}

func (x *connection) notify() { close(x.changed); x.changed = make(chan struct{}) }
func (x *connection) reserve(f protocol.Frame) protocol.ErrorCode {
	x.mu.Lock()
	defer x.mu.Unlock()
	if x.finished {
		return protocol.ErrorState
	}
	n := 0
	limit := policy.OrdinarySlots
	for _, a := range x.accepted {
		same := false
		switch f.Header.Type {
		case protocol.TypeOutputCredit:
			same = a.typ == protocol.TypeOutputCredit
			limit = policy.CreditSlots
		case protocol.TypeCloseSession:
			same = a.typ == protocol.TypeCloseSession
			limit = policy.CloseSlots
		case protocol.TypeShutdown:
			same = a.typ == protocol.TypeShutdown
			limit = 1
		default:
			same = a.typ == protocol.TypeHello || a.typ == protocol.TypeCreateSession || a.typ == protocol.TypeInputBytes || a.typ == protocol.TypeResizeSession
		}
		if same {
			n++
			if (f.Header.Type == protocol.TypeOutputCredit || f.Header.Type == protocol.TypeCloseSession) && a.session == f.Header.Session {
				return protocol.ErrorState
			}
		}
	}
	if n >= limit {
		return protocol.ErrorLimit
	}
	for i, a := range x.accepted {
		if a.request == 0 {
			x.accepted[i] = correlation{f.Header.Request, f.Header.Session, f.Header.Type}
			if f.Header.Type == protocol.TypeShutdown {
				x.shutdown = f.Header.Request
			}
			return 0
		}
	}
	return protocol.ErrorLimit
}
func (x *connection) retire(req protocol.RequestID) {
	x.mu.Lock()
	defer x.mu.Unlock()
	x.retireLocked(req)
}
func (x *connection) retireLocked(req protocol.RequestID) {
	for i, a := range x.accepted {
		if a.request == req {
			x.accepted[i] = correlation{}
			x.notify()
			return
		}
	}
	for i, r := range x.rejected {
		if r == req {
			x.rejected[i] = 0
			x.notify()
			return
		}
	}
}
func (x *connection) reject(f protocol.Frame, code protocol.ErrorCode, fatal bool) error {
	x.mu.Lock()
	slot := -1
	for i, r := range x.rejected {
		if r == 0 {
			slot = i
			break
		}
	}
	if slot < 0 {
		x.mu.Unlock()
		return ErrContact
	}
	x.rejected[slot] = f.Header.Request
	if fatal {
		x.fatal = f.Header.Request
	}
	x.mu.Unlock()
	return x.scheduler.Enqueue(session.Event{Request: f.Header.Request, Session: f.Header.Session, Message: protocol.ErrorMessage{Code: code, Message: "request rejected"}})
}
func (x *connection) decode(ctx context.Context) error {
	// Registration is completed by Serve before workers can be joined.
	defer x.workers.Done()
	ids := protocol.RequestIDs{}
	established := false
	handshake := time.Now().Add(x.policy.HandshakeTimeout)
	for {
		deadline := time.Time{}
		if !established {
			deadline = handshake
		}
		if e := x.conn.SetReadDeadline(deadline); e != nil {
			return e
		}
		var first [1]byte
		if _, e := io.ReadFull(x.conn, first[:]); e != nil {
			if errors.Is(e, io.EOF) {
				return nil
			}
			return e
		}
		partial := time.Now().Add(x.policy.FrameTimeout)
		if !established && handshake.Before(partial) {
			partial = handshake
		}
		if e := x.conn.SetReadDeadline(partial); e != nil {
			return e
		}
		f, e := protocol.ReadFrame(io.MultiReader(bytes.NewReader(first[:]), x.conn))
		if e != nil {
			return e
		}
		if e = protocol.ValidateDirection(f, protocol.FrontendToBackend); e != nil {
			return x.fatalError(ctx, f, protocol.ErrorProtocol)
		}
		if e = ids.Accept(f.Header.Request); e != nil {
			return x.fatalError(ctx, f, protocol.ErrorProtocol)
		}
		if !established {
			if f.Header.Type != protocol.TypeHello {
				return x.fatalError(ctx, f, protocol.ErrorProtocol)
			}
			v, _ := protocol.Decode(f)
			h := v.(protocol.Hello)
			if h.MinMajor > 1 || h.MaxMajor < 1 {
				return x.fatalError(ctx, f, protocol.ErrorVersion)
			}
			if code := x.reserve(f); code != 0 {
				return ErrContact
			}
			e = x.scheduler.Enqueue(session.Event{Request: f.Header.Request, Message: protocol.HelloAck{SelectedMajor: 1, MaxPayload: protocol.MaxPayload, Epoch: x.manager.Epoch()}})
			if e != nil {
				return e
			}
			established = true
			x.mu.Lock()
			x.established = true
			x.mu.Unlock()
			continue
		}
		if f.Header.Type == protocol.TypeHello {
			return x.fatalError(ctx, f, protocol.ErrorProtocol)
		}
		if code := x.reserve(f); code != 0 {
			if e = x.reject(f, code, false); e != nil {
				return e
			}
			continue
		}
		e = x.manager.Admit(f)
		if e != nil {
			// Convert the accepted reservation into a bounded rejection record; neither
			// record owns a body or any PTY work.
			x.retire(f.Header.Request)
			var a *session.AdmissionError
			if !errors.As(e, &a) {
				return e
			}
			if e = x.reject(f, a.Code, false); e != nil {
				return e
			}
		} else if f.Header.Type == protocol.TypeShutdown {
			x.mu.Lock()
			x.shutdown = f.Header.Request
			x.mu.Unlock()
		}
	}
}
func (x *connection) fatalError(ctx context.Context, f protocol.Frame, code protocol.ErrorCode) error {
	if f.Header.Request == 0 {
		return protocol.ErrInvalid
	}
	if e := x.reject(f, code, true); e != nil {
		return e
	}
	// Cleanup begins immediately; the bounded diagnostic cannot gate ownership.
	x.manager.Abort(protocol.ErrInvalid)
	select {
	case <-ctx.Done():
		return protocol.ErrInvalid
	case <-time.After(x.policy.WriteTimeout):
		return protocol.ErrInvalid
	}
}

func (x *connection) othersPending(req protocol.RequestID) bool {
	x.mu.Lock()
	defer x.mu.Unlock()
	for _, a := range x.accepted {
		if a.request != 0 && a.request != req {
			return true
		}
	}
	return false
}
func (x *connection) write(ctx context.Context) error {
	defer x.workers.Done()
	var held *session.Event
	for {
		var e session.Event
		var err error
		if held != nil && !x.othersPending(held.Request) {
			e = *held
			held = nil
		} else {
			e, err = x.scheduler.Next(ctx)
			if err != nil {
				return err
			}
		}
		isShutdown := false
		if a, ok := e.Message.(protocol.Ack); ok && a.CompletedType == protocol.TypeShutdown {
			isShutdown = true
			if x.othersPending(e.Request) {
				copyEvent := e
				held = &copyEvent
				x.scheduler.Complete(e)
				continue
			}
		}
		ticket := e.Output
		if ticket != nil {
			if err = ticket.BeginWrite(); err != nil {
				x.scheduler.Complete(e)
				ticket.FailWrite(err)
				return err
			}
		}
		version := uint16(1)
		if _, ok := e.Message.(protocol.HelloAck); ok {
			version = 0
		}
		x.mu.Lock()
		fatal := e.Request != 0 && e.Request == x.fatal
		established := x.established
		x.mu.Unlock()
		if fatal { // A pre-negotiation Error uses version zero.
			// Hello/type version alone is insufficient for pre-Hello traffic.
			if !established {
				version = 0
			}
		}
		f, err := protocol.Encode(e.Message, e.Request, e.Session, version)
		if err == nil {
			err = x.writeFrame(f)
		}
		f = protocol.Frame{}
		x.scheduler.Complete(e)
		req := e.Request
		e = session.Event{}
		if err != nil {
			if ticket != nil {
				ticket.FailWrite(err)
			}
			return err
		}
		// Correlation retirement occurred atomically at the final socket syscall.
		if ticket != nil {
			completion, err := ticket.CommitWrite()
			if err != nil {
				return err
			}
			if completion != nil {
				if err = x.scheduler.Enqueue(*completion); err != nil {
					return err
				}
			}
		}
		if fatal {
			return protocol.ErrInvalid
		}
		x.mu.Lock()
		shutdownError := req != 0 && req == x.shutdown
		x.mu.Unlock()
		if shutdownError && !isShutdown {
			return errors.New("owned cleanup failed")
		}
		if isShutdown {
			x.mu.Lock()
			x.finished = true
			x.mu.Unlock()
			return nil
		}
	}
}
