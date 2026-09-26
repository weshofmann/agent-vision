package session

import (
	"context"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"io"
	"sync/atomic"
	"syscall"
	"testing"
	"time"
)

type outputFixture struct {
	read func([]byte) (int, error)
	wait func(context.Context) error
}

func (f outputFixture) Read(_ int, b []byte) (int, error) { return f.read(b) }
func (f outputFixture) Wait(ctx context.Context, _ int) error {
	if f.wait != nil {
		return f.wait(ctx)
	}
	<-ctx.Done()
	return ctx.Err()
}
func idleOutput() outputIO {
	return outputFixture{read: func([]byte) (int, error) { return 0, syscall.EAGAIN }}
}
func newFixtureManager(p policy.Policy, sp Spawner, s Sink) *Manager {
	m := NewManager(p, sp, s)
	m.outputIO = idleOutput()
	return m
}
func outputManager(t *testing.T, rd outputIO, p policy.Policy) (*Manager, *recordingSink, *fixtureProcess, protocol.SessionID) {
	t.Helper()
	s := newSink()
	pr := newFixtureProcess()
	m := NewManager(p, spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) { return fixtureResources(pr), nil }), s)
	m.outputIO = rd
	create(t, m, 1)
	id := s.await(t, 1)[0].Session
	t.Cleanup(func() { shutdown(t, m) })
	return m, s, pr, id
}
func awaitExit(t *testing.T, s *recordingSink) protocol.SessionExited {
	t.Helper()
	deadline := time.After(time.Second)
	for {
		for _, e := range s.snapshot() {
			if v, ok := e.Message.(protocol.SessionExited); ok {
				return v
			}
		}
		select {
		case <-s.wake:
		case <-deadline:
			t.Fatal("seal watchdog")
		}
	}
}

