package session

import (
	"context"
	"errors"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"sync"
	"testing"
	"time"
)

type recordingSink struct {
	mu     sync.Mutex
	events []Event
	wake   chan struct{}
}

func newSink() *recordingSink { return &recordingSink{wake: make(chan struct{}, 128)} }
func (s *recordingSink) Enqueue(e Event) error {
	s.mu.Lock()
	s.events = append(s.events, e)
	s.mu.Unlock()
	s.wake <- struct{}{}
	return nil
}
func (s *recordingSink) snapshot() []Event {
	s.mu.Lock()
	defer s.mu.Unlock()
	return append([]Event(nil), s.events...)
}
func (s *recordingSink) await(t *testing.T, n int) []Event {
	t.Helper()
	deadline := time.After(time.Second)
	for {
		e := s.snapshot()
		if len(e) >= n {
			return e
		}
		select {
		case <-s.wake:
		case <-deadline:
			t.Fatalf("response watchdog: got %d want %d", len(e), n)
		}
	}
}
func frame(t *testing.T, msg protocol.Message, req protocol.RequestID, id protocol.SessionID) protocol.Frame {
	t.Helper()
	f, e := protocol.Encode(msg, req, id, 1)
	if e != nil {
		t.Fatal(e)
	}
	return f
}
func create(t *testing.T, m *Manager, req protocol.RequestID) {
	t.Helper()
	if e := m.Admit(frame(t, protocol.CreateSession{Rows: 24, Cols: 80, ReceiveWindow: 262144}, req, 0)); e != nil {
		t.Fatalf("Create admission: %v", e)
	}
}
func errorCode(t *testing.T, e error, want protocol.ErrorCode) {
	t.Helper()
	var pe *AdmissionError
	if !errors.As(e, &pe) || pe.Code != want {
		t.Fatalf("admission error %v want code %d", e, want)
	}
}
func shutdown(t *testing.T, m *Manager) {
	t.Helper()
	ctx, c := context.WithTimeout(context.Background(), time.Second)
	defer c()
	if e := m.Shutdown(ctx); e != nil {
		t.Fatal(e)
	}
}

type spawnFunc func(context.Context, SpawnConfig) (*Resources, error)

func (s spawnFunc) Spawn(c context.Context, cfg SpawnConfig) (*Resources, error) { return s(c, cfg) }

// Catches opening before reservation and admitting a seventeenth Starting slot.
func TestReservationLimit(t *testing.T) {
	entered := make(chan struct{}, 16)
	release := make(chan struct{})
	s := newSink()
	m := NewManager(policy.Default(), spawnFunc(func(ctx context.Context, _ SpawnConfig) (*Resources, error) {
		entered <- struct{}{}
		<-release
		return nil, ctx.Err()
	}), s)
	for i := 1; i <= 16; i++ {
		create(t, m, protocol.RequestID(i))
	}
	for i := 0; i < 16; i++ {
		select {
		case <-entered:
		case <-time.After(time.Second):
			t.Fatal("reserved start not dispatched")
		}
	}
	errorCode(t, m.Admit(frame(t, protocol.CreateSession{Rows: 24, Cols: 80, ReceiveWindow: 262144}, 17, 0)), protocol.ErrorLimit)
	m.Abort(errors.New("synthetic EOF"))
	close(release)
	shutdown(t, m)
	if len(s.snapshot()) != 0 {
		t.Fatal("EOF emitted undeliverable response")
	}
}

// Catches reuse across release and failure to isolate backend runs by epoch.
func TestOpaqueEpochIDs(t *testing.T) {
	s := newSink()
	m := NewManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
		return completedResources(protocol.ExitStatus{Kind: 1, Value: 17}), nil
	}), s)
	seen := map[protocol.SessionID]bool{}
	for i := 1; i <= 32; i++ {
		create(t, m, protocol.RequestID(i*2-1))
		e := s.await(t, (i-1)*3+1)
		id := e[len(e)-1].Session
		if id == 0 || seen[id] || id == 123 {
			t.Fatal("zero, repeated or process-derived ID")
		}
		seen[id] = true
		if err := m.Admit(frame(t, protocol.CloseSession{}, protocol.RequestID(i*2), id)); err != nil {
			t.Fatal(err)
		}
		e = s.await(t, i*3)
		if _, ok := e[len(e)-1].Message.(protocol.SessionClosed); !ok {
			t.Fatal("missing Close completion")
		}
		errorCode(t, m.Admit(frame(t, protocol.CloseSession{}, 1000, id)), protocol.ErrorUnknownSession)
	}
	other := NewManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) { return nil, errors.New("unused") }), newSink())
	if m.Epoch() == other.Epoch() || m.Epoch() == ([16]byte{}) {
		t.Fatal("epoch failed run isolation")
	}
	shutdown(t, m)
	shutdown(t, other)
}

// Catches loss of typed status before explicit Close.
func TestStartupExitedRecord(t *testing.T) {
	s := newSink()
	m := NewManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
		return completedResources(protocol.ExitStatus{Kind: 1, Value: 17}), nil
	}), s)
	create(t, m, 1)
	id := s.await(t, 1)[0].Session
	if e := m.Admit(frame(t, protocol.CloseSession{}, 2, id)); e != nil {
		t.Fatal(e)
	}
	e := s.await(t, 3)
	v, ok := e[1].Message.(protocol.SessionExited)
	if !ok || v.Status != (protocol.ExitStatus{Kind: 1, Value: 17}) {
		t.Fatalf("lost status %+v", e)
	}
	shutdown(t, m)
}

