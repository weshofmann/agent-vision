package session

import (
	"bytes"
	"context"
	"errors"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"sync"
	"syscall"
	"testing"
	"time"
)

type commandFixture struct {
	write  func(int, []byte) (int, error)
	resize func(int, uint16, uint16) error
	wait   func(context.Context, int) error
}

func (f commandFixture) Write(fd int, b []byte) (int, error) { return f.write(fd, b) }
func (f commandFixture) Resize(fd int, r, c uint16) error {
	if f.resize != nil {
		return f.resize(fd, r, c)
	}
	return nil
}
func (f commandFixture) Wait(ctx context.Context, fd int) error {
	if f.wait != nil {
		return f.wait(ctx, fd)
	}
	<-ctx.Done()
	return ctx.Err()
}
func commandManager(t *testing.T, io commandIO, p policy.Policy) (*Manager, *recordingSink, protocol.SessionID) {
	t.Helper()
	s := newSink()
	m := NewManager(p, spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
		return fixtureResources(newFixtureProcess()), nil
	}), s)
	m.commandIO = io
	create(t, m, 1)
	id := s.await(t, 1)[0].Session
	t.Cleanup(func() { shutdown(t, m) })
	return m, s, id
}
func admitCommand(t *testing.T, m *Manager, msg protocol.Message, req protocol.RequestID, id protocol.SessionID) {
	t.Helper()
	if e := m.Admit(frame(t, msg, req, id)); e != nil {
		t.Fatalf("command %d rejected: %v", req, e)
	}
}
func completion(t *testing.T, s *recordingSink, req protocol.RequestID) Event {
	t.Helper()
	deadline := time.After(time.Second)
	for {
		for _, e := range s.snapshot() {
			if e.Request == req {
				return e
			}
		}
		select {
		case <-s.wake:
		case <-deadline:
			t.Fatalf("request %d completion watchdog", req)
		}
	}
}
func assertAck(t *testing.T, e Event, typ protocol.MessageType) {
	t.Helper()
	v, ok := e.Message.(protocol.Ack)
	if !ok || v.CompletedType != typ {
		t.Fatalf("want Ack(%d), got %+v", typ, e)
	}
}
func assertError(t *testing.T, e Event, code protocol.ErrorCode, n uint32) {
	t.Helper()
	v, ok := e.Message.(protocol.ErrorMessage)
	if !ok || v.Code != code || v.PartialInputBytes != n {
		t.Fatalf("want Error(%d,%d), got %+v", code, n, e)
	}
}
func onceCompletions(t *testing.T, s *recordingSink, reqs ...protocol.RequestID) {
	t.Helper()
	for _, req := range reqs {
		n := 0
		for _, e := range s.snapshot() {
			if e.Request == req {
				n++
			}
		}
		if n != 1 {
			t.Fatalf("request %d has %d completions", req, n)
		}
	}
}