// Holding a successful syscall result across wait must not let Exited overtake it.
func TestReadHeldAcrossExitSeal(t *testing.T) {
	held, release := make(chan struct{}), make(chan struct{})
	reads := 0
	rd := outputFixture{read: func(b []byte) (int, error) {
		reads++
		switch reads {
		case 1:
			return copy(b, "first"), nil
		case 2:
			close(held)
			<-release
			return copy(b, "tail"), nil
		default:
			return 0, syscall.EAGAIN
		}
	}}
	m, s, pr, id := outputManager(t, rd, policy.Default())
	waitGate(t, held)
	pr.finish(protocol.ExitStatus{Kind: 1, Value: 17})
	m.mu.Lock()
	r := m.records[id]
	m.mu.Unlock()
	waitGate(t, r.observed)
	for _, e := range s.snapshot() {
		if _, ok := e.Message.(protocol.SessionExited); ok {
			t.Fatal("exit overtook held result")
		}
	}
	close(release)
	v := awaitExit(t, s)
	if v.LastOutputSequence != 2 || v.DrainReason != protocol.DrainNoData || v.Status.Value != 17 {
		t.Fatalf("seal %+v", v)
	}
	e := s.snapshot()
	if len(e) != 4 {
		t.Fatalf("events %d", len(e))
	}
	for i, w := range []string{"first", "tail"} {
		o, ok := e[i+1].Message.(protocol.OutputBytes)
		if !ok || string(o.Bytes) != w || o.Sequence != uint64(i+1) || e[i+1].Output == nil {
			t.Fatal("lost owned output")
		}
	}
	m.records[id].ledger.mu.Lock()
	used := m.records[id].ledger.used
	m.records[id].ledger.mu.Unlock()
	if used != 9 {
		t.Fatal("read charged twice")
	}
}
func TestEOFBeforeWait(t *testing.T) {
	calls := 0
	m, s, _, id := outputManager(t, outputFixture{read: func(b []byte) (int, error) {
		calls++
		if calls == 1 {
			return copy(b, "end"), nil
		}
		return 0, io.EOF
	}}, policy.Default())
	v := awaitExit(t, s)
	if v.DrainReason != protocol.DrainEOF || v.Status != (protocol.ExitStatus{Kind: 2, Value: 1}) || v.LastOutputSequence != 1 {
		t.Fatalf("EOF inferred/lost typed status %+v", v)
	}
	m.mu.Lock()
	record := m.records[id]
	m.mu.Unlock()
	if record == nil {
		t.Fatal("EOF removed exited registry before explicit Close")
	}
	ticket := s.snapshot()[1].Output
	if err := ticket.BeginWrite(); err != nil {
		t.Fatal(err)
	}
	if _, err := ticket.CommitWrite(); err != nil {
		t.Fatal(err)
	}
	admitCommand(t, m, protocol.OutputCredit{RawBytes: 3}, 2, id)
	assertAck(t, completion(t, s, 2), protocol.TypeOutputCredit)
	admitCommand(t, m, protocol.CloseSession{}, 3, id)
	completion(t, s, 3)
	errorCode(t, m.Admit(frame(t, protocol.OutputCredit{RawBytes: 1}, 4, id)), protocol.ErrorUnknownSession)
}
func TestWaitBeforeTail(t *testing.T) {
	entered, release := make(chan struct{}), make(chan struct{})
	reads := 0
	rd := outputFixture{read: func(b []byte) (int, error) {
		reads++
		if reads == 1 {
			close(entered)
			<-release
			return copy(b, "tail"), nil
		}
		return 0, io.EOF
	}}
	m, s, pr, id := outputManager(t, rd, policy.Default())
	waitGate(t, entered)
	pr.finish(protocol.ExitStatus{Kind: 1, Value: 3})
	m.mu.Lock()
	r := m.records[id]
	m.mu.Unlock()
	waitGate(t, r.observed)
	close(release)
	v := awaitExit(t, s)
	if v.LastOutputSequence != 1 || v.DrainReason != protocol.DrainEOF {
		t.Fatalf("tail %+v", v)
	}
}
func TestHeldContinuousSlaveDrain(t *testing.T) {
	held, release := make(chan struct{}), make(chan struct{})
	reads := 0
	p := policy.Default()
	p.DrainBytes = 40000
	rd := outputFixture{read: func(b []byte) (int, error) {
		reads++
		if reads == 1 {
			close(held)
			<-release
		}
		for i := range b {
			b[i] = 'x'
		}
		return len(b), nil
	}}
	m, s, pr, id := outputManager(t, rd, p)
	waitGate(t, held)
	pr.finish(protocol.ExitStatus{Kind: 1})
	m.mu.Lock()
	r := m.records[id]
	m.mu.Unlock()
	waitGate(t, r.observed)
	close(release)
	v := awaitExit(t, s)
	if v.DrainReason != protocol.DrainByteCap || v.LastOutputSequence != 2 {
		t.Fatalf("continuous cap %+v", v)
	}
	total := 0
	for _, e := range s.snapshot() {
		if o, ok := e.Message.(protocol.OutputBytes); ok {
			total += len(o.Bytes)
		}
	}
	if total != 40000 {
		t.Fatalf("bounded admitted total %d", total)
	}
}

