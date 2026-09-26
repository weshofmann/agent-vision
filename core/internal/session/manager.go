package session

import (
	"context"
	"crypto/rand"
	"encoding/binary"
	"errors"
	"fmt"
	"sync"
	"time"

	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
)

type Event struct {
	Session protocol.SessionID
	Message protocol.Message
	Request protocol.RequestID
	Output  *OutputTicket
}

// Enqueue is bounded and nonblocking. Socket writes belong to the connection
// writer, never this admission boundary. An error means contact is unusable.
type Sink interface{ Enqueue(Event) error }
type AdmissionError struct{ Code protocol.ErrorCode }

func (e *AdmissionError) Error() string { return fmt.Sprintf("admission rejected (%d)", e.Code) }

type sessionState uint8

const (
	starting sessionState = iota
	live
	draining
	closing
	exited
)

type reservation struct {
	id                protocol.SessionID
	request           protocol.RequestID
	config            SpawnConfig
	ctx               context.Context
	cancel            context.CancelFunc
	state             sessionState
	published         bool
	resources         *Resources // startup writes; startDone publishes ownership
	startDone         chan struct{}
	observed          chan struct{}
	result            ProcessResult  // observed closes after the sole Process.Done read
	ioWorkers         sync.WaitGroup // FD workers: register before startDone, join before rollback
	cleanupMu         sync.Mutex
	resourceCleanupMu sync.Mutex
	cleanupRunning    bool
	cleanupComplete   bool
	cleanupDone       chan struct{}
	cleanupErr        error
	closeRequest      protocol.RequestID
	commandCtx        context.Context
	commandCancel     context.CancelFunc
	commands          [policy.OrdinarySlots]command
	commandHead       int
	commandQueued     int
	inputBytes        int
	commandWake       chan struct{}
	ledger            *DeliveryLedger
	sealed            chan struct{}
	naturalDone       chan struct{}
	monitorDone       chan struct{}
	sequence          uint64
	drainReason       protocol.DrainReason
	exitAt            time.Time
	exitedPublished   bool
	readWaitCancel    context.CancelFunc
}

// Manager serializes admission, cancellation and Created publication. Resource
// work never runs under mu. Each reservation owns late-returned spawn resources.
type Manager struct {
	mu              sync.Mutex
	commandIO       commandIO
	outputIO        outputIO
	outputBudget    *outputBudget
	creditCtx       context.Context
	creditCancel    context.CancelFunc
	monitorWorkers  sync.WaitGroup
	commandCount    int
	now             func() time.Time
	policy          policy.Policy
	spawner         Spawner
	sink            Sink
	epoch           [16]byte
	nextID          protocol.SessionID
	exhausted       bool
	initErr         error
	records         map[protocol.SessionID]*reservation
	stopping        bool
	usable          bool
	shutdownRequest protocol.RequestID
	done            chan struct{}
	stopErr         error
}