// A resize overtaking unfinished input or retrying the whole prefix fails this.
func TestCommandAdmissionOrder(t *testing.T) {
	entered, release := make(chan struct{}), make(chan struct{})
	var mu sync.Mutex
	var written []byte
	var order []string
	attempts := 0
	io := commandFixture{write: func(_ int, b []byte) (int, error) {
		mu.Lock()
		defer mu.Unlock()
		attempts++
		switch attempts {
		case 1:
			return 0, syscall.EINTR
		case 2:
			written = append(written, b[:2]...)
			return 2, nil
		case 3:
			return 0, syscall.EAGAIN
		}
		written = append(written, b...)
		order = append(order, "input")
		return len(b), nil
	}, wait: func(ctx context.Context, _ int) error {
		close(entered)
		select {
		case <-release:
			return nil
		case <-ctx.Done():
			return ctx.Err()
		}
	}, resize: func(_ int, r, c uint16) error {
		mu.Lock()
		defer mu.Unlock()
		if r != 31 || c != 91 {
			t.Error("resize dimensions changed")
		}
		order = append(order, "resize")
		return nil
	}}
	m, s, id := commandManager(t, io, policy.Default())
	admitCommand(t, m, protocol.InputBytes{Bytes: []byte{'a', 'b', 3, 26, 'z'}}, 2, id)
	waitGate(t, entered)
	admitCommand(t, m, protocol.ResizeSession{Rows: 31, Cols: 91}, 3, id)
	mu.Lock()
	if len(order) != 0 {
		t.Fatal("resize overtook blocked input")
	}
	mu.Unlock()
	close(release)
	assertAck(t, completion(t, s, 2), protocol.TypeInputBytes)
	assertAck(t, completion(t, s, 3), protocol.TypeResizeSession)
	mu.Lock()
	defer mu.Unlock()
	if !bytes.Equal(written, []byte{'a', 'b', 3, 26, 'z'}) || len(order) != 2 || order[0] != "input" || order[1] != "resize" {
		t.Fatalf("wrong bytes/order %v %v", written, order)
	}
	onceCompletions(t, s, 2, 3)
}

// Progress must not restart the first-attempt deadline, and timeout closes A.
func TestPartialInputTimeout(t *testing.T) {
	p := policy.Default()
	p.InputTimeout = 40 * time.Millisecond
	attempts := 0
	var started time.Time
	io := commandFixture{write: func(_ int, b []byte) (int, error) {
		attempts++
		if attempts == 1 {
			started = time.Now()
			return 2, nil
		}
		return 0, syscall.EAGAIN
	}, wait: func(ctx context.Context, _ int) error {
		select {
		case <-time.After(8 * time.Millisecond):
			return nil
		case <-ctx.Done():
			return ctx.Err()
		}
	}}
	m, s, id := commandManager(t, io, p)
	admitCommand(t, m, protocol.InputBytes{Bytes: []byte("abcdef")}, 2, id)
	admitCommand(t, m, protocol.ResizeSession{Rows: 31, Cols: 91}, 3, id)
	assertError(t, completion(t, s, 2), protocol.ErrorInputTimeout, 2)
	assertError(t, completion(t, s, 3), protocol.ErrorState, 0)
	s.await(t, 5)
	if time.Since(started) > 250*time.Millisecond {
		t.Fatal("deadline extended by readiness")
	}
	onceCompletions(t, s, 2, 3)
}

// Close bypasses Input, finishes accepted commands, and joins before rollback.
func TestCloseBypassesBlockedInput(t *testing.T)           { testCommandCancel(t, false, false) }
func TestCommandShutdownBypassesBlockedInput(t *testing.T) { testCommandCancel(t, true, false) }
func TestCommandAbortSuppressesCompletion(t *testing.T)    { testCommandCancel(t, true, true) }
func testCommandCancel(t *testing.T, stop, abort bool) {
	entered, cancelled, release, rolled := make(chan struct{}), make(chan struct{}), make(chan struct{}), make(chan struct{})
	var once sync.Once
	io := commandFixture{write: func(int, []byte) (int, error) { return 0, syscall.EAGAIN }, wait: func(ctx context.Context, _ int) error {
		close(entered)
		<-ctx.Done()
		close(cancelled)
		<-release
		return ctx.Err()
	}}
	s := newSink()
	pr := newFixtureProcess()
	res := fixtureResources(pr)
	rollback := res.rollback
	res.rollback = func(ctx context.Context) error { once.Do(func() { close(rolled) }); return rollback(ctx) }
	m := NewManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) { return res, nil }), s)
	m.commandIO = io
	create(t, m, 1)
	id := s.await(t, 1)[0].Session
	admitCommand(t, m, protocol.InputBytes{Bytes: []byte("input")}, 2, id)
	waitGate(t, entered)
	admitCommand(t, m, protocol.ResizeSession{Rows: 31, Cols: 91}, 3, id)
	if abort {
		m.Abort(errors.New("synthetic loss"))
	} else if stop {
		admitCommand(t, m, protocol.Shutdown{}, 4, 0)
	} else {
		admitCommand(t, m, protocol.CloseSession{}, 4, id)
	}
	waitGate(t, cancelled)
	select {
	case <-rolled:
		t.Fatal("rollback preceded worker join")
	default:
	}
	close(release)
	waitGate(t, rolled)
	shutdown(t, m)
	if abort {
		if len(s.snapshot()) != 1 {
			t.Fatal("contact loss emitted undeliverable responses")
		}
		return
	}
	assertError(t, completion(t, s, 2), protocol.ErrorState, 0)
	assertError(t, completion(t, s, 3), protocol.ErrorState, 0)
	completion(t, s, 4)
	onceCompletions(t, s, 2, 3, 4)
}