// Different drain stops must remain visible and must retain every observed byte.
func TestOutputDrainReasons(t *testing.T) {
	for _, tc := range []struct {
		name    string
		reason  protocol.DrainReason
		err     error
		bytes   int
		timeCap bool
	}{{"no-data", protocol.DrainNoData, syscall.EAGAIN, 0, false}, {"io-error", protocol.DrainIOError, syscall.EBADF, 0, false}, {"credit", protocol.DrainCreditCap, nil, 262144, false}, {"time", protocol.DrainTimeCap, nil, 1, true}} {
		t.Run(tc.name, func(t *testing.T) {
			entered, release := make(chan struct{}), make(chan struct{})
			calls := 0
			p := policy.Default()
			p.DrainBytes = 1048576
			if tc.timeCap {
				p.DrainTime = time.Nanosecond
			}
			rd := outputFixture{read: func(b []byte) (int, error) {
				calls++
				if calls == 1 {
					close(entered)
					<-release
				}
				if tc.bytes == 0 {
					return 0, tc.err
				}
				for i := range b {
					b[i] = 'x'
				}
				return len(b), nil
			}}
			m, s, pr, id := outputManager(t, rd, p)
			waitGate(t, entered)
			pr.finish(protocol.ExitStatus{Kind: 1})
			m.mu.Lock()
			r := m.records[id]
			m.mu.Unlock()
			waitGate(t, r.observed)
			close(release)
			v := awaitExit(t, s)
			if v.DrainReason != tc.reason {
				t.Fatalf("reason %+v", v)
			}
			if tc.bytes != 0 && v.LastOutputSequence == 0 {
				t.Fatal("discarded in-flight read")
			}
		})
	}
}
func TestOutputExplicitCloseHeldRead(t *testing.T) {
	entered, release := make(chan struct{}), make(chan struct{})
	rd := outputFixture{read: func(b []byte) (int, error) { close(entered); <-release; return copy(b, "owned"), nil }}
	m, s, _, id := outputManager(t, rd, policy.Default())
	waitGate(t, entered)
	admitCommand(t, m, protocol.CloseSession{}, 2, id)
	close(release)
	completion(t, s, 2)
	v := awaitExit(t, s)
	if v.LastOutputSequence != 1 || v.DrainReason != protocol.DrainExplicitClose {
		t.Fatalf("explicit seal %+v", v)
	}
	n := 0
	for _, e := range s.snapshot() {
		if _, ok := e.Message.(protocol.SessionExited); ok {
			n++
		}
	}
	if n != 1 {
		t.Fatal("Exited emitted twice")
	}
}
func TestOutputCreditTimeoutAbortsContact(t *testing.T) {
	p := policy.Default()
	p.CreditTimeout = 20 * time.Millisecond
	calls := 0
	rd := outputFixture{read: func(b []byte) (int, error) {
		calls++
		if calls == 1 {
			return copy(b, "unconsumed"), nil
		}
		return 0, syscall.EAGAIN
	}}
	m, s, _, _ := outputManager(t, rd, p)
	waitGate(t, m.Done())
	for _, e := range s.snapshot() {
		switch e.Message.(type) {
		case protocol.SessionExited, protocol.SessionClosed:
			t.Fatal("stalled contact fabricated terminal delivery")
		}
	}
}