func NewManager(p policy.Policy, s Spawner, sink Sink) *Manager {
	m := &Manager{now: time.Now, outputIO: nativeOutputIO{}, outputBudget: newOutputBudget(), commandIO: nativeCommandIO{}, policy: p, spawner: s, sink: sink, records: make(map[protocol.SessionID]*reservation), usable: true, done: make(chan struct{})}
	m.creditCtx, m.creditCancel = context.WithCancel(context.Background())
	_, m.initErr = rand.Read(m.epoch[:])
	// Random initial counter is opaque; it contains no process or address data.
	var seed [8]byte
	if _, e := rand.Read(seed[:]); e != nil {
		m.initErr = errors.Join(m.initErr, e)
	}
	m.nextID = protocol.SessionID(binary.BigEndian.Uint64(seed[:]))
	if m.nextID == 0 {
		m.nextID = 1
	}
	return m
}
func (m *Manager) Epoch() [16]byte       { return m.epoch }
func (m *Manager) Done() <-chan struct{} { return m.done }
func reject(c protocol.ErrorCode) error  { return &AdmissionError{Code: c} }
func (m *Manager) Admit(f protocol.Frame) error {
	msg, e := protocol.Decode(f)
	if e != nil {
		return reject(protocol.ErrorProtocol)
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.stopping && f.Header.Type != protocol.TypeOutputCredit {
		return reject(protocol.ErrorState)
	}
	if m.initErr != nil {
		return reject(protocol.ErrorInternal)
	}
	switch v := msg.(type) {
	case protocol.CreateSession:
		if len(m.records) >= policy.MaxSessions || m.exhausted {
			return reject(protocol.ErrorLimit)
		}
		ctx, cancel := context.WithCancel(context.Background())
		id := m.nextID
		m.nextID++
		if m.nextID == 0 {
			m.exhausted = true
		}
		r := &reservation{id: id, request: f.Header.Request, config: SpawnConfig{Rows: v.Rows, Cols: v.Cols}, ctx: ctx, cancel: cancel, state: starting, startDone: make(chan struct{}), observed: make(chan struct{}), cleanupDone: make(chan struct{})}
		m.records[id] = r
		go m.start(r, v.ReceiveWindow)
	case protocol.InputBytes, protocol.ResizeSession:
		return m.admitCommandLocked(f, msg)
	case protocol.OutputCredit:
		r := m.records[f.Header.Session]
		if r == nil {
			return reject(protocol.ErrorUnknownSession)
		}
		if !r.published {
			return reject(protocol.ErrorState)
		}
		disposition, event := r.ledger.ApplyCredit(f.Header.Request, v.RawBytes)
		if disposition == CreditRejected {
			return reject(protocol.ErrorState)
		}
		if event != nil {
			m.emitLocked(*event)
		}
	case protocol.CloseSession:
		r := m.records[f.Header.Session]
		if r == nil {
			return reject(protocol.ErrorUnknownSession)
		}
		if !r.published || r.state == closing {
			return reject(protocol.ErrorState)
		}
		r.state = closing
		r.closeRequest = f.Header.Request
		r.cancel()
		go m.closeSession(r)
	case protocol.Shutdown:
		m.shutdownRequest = f.Header.Request
		m.stopLocked()
	default:
		// State validation for messages outside manager admission.
		if f.Header.Session != 0 && m.records[f.Header.Session] == nil {
			return reject(protocol.ErrorUnknownSession)
		}
		return reject(protocol.ErrorState)
	}
	return nil
}
func (m *Manager) Shutdown(ctx context.Context) error {
	m.mu.Lock()
	m.stopLocked()
	m.mu.Unlock()
	select {
	case <-m.done:
		m.mu.Lock()
		defer m.mu.Unlock()
		return m.stopErr
	case <-ctx.Done():
		return ctx.Err()
	}
}
func (m *Manager) Abort(_ error) { m.mu.Lock(); m.usable = false; m.stopLocked(); m.mu.Unlock() }
func (m *Manager) stopLocked() {
	if m.stopping {
		return
	}
	m.stopping = true
	m.creditCancel()
	records := make([]*reservation, 0, len(m.records))
	for _, r := range m.records {
		r.cancel()
		records = append(records, r)
	}
	go m.stop(records)
}

// emitLocked linearizes bounded queue admission with cancel/commit. No socket I/O.
func (m *Manager) emitLocked(e Event) {
	if !m.usable {
		if e.Output != nil {
			e.Output.FailWrite(errors.New("contact unavailable"))
		}
		return
	}
	if err := m.sink.Enqueue(e); err != nil {
		if e.Output != nil {
			e.Output.FailWrite(err)
		}
		m.usable = false
		m.stopLocked()
	}
}
func (m *Manager) stop(records []*reservation) {
	ctx, cancel := context.WithTimeout(context.Background(), m.policy.CleanupTimeout)
	defer cancel()
	var wg sync.WaitGroup
	errs := make(chan error, len(records))
	for _, r := range records {
		wg.Add(1)
		go func(r *reservation) { defer wg.Done(); errs <- m.cleanup(ctx, r) }(r)
	}
	wg.Wait()
	m.monitorWorkers.Wait()
	close(errs)
	var err error
	for e := range errs {
		err = errors.Join(err, e)
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	m.stopErr = err
	if m.shutdownRequest != 0 {
		if err == nil {
			m.emitLocked(Event{Request: m.shutdownRequest, Message: protocol.Ack{CompletedType: protocol.TypeShutdown}})
		} else {
			m.emitLocked(Event{Request: m.shutdownRequest, Message: protocol.ErrorMessage{Code: protocol.ErrorStatusUnavailable, Message: "backend cleanup failed"}})
		}
	}
	close(m.done)
}
func (m *Manager) closeSession(r *reservation) {
	ctx, cancel := context.WithTimeout(context.Background(), m.policy.CleanupTimeout)
	defer cancel()
	_ = m.cleanup(ctx, r)
}