// A blocked worker must not serialize B's independent worker or admission.
func TestTwoSessionCommands(t *testing.T) {
	entered := make(chan struct{})
	var once sync.Once
	io := commandFixture{write: func(fd int, b []byte) (int, error) {
		if fd == 11 {
			return 0, syscall.EAGAIN
		}
		return len(b), nil
	}, wait: func(ctx context.Context, _ int) error {
		once.Do(func() { close(entered) })
		<-ctx.Done()
		return ctx.Err()
	}}
	s := newSink()
	next := 10
	m := NewManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
		next++
		r := fixtureResources(newFixtureProcess())
		r.MasterFD = next
		return r, nil
	}), s)
	m.commandIO = io
	t.Cleanup(func() { shutdown(t, m) })
	create(t, m, 1)
	a := s.await(t, 1)[0].Session
	admitCommand(t, m, protocol.InputBytes{Bytes: []byte("blocked")}, 3, a)
	waitGate(t, entered)
	create(t, m, 2)
	b := completion(t, s, 2).Session
	admitCommand(t, m, protocol.InputBytes{Bytes: []byte("works")}, 4, b)
	assertAck(t, completion(t, s, 4), protocol.TypeInputBytes)
	admitCommand(t, m, protocol.CloseSession{}, 5, a)
	completion(t, s, 5)
}

// Byte bounds include in-flight bytes, while tiny resizes hit metadata bounds.
func TestCommandAdmissionBounds(t *testing.T) {
	entered := make(chan struct{})
	var once sync.Once
	io := commandFixture{write: func(int, []byte) (int, error) { return 0, syscall.EAGAIN }, wait: func(ctx context.Context, _ int) error {
		once.Do(func() { close(entered) })
		<-ctx.Done()
		return ctx.Err()
	}}
	m, s, id := commandManager(t, io, policy.Default())
	admitCommand(t, m, protocol.InputBytes{Bytes: make([]byte, 32768)}, 2, id)
	waitGate(t, entered)
	admitCommand(t, m, protocol.InputBytes{Bytes: make([]byte, 32768)}, 3, id)
	errorCode(t, m.Admit(frame(t, protocol.InputBytes{Bytes: []byte{1}}, 4, id)), protocol.ErrorLimit)
	for i := 0; i < policy.OrdinarySlots-2; i++ {
		admitCommand(t, m, protocol.ResizeSession{Rows: 1, Cols: 1}, protocol.RequestID(i+10), id)
	}
	errorCode(t, m.Admit(frame(t, protocol.ResizeSession{Rows: 1, Cols: 1}, 100, id)), protocol.ErrorLimit)
	admitCommand(t, m, protocol.CloseSession{}, 101, id)
	completion(t, s, 101)
}
func TestCommandResizeErrorContinues(t *testing.T) {
	io := commandFixture{write: func(_ int, b []byte) (int, error) { return len(b), nil }, resize: func(int, uint16, uint16) error { return syscall.EIO }}
	m, s, id := commandManager(t, io, policy.Default())
	admitCommand(t, m, protocol.ResizeSession{Rows: 31, Cols: 91}, 2, id)
	assertError(t, completion(t, s, 2), protocol.ErrorResize, 0)
	admitCommand(t, m, protocol.InputBytes{Bytes: []byte("after")}, 3, id)
	assertAck(t, completion(t, s, 3), protocol.TypeInputBytes)
}