// Catches natural completion freeing a reservation before Close releases it.
func TestReservationExitedLimit(t *testing.T) {
	s := newSink()
	m := NewManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
		return completedResources(protocol.ExitStatus{Kind: 1, Value: 17}), nil
	}), s)
	for i := 1; i <= 16; i++ {
		create(t, m, protocol.RequestID(i))
		s.await(t, i)
	}
	errorCode(t, m.Admit(frame(t, protocol.CreateSession{Rows: 24, Cols: 80, ReceiveWindow: 262144}, 17, 0)), protocol.ErrorLimit)
	shutdown(t, m)
}

// Catches fabricated Closed/Ack and released ownership on uncertain wait.
func TestStartupUncertainCleanup(t *testing.T) {
	s := newSink()
	uncertain := errors.New("synthetic uncertain wait")
	p := newFixtureProcess()
	p.done <- ProcessResult{Status: protocol.ExitStatus{Kind: 3}, Err: uncertain}
	close(p.done)
	close(p.complete)
	m := NewManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
		return &Resources{Process: p, rollback: func(context.Context) error { return uncertain }}, nil
	}), s)
	create(t, m, 1)
	s.await(t, 1)
	if e := m.Admit(frame(t, protocol.Shutdown{}, 2, 0)); e != nil {
		t.Fatal(e)
	}
	if e := m.Shutdown(context.Background()); !errors.Is(e, uncertain) {
		t.Fatalf("lost uncertainty: %v", e)
	}
	events := s.snapshot()
	if len(events) != 2 {
		t.Fatal("accepted shutdown lost its terminal error response")
	}
	if v, ok := events[1].Message.(protocol.ErrorMessage); !ok || v.Code != protocol.ErrorStatusUnavailable || events[1].Request != 2 {
		t.Fatal("uncertain cleanup fabricated success")
	}

	m.mu.Lock()
	defer m.mu.Unlock()
	if len(m.records) != 1 {
		t.Fatal("uncertain ownership discarded")
	}
}

// Catches a stuck open being acknowledged as clean shutdown. The late resource
// must still be adopted and rolled back after the watchdog reports failure.
func TestStartingWatchdogLateReturn(t *testing.T) {
	entered, release, rolled := make(chan struct{}), make(chan struct{}), make(chan struct{})
	var once sync.Once
	p := policy.Default()
	p.CleanupTimeout = 10 * time.Millisecond
	s := newSink()
	m := NewManager(p, spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
		close(entered)
		<-release
		return &Resources{rollback: func(context.Context) error { once.Do(func() { close(rolled) }); return nil }}, nil
	}), s)
	create(t, m, 1)
	waitGate(t, entered)
	if e := m.Admit(frame(t, protocol.Shutdown{}, 2, 0)); e != nil {
		t.Fatal(e)
	}
	if e := m.Shutdown(context.Background()); !errors.Is(e, context.DeadlineExceeded) {
		t.Fatalf("stuck start claimed success: %v", e)
	}
	events := s.snapshot()
	if len(events) != 1 {
		t.Fatal("stuck start shutdown must complete with error")
	}
	if v, ok := events[0].Message.(protocol.ErrorMessage); !ok || v.Code != protocol.ErrorStatusUnavailable {
		t.Fatal("Ack preceded stuck start completion")
	}

	close(release)
	waitGate(t, rolled)
	e := s.await(t, 2)
	v, ok := e[1].Message.(protocol.ErrorMessage)
	if !ok || v.Code != protocol.ErrorState {
		t.Fatal("late startup cancellation completion missing")
	}
}

// Catches a Close deadline permanently freezing cleanup and preventing shutdown
// from retrying the still-owned child through Resources.Rollback.
func TestStartupCleanupRetryAfterCloseDeadline(t *testing.T) {
	s := newSink()
	p := newFixtureProcess()
	var mu sync.Mutex
	calls := 0
	first := make(chan struct{})
	m := NewManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
		return &Resources{Process: p, rollback: func(context.Context) error {
			mu.Lock()
			defer mu.Unlock()
			calls++
			if calls == 1 {
				close(first)
				return context.DeadlineExceeded
			}
			p.RequestHangup()
			return nil
		}}, nil
	}), s)
	create(t, m, 1)
	id := s.await(t, 1)[0].Session
	if e := m.Admit(frame(t, protocol.CloseSession{}, 2, id)); e != nil {
		t.Fatal(e)
	}
	waitGate(t, first)
	s.await(t, 2)
	if e := m.Admit(frame(t, protocol.Shutdown{}, 3, 0)); e != nil {
		t.Fatal(e)
	}
	shutdown(t, m)
	e := s.snapshot()
	if len(e) != 5 {
		t.Fatalf("retry lost lifecycle: %+v", e)
	}
	if e[3].Request != 0 {
		t.Fatal("Close received a second terminal response after its error")
	}
}