// Credit progresses on the decoder while all ordinary operations wait on PTY.
func TestCreditProgressWith48Ordinary(t *testing.T) {
	readGate := make(chan struct{})
	calls := 0
	rd := outputFixture{read: func(b []byte) (int, error) {
		calls++
		if calls == 1 {
			return copy(b, "credit"), nil
		}
		return 0, syscall.EAGAIN
	}, wait: func(ctx context.Context) error {
		select {
		case <-readGate:
			return nil
		case <-ctx.Done():
			return ctx.Err()
		}
	}}
	m, s, _, id := outputManager(t, rd, policy.Default())
	m.commandIO = commandFixture{write: func(int, []byte) (int, error) { return 0, syscall.EAGAIN }}
	s.await(t, 2)
	for i := 0; i < 48; i++ {
		admitCommand(t, m, protocol.InputBytes{Bytes: []byte{1}}, protocol.RequestID(i+10), id)
	}
	ticket := s.snapshot()[1].Output
	ticket.BeginWrite()
	ticket.CommitWrite()
	admitCommand(t, m, protocol.OutputCredit{RawBytes: 6}, 100, id)
	assertAck(t, completion(t, s, 100), protocol.TypeOutputCredit)
	admitCommand(t, m, protocol.CloseSession{}, 101, id)
	completion(t, s, 101)
}
func TestCreditClosingAndExitedRegistry(t *testing.T) {
	for _, closingNow := range []bool{false, true} {
		t.Run(map[bool]string{false: "Exited", true: "Closing"}[closingNow], func(t *testing.T) {
			held, release := make(chan struct{}), make(chan struct{})
			calls := 0
			rd := outputFixture{read: func(b []byte) (int, error) {
				calls++
				if calls == 1 {
					return copy(b, "x"), nil
				}
				if calls == 2 {
					close(held)
					<-release
				}
				return 0, syscall.EAGAIN
			}}
			m, s, pr, id := outputManager(t, rd, policy.Default())
			waitGate(t, held)
			ticket := s.snapshot()[1].Output
			ticket.BeginWrite()
			ticket.CommitWrite()
			if closingNow {
				admitCommand(t, m, protocol.CloseSession{}, 3, id)
			} else {
				pr.finish(protocol.ExitStatus{Kind: 1})
				m.mu.Lock()
				r := m.records[id]
				m.mu.Unlock()
				waitGate(t, r.observed)
				close(release)
				awaitExit(t, s)
			}
			admitCommand(t, m, protocol.OutputCredit{RawBytes: 1}, 2, id)
			assertAck(t, completion(t, s, 2), protocol.TypeOutputCredit)
			if closingNow {
				close(release)
			} else {
				admitCommand(t, m, protocol.CloseSession{}, 3, id)
			}
			completion(t, s, 3)
			errorCode(t, m.Admit(frame(t, protocol.OutputCredit{RawBytes: 1}, 4, id)), protocol.ErrorUnknownSession)
		})
	}
}
func TestOutputReadinessFailureCloses(t *testing.T) {
	m, s, _, id := outputManager(t, outputFixture{read: func([]byte) (int, error) { return 0, syscall.EAGAIN }, wait: func(context.Context) error { return syscall.EBADF }}, policy.Default())
	events := s.await(t, 4)
	if v, ok := events[1].Message.(protocol.ErrorMessage); !ok || v.Code != protocol.ErrorPTYIO {
		t.Fatal("readiness failure lost explicit PTY error")
	}
	v, ok := events[2].Message.(protocol.SessionExited)
	if !ok || v.DrainReason != protocol.DrainIOError || v.Status != (protocol.ExitStatus{Kind: 2, Value: 1}) {
		t.Fatalf("failure seal %+v", events)
	}
	if _, ok := events[3].Message.(protocol.SessionClosed); !ok {
		t.Fatal("PTY failure did not close")
	}
	errorCode(t, m.Admit(frame(t, protocol.CloseSession{}, 2, id)), protocol.ErrorUnknownSession)
}

func TestOutputRetiredMonitorJoin(t *testing.T) {
	calls := 0
	rd := outputFixture{read: func(b []byte) (int, error) {
		calls++
		if calls == 1 {
			return copy(b, "x"), nil
		}
		return 0, syscall.EAGAIN
	}}
	m, s, _, id := outputManager(t, rd, policy.Default())
	s.await(t, 2)
	m.mu.Lock()
	r := m.records[id]
	m.mu.Unlock()
	ticket := s.snapshot()[1].Output
	admitCommand(t, m, protocol.CloseSession{}, 2, id)
	completion(t, s, 2)
	select {
	case <-r.monitorDone:
		t.Fatal("retired queued output lost stall supervision")
	default:
	}
	ticket.BeginWrite()
	ticket.CommitWrite()
	waitGate(t, r.monitorDone)
	shutdown(t, m)
	m.outputBudget.mu.Lock()
	bytes, chunks := m.outputBudget.bytes, m.outputBudget.chunks
	m.outputBudget.mu.Unlock()
	if bytes != 0 || chunks != 0 {
		t.Fatal("retired delivery retained allocation")
	}
}
func TestOutputAbortJoinsMonitorAndReader(t *testing.T) {
	calls := 0
	rd := outputFixture{read: func(b []byte) (int, error) {
		calls++
		if calls == 1 {
			return copy(b, "x"), nil
		}
		return 0, syscall.EAGAIN
	}}
	m, s, _, id := outputManager(t, rd, policy.Default())
	s.await(t, 2)
	m.mu.Lock()
	r := m.records[id]
	m.mu.Unlock()
	m.Abort(io.ErrUnexpectedEOF)
	waitGate(t, m.Done())
	select {
	case <-r.monitorDone:
	default:
		t.Fatal("Done preceded monitor join")
	}
	select {
	case <-r.sealed:
	default:
		t.Fatal("Done preceded reader seal")
	}
	select {
	case <-r.naturalDone:
	default:
		t.Fatal("Done preceded natural cleanup join")
	}
}