// Natural exit cancels commands while leaving lifetime context for Task5 drain.
func TestCommandNaturalExitCancelsQueued(t *testing.T) {
	entered := make(chan struct{})
	var once sync.Once
	io := commandFixture{write: func(int, []byte) (int, error) { return 0, syscall.EAGAIN }, wait: func(ctx context.Context, _ int) error {
		once.Do(func() { close(entered) })
		<-ctx.Done()
		return ctx.Err()
	}}
	s := newSink()
	pr := newFixtureProcess()
	m := NewManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) { return fixtureResources(pr), nil }), s)
	m.commandIO = io
	t.Cleanup(func() { shutdown(t, m) })
	create(t, m, 1)
	id := s.await(t, 1)[0].Session
	admitCommand(t, m, protocol.InputBytes{Bytes: []byte("pending")}, 2, id)
	waitGate(t, entered)
	admitCommand(t, m, protocol.ResizeSession{Rows: 31, Cols: 91}, 3, id)
	pr.finish(protocol.ExitStatus{Kind: protocol.ExitNormal, Value: 17})
	assertError(t, completion(t, s, 2), protocol.ErrorState, 0)
	assertError(t, completion(t, s, 3), protocol.ErrorState, 0)
	m.mu.Lock()
	r := m.records[id]
	if r == nil || r.state != draining || r.ctx.Err() != nil {
		t.Error("command cancellation destroyed future output drain")
	}
	m.mu.Unlock()
	onceCompletions(t, s, 2, 3)
}

// Admission latency must not consume the next Input's own first-attempt budget.
func TestCommandDeadlineStartsAtAttempt(t *testing.T) {
	p := policy.Default()
	p.InputTimeout = 120 * time.Millisecond
	entered, release := make(chan struct{}), make(chan struct{})
	calls := 0
	io := commandFixture{write: func(_ int, b []byte) (int, error) {
		calls++
		switch calls {
		case 1:
			close(entered)
			return 0, syscall.EAGAIN
		case 3:
			return 0, syscall.EAGAIN
		}
		return len(b), nil
	}, wait: func(ctx context.Context, _ int) error {
		if calls == 1 {
			select {
			case <-release:
				return nil
			case <-ctx.Done():
				return ctx.Err()
			}
		}
		select {
		case <-time.After(70 * time.Millisecond):
			return nil
		case <-ctx.Done():
			return ctx.Err()
		}
	}}
	m, s, id := commandManager(t, io, p)
	admitCommand(t, m, protocol.InputBytes{Bytes: []byte("first")}, 2, id)
	waitGate(t, entered)
	admitCommand(t, m, protocol.InputBytes{Bytes: []byte("second")}, 3, id)
	// This delay puts second completion beyond its admission-time deadline while
	// each individual attempt still uses less than the configured budget.
	time.Sleep(70 * time.Millisecond)
	close(release)
	assertAck(t, completion(t, s, 2), protocol.TypeInputBytes)
	assertAck(t, completion(t, s, 3), protocol.TypeInputBytes)
}
func TestCommandPTYErrorExactPrefixCloses(t *testing.T) {
	calls := 0
	io := commandFixture{write: func(_ int, b []byte) (int, error) {
		calls++
		if calls == 1 {
			return 2, nil
		}
		return 0, syscall.EIO
	}}
	m, s, id := commandManager(t, io, policy.Default())
	admitCommand(t, m, protocol.InputBytes{Bytes: []byte("abcdef")}, 2, id)
	assertError(t, completion(t, s, 2), protocol.ErrorPTYIO, 2)
	s.await(t, 4)
	onceCompletions(t, s, 2)
}