// A successful syscall begun before wait owns its complete result even beyond a
// very small post-wait byte policy. There is no second read after that cap.
func TestReadInFlightCrossesDrainCap(t *testing.T) {
	held, release := make(chan struct{}), make(chan struct{})
	p := policy.Default()
	p.DrainBytes = 1
	rd := outputFixture{read: func(b []byte) (int, error) {
		close(held)
		<-release
		for i := range b {
			b[i] = 'x'
		}
		return len(b), nil
	}}
	m, s, pr, id := outputManager(t, rd, p)
	waitGate(t, held)
	pr.finish(protocol.ExitStatus{Kind: 1})
	m.mu.Lock()
	r := m.records[id]
	m.mu.Unlock()
	waitGate(t, r.observed)
	close(release)
	v := awaitExit(t, s)
	if v.LastOutputSequence != 1 || v.DrainReason != protocol.DrainByteCap {
		t.Fatalf("in-flight cap %+v", v)
	}
	if len(s.snapshot()[1].Message.(protocol.OutputBytes).Bytes) != 32768 {
		t.Fatal("in-flight data truncated")
	}
}

// A fake monotonic clock proves the exact time boundary without sleeps or a
// wall-clock scheduling assumption; the held read remains owned across wait.
func TestOutputDrainFakeClock(t *testing.T) {
	var ns atomic.Int64
	ns.Store(1)
	held, release := make(chan struct{}), make(chan struct{})
	calls := 0
	rd := outputFixture{read: func(b []byte) (int, error) {
		calls++
		if calls == 1 {
			close(held)
			<-release
			ns.Add(int64(99 * time.Millisecond))
		} else {
			ns.Add(int64(time.Millisecond))
		}
		return copy(b, "x"), nil
	}}
	s := newSink()
	pr := newFixtureProcess()
	m := NewManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) { return fixtureResources(pr), nil }), s)
	m.now = func() time.Time { return time.Unix(1000, ns.Load()) }
	m.outputIO = rd
	t.Cleanup(func() { shutdown(t, m) })
	create(t, m, 1)
	id := s.await(t, 1)[0].Session
	waitGate(t, held)
	pr.finish(protocol.ExitStatus{Kind: 1})
	m.mu.Lock()
	r := m.records[id]
	m.mu.Unlock()
	waitGate(t, r.observed)
	close(release)
	v := awaitExit(t, s)
	if v.LastOutputSequence != 2 || v.DrainReason != protocol.DrainTimeCap {
		t.Fatalf("fake-clock seal %+v", v)
	}
}

// The count represents already reserved live/retired monitor owners, including
// those whose last payload finished but whose goroutine has not joined yet.
func TestOutputMonitorAdmissionBound(t *testing.T) {
	m := newFixtureManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
		return fixtureResources(newFixtureProcess()), nil
	}), newSink())
	m.monitorCount = 2064
	errorCode(t, m.Admit(frame(t, protocol.CreateSession{Rows: 24, Cols: 80, ReceiveWindow: 262144}, 1, 0)), protocol.ErrorLimit)
	m.monitorCount = 0
	shutdown(t, m)
}
