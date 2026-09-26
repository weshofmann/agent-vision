package server

import (
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"github.com/weshofmann/agent-vision/core/internal/session"
	"net"
	"strings"
	"sync"
	"syscall"
	"testing"
	"time"
	"unsafe"
)

// Hold the RawConn outer return after the callback's final successful syscall.
// Socket data is real; this only controls an otherwise unforceable interleaving.
type outerGate struct {
	syscall.RawConn
	mu               sync.Mutex
	call             int
	target           int
	entered, release chan struct{}
}

func (g *outerGate) Write(f func(uintptr) bool) error {
	err := g.RawConn.Write(f)
	g.mu.Lock()
	g.call++
	hold := g.call == g.target
	g.mu.Unlock()
	if hold {
		close(g.entered)
		<-g.release
	}
	return err
}

type rawGateConn struct {
	net.Conn
	raw syscall.RawConn
}

func (c rawGateConn) SyscallConn() (syscall.RawConn, error) { return c.raw, nil }
func TestAtomicCorrelationAfterPeerReceipt(t *testing.T) {
	a, b := unixPair(t)
	defer a.Close()
	defer b.Close()
	raw, e := a.(syscall.Conn).SyscallConn()
	if e != nil {
		t.Fatal(e)
	}
	gate := &outerGate{RawConn: raw, target: 2, entered: make(chan struct{}), release: make(chan struct{})}
	x := &connection{conn: rawGateConn{a, gate}, policy: policy.Default()}
	f, _ := protocol.Encode(protocol.OutputCredit{RawBytes: 1}, 1, 7, 1)
	if code := x.reserve(f); code != 0 {
		t.Fatal(code)
	}
	ack, _ := protocol.Encode(protocol.Ack{CompletedType: protocol.TypeOutputCredit}, 1, 7, 1)
	done := make(chan error, 1)
	go func() { done <- x.writeFrame(ack) }()
	got := receive(t, b)
	if got.Header.Request != 1 {
		t.Fatal("wrong response")
	}
	select {
	case <-gate.entered:
	case <-done:
		t.Fatal("writer bypassed atomic RawConn completion boundary")
	}
	successor, _ := protocol.Encode(protocol.OutputCredit{RawBytes: 1}, 2, 7, 1)
	if code := x.reserve(successor); code != 0 {
		t.Fatalf("peer-received completion leaves stale lane: %d", code)
	}
	close(gate.release)
	if err := <-done; err != nil {
		t.Fatal(err)
	}
}
func Test81Lanes(t *testing.T) {
	x := &connection{}
	req := protocol.RequestID(1)
	reserve := func(typ uint16, id protocol.SessionID, want protocol.ErrorCode) {
		t.Helper()
		f := protocol.Frame{Header: protocol.Header{Type: typ, Request: req, Session: id}}
		req++
		if code := x.reserve(f); code != want {
			t.Fatalf("type%d code%d want%d", typ, code, want)
		}
	}
	for i := 0; i < 48; i++ {
		reserve(protocol.TypeInputBytes, 1, 0)
	}
	reserve(protocol.TypeInputBytes, 1, protocol.ErrorLimit)
	for i := 1; i <= 16; i++ {
		reserve(protocol.TypeOutputCredit, protocol.SessionID(i), 0)
		reserve(protocol.TypeCloseSession, protocol.SessionID(i), 0)
	}
	reserve(protocol.TypeOutputCredit, 1, protocol.ErrorState)
	reserve(protocol.TypeCloseSession, 1, protocol.ErrorState)
	reserve(protocol.TypeShutdown, 0, 0)
	reserve(protocol.TypeShutdown, 0, protocol.ErrorLimit)
	count := 0
	for _, a := range x.accepted {
		if a.request != 0 {
			count++
		}
	}
	if count != 81 {
		t.Fatalf("accepted %d want81", count)
	}
}
func TestNativeRawWriteDeadlineAndClose(t *testing.T) {
	for _, closeInstead := range []bool{false, true} {
		t.Run(map[bool]string{false: "deadline", true: "close"}[closeInstead], func(t *testing.T) {
			a, b := unixPair(t)
			defer a.Close()
			defer b.Close()
			raw, e := a.(syscall.Conn).SyscallConn()
			if e != nil {
				t.Fatal(e)
			}
			// Fill only the owned nonblocking endpoint, then let RawConn wait for capacity.
			e = raw.Control(func(fd uintptr) {
				syscall.SetsockoptInt(int(fd), syscall.SOL_SOCKET, syscall.SO_SNDBUF, 1024)
				data := make([]byte, 32768)
				for {
					_, err := syscall.Write(int(fd), data)
					if err == syscall.EAGAIN || err == syscall.EWOULDBLOCK {
						break
					}
					if err != nil {
						t.Error(err)
						break
					}
				}
			})
			if e != nil {
				t.Fatal(e)
			}
			p := policy.Default()
			p.WriteTimeout = 40 * time.Millisecond
			x := &connection{conn: a, policy: p}
			f, _ := protocol.Encode(protocol.Ack{CompletedType: protocol.TypeShutdown}, 1, 0, 1)
			done := make(chan error, 1)
			go func() { done <- x.writeFrame(f) }()
			if closeInstead {
				time.Sleep(10 * time.Millisecond)
				a.Close()
			}
			select {
			case err := <-done:
				if err == nil {
					t.Fatal("blocked writer succeeded")
				}
			case <-time.After(200 * time.Millisecond):
				t.Fatal("raw writer did not wake")
			}
		})
	}
}

type callbackRaw struct{}

func (callbackRaw) Control(f func(uintptr)) error    { f(0); return nil }
func (callbackRaw) Read(f func(uintptr) bool) error  { f(0); return nil }
func (callbackRaw) Write(f func(uintptr) bool) error { f(0); return nil }

type deadlineConn struct {
	net.Conn
	deadlines []time.Time
}

func (c *deadlineConn) SetWriteDeadline(d time.Time) error {
	c.deadlines = append(c.deadlines, d)
	return nil
}
func TestWriteInterruptDoesNotResetProgressDeadline(t *testing.T) {
	c := &deadlineConn{}
	p := policy.Default()
	p.WriteTimeout = 10 * time.Millisecond
	x := &connection{conn: c, policy: p}
	attempts := 0
	w := socketWriter{connection: x, raw: callbackRaw{}, remaining: 1, attempt: func(int, []byte) (int, error) {
		attempts++
		if attempts < 4 {
			time.Sleep(time.Millisecond)
			return 0, syscall.EINTR
		}
		return 1, nil
	}}
	if _, e := w.Write([]byte{1}); e != nil {
		t.Fatal(e)
	}
	for _, d := range c.deadlines {
		if !d.Equal(c.deadlines[0]) {
			t.Fatal("zero-progress EINTR reset watchdog")
		}
	}
}

type retryRaw struct{ onWait func() }

func (r retryRaw) Control(f func(uintptr)) error   { f(0); return nil }
func (r retryRaw) Read(f func(uintptr) bool) error { f(0); return nil }
func (r retryRaw) Write(f func(uintptr) bool) error {
	for i := 0; i < 100; i++ {
		if f(0) {
			return nil
		}
		if r.onWait != nil {
			r.onWait()
		}
	}
	return syscall.EIO
}
func TestShortInterruptAndReadinessWire(t *testing.T) {
	c := &deadlineConn{}
	x := &connection{conn: c, policy: policy.Default()}
	request := protocol.Frame{Header: protocol.Header{Type: 14, Request: 1, Session: 7}}
	if x.reserve(request) != 0 {
		t.Fatal("reserve")
	}
	var got []byte
	attempts := 0
	waited := false
	raw := retryRaw{onWait: func() {
		waited = true
		if !x.mu.TryLock() {
			t.Fatal("readiness holds admission mutex")
		}
		x.mu.Unlock()
		f := protocol.Frame{Header: protocol.Header{Type: 7, Request: 2, Session: 8}}
		if x.reserve(f) != 0 {
			t.Fatal("Close blocked by readiness")
		}
	}}
	w := socketWriter{connection: x, raw: raw, request: 1, remaining: 34, attempt: func(_ int, b []byte) (int, error) {
		attempts++
		if attempts == 1 {
			return 0, syscall.EINTR
		}
		if attempts == 2 {
			return 0, syscall.EAGAIN
		}
		n := min(3, len(b))
		got = append(got, b[:n]...)
		return n, nil
	}}
	frame, _ := protocol.Encode(protocol.Ack{CompletedType: 14}, 1, 7, 1)
	if e := protocol.WriteFrame(&w, frame); e != nil {
		t.Fatal(e)
	}
	literal := []byte{65, 86, 67, 80, 0, 1, 0, 13, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 7, 0, 14}
	if string(got) != string(literal) || !waited {
		t.Fatal("short/EINTR/EAGAIN changed frame bytes")
	}
	if x.reserve(protocol.Frame{Header: protocol.Header{Type: 14, Request: 3, Session: 7}}) != 0 {
		t.Fatal("complete frame not retired")
	}
}
func TestWriteFailureKeepsCorrelation(t *testing.T) {
	for _, partial := range []bool{false, true} {
		t.Run(map[bool]string{false: "zero", true: "prefix"}[partial], func(t *testing.T) {
			c := &deadlineConn{}
			x := &connection{conn: c, policy: policy.Default()}
			f := protocol.Frame{Header: protocol.Header{Type: 14, Request: 1, Session: 7}}
			x.reserve(f)
			w := socketWriter{connection: x, raw: callbackRaw{}, request: 1, remaining: 34, attempt: func(int, []byte) (int, error) {
				if partial {
					return 3, syscall.EPIPE
				}
				return 0, nil
			}}
			ack, _ := protocol.Encode(protocol.Ack{CompletedType: 14}, 1, 7, 1)
			if e := protocol.WriteFrame(&w, ack); e == nil {
				t.Fatal("failed frame succeeded")
			}
			f.Header.Request = 2
			if x.reserve(f) != protocol.ErrorState {
				t.Fatal("failed frame retired as success")
			}
		})
	}
}
func TestBeforeFirstByteLanesAndReadinessProgress(t *testing.T) {
	a, b := unixPair(t)
	defer a.Close()
	defer b.Close()
	raw, _ := a.(syscall.Conn).SyscallConn()
	raw.Control(func(fd uintptr) {
		syscall.SetsockoptInt(int(fd), syscall.SOL_SOCKET, syscall.SO_SNDBUF, 1024)
		bytes := make([]byte, 32768)
		for {
			_, e := syscall.Write(int(fd), bytes)
			if e == syscall.EAGAIN {
				break
			}
			if e != nil {
				t.Fatal(e)
			}
		}
	})
	p := policy.Default()
	p.WriteTimeout = 50 * time.Millisecond
	x := &connection{conn: a, raw: raw, policy: p}
	for i := 1; i <= 48; i++ {
		if x.reserve(protocol.Frame{Header: protocol.Header{Type: 5, Request: protocol.RequestID(i), Session: 1}}) != 0 {
			t.Fatal("ordinary slots")
		}
	}
	x.reserve(protocol.Frame{Header: protocol.Header{Type: 14, Request: 49, Session: 1}})
	x.reserve(protocol.Frame{Header: protocol.Header{Type: 7, Request: 50, Session: 1}})
	ack, _ := protocol.Encode(protocol.Ack{CompletedType: 5}, 1, 1, 1)
	done := make(chan error, 1)
	go func() { done <- x.writeFrame(ack) }()
	// Real filled socket forces EAGAIN. A nonblocking callback cannot consume its
	// outstanding lane; admissions are synchronous while the writer polls.
	cases := []struct {
		typ  uint16
		id   protocol.SessionID
		want protocol.ErrorCode
	}{{5, 1, 5}, {14, 1, 4}, {7, 1, 4}, {14, 2, 0}, {7, 2, 0}, {8, 0, 0}}
	for i, v := range cases {
		if code := x.reserve(protocol.Frame{Header: protocol.Header{Type: v.typ, Request: protocol.RequestID(51 + i), Session: v.id}}); code != v.want {
			t.Fatalf("blocked writer admission%d: got%d want%d", i, code, v.want)
		}
	}
	if e := <-done; e == nil {
		t.Fatal("blocked terminal frame succeeded")
	}
}
func TestRejectionMetadataBoundAndExactRetirement(t *testing.T) {
	x := &connection{scheduler: NewScheduler()}
	accepted := protocol.Frame{Header: protocol.Header{Type: 14, Request: 1, Session: 7}}
	x.reserve(accepted)
	for i := 2; i < 130; i++ {
		if e := x.reject(protocol.Frame{Header: protocol.Header{Request: protocol.RequestID(i), Session: 7}}, protocol.ErrorState, false); e != nil {
			t.Fatal(e)
		}
	}
	if e := x.reject(protocol.Frame{Header: protocol.Header{Request: 130, Session: 7}}, protocol.ErrorLimit, false); e == nil {
		t.Fatal("unbounded rejected reply metadata")
	}
	retireCorrelation(x, 2)
	accepted.Header.Request = 131
	if x.reserve(accepted) != protocol.ErrorState {
		t.Fatal("rejection completion retired another request")
	}
	if e := x.reject(protocol.Frame{Header: protocol.Header{Request: 132, Session: 7}}, protocol.ErrorState, false); e != nil {
		t.Fatal("delivered rejection did not retire metadata")
	}
}
func TestShutdownFinalAdmissionBarrier(t *testing.T) {
	x := &connection{scheduler: NewScheduler()}
	x.reserve(protocol.Frame{Header: protocol.Header{Type: 8, Request: 1}})
	x.reject(protocol.Frame{Header: protocol.Header{Request: 2, Session: 7}}, protocol.ErrorUnknownSession, false)
	if x.sealShutdown(1) {
		t.Fatal("shutdown silently loses owed rejected reply")
	}
	retireCorrelation(x, 2)
	if !x.sealShutdown(1) {
		t.Fatal("completed responses prevent final boundary")
	}
	if x.reserve(protocol.Frame{Header: protocol.Header{Type: 14, Request: 3, Session: 7}}) == 0 {
		t.Fatal("admission after final Shutdown boundary")
	}
}

func TestFinalAttemptAdmissionAtomicity(t *testing.T) {
	a, b := unixPair(t)
	defer a.Close()
	defer b.Close()
	raw, _ := a.(syscall.Conn).SyscallConn()
	p := policy.Default()
	x := &connection{conn: a, raw: raw, policy: p}
	x.reserve(protocol.Frame{Header: protocol.Header{Type: 14, Request: 1, Session: 7}})
	entered, release := make(chan struct{}), make(chan struct{})
	w := socketWriter{connection: x, raw: raw, request: 1, remaining: 34, attempt: func(fd int, bytes []byte) (int, error) {
		n, e := syscall.Write(fd, bytes)
		if len(bytes) == 2 && n == 2 && e == nil {
			close(entered)
			<-release
		}
		return n, e
	}}
	ack, _ := protocol.Encode(protocol.Ack{CompletedType: 14}, 1, 7, 1)
	done := make(chan error, 1)
	go func() { done <- protocol.WriteFrame(&w, ack) }()
	receive(t, b)
	<-entered
	admitted := make(chan protocol.ErrorCode, 1)
	go func() {
		admitted <- x.reserve(protocol.Frame{Header: protocol.Header{Type: 14, Request: 2, Session: 7}})
	}()
	select {
	case code := <-admitted:
		t.Fatalf("admission observed stale middle of final syscall: %d", code)
	case <-time.After(10 * time.Millisecond):
	}
	close(release)
	if code := <-admitted; code != 0 {
		t.Fatal("released final attempt leaves stale lane")
	}
	if e := <-done; e != nil {
		t.Fatal(e)
	}
}
func TestOrdinaryReplacementAt48AfterPeerReceipt(t *testing.T) {
	a, b := unixPair(t)
	defer a.Close()
	defer b.Close()
	raw, _ := a.(syscall.Conn).SyscallConn()
	gate := &outerGate{RawConn: raw, target: 2, entered: make(chan struct{}), release: make(chan struct{})}
	x := &connection{conn: rawGateConn{a, gate}, policy: policy.Default()}
	for i := 1; i <= 48; i++ {
		x.reserve(protocol.Frame{Header: protocol.Header{Type: 5, Request: protocol.RequestID(i), Session: 7}})
	}
	ack, _ := protocol.Encode(protocol.Ack{CompletedType: 5}, 1, 7, 1)
	done := make(chan error, 1)
	go func() { done <- x.writeFrame(ack) }()
	receive(t, b)
	<-gate.entered
	if x.reserve(protocol.Frame{Header: protocol.Header{Type: 5, Request: 49, Session: 7}}) != 0 {
		t.Fatal("delivered ordinary response not retired")
	}
	if x.reserve(protocol.Frame{Header: protocol.Header{Type: 5, Request: 50, Session: 7}}) != protocol.ErrorLimit {
		t.Fatal("accepted49th ordinary")
	}
	close(gate.release)
	if e := <-done; e != nil {
		t.Fatal(e)
	}
}
func TestRejectBlockingSocketMode(t *testing.T) {
	a, b := unixPair(t)
	defer a.Close()
	defer b.Close()
	raw, _ := a.(syscall.Conn).SyscallConn()
	if e := raw.Control(func(fd uintptr) {
		if err := syscall.SetNonblock(int(fd), false); err != nil {
			t.Fatal(err)
		}
	}); e != nil {
		t.Fatal(e)
	}
	defer raw.Control(func(fd uintptr) { syscall.SetNonblock(int(fd), true) })
	if _, e := validateRawConn(a); e == nil {
		t.Fatal("blocking descriptor accepted for atomic writer")
	}
}

func TestRejectedDecisionRetainsShutdownObligation(t *testing.T) {
	x := &connection{scheduler: NewScheduler()}
	for i := 1; i <= 48; i++ {
		x.reserve(protocol.Frame{Header: protocol.Header{Type: 5, Request: protocol.RequestID(i), Session: 7}})
	}
	code, e := x.reserveForDecode(protocol.Frame{Header: protocol.Header{Type: 5, Request: 49, Session: 7}})
	if e != nil || code != protocol.ErrorLimit {
		t.Fatal("limit decision")
	}
	for i := 1; i <= 48; i++ {
		retireCorrelation(x, protocol.RequestID(i))
	}
	x.reserve(protocol.Frame{Header: protocol.Header{Type: 8, Request: 50}})
	if x.sealShutdown(50) {
		t.Fatal("decision/registration gap forgot correlated LIMIT Error")
	}
}

func retireCorrelation(x *connection, req protocol.RequestID) {
	x.mu.Lock()
	defer x.mu.Unlock()
	x.retireLocked(req)
}

func TestConnectionStorageInventory(t *testing.T) {
	x := connection{}
	output, _ := protocol.Encode(protocol.OutputBytes{Sequence: 1, Bytes: make([]byte, 32768)}, 0, 7, 1)
	control, _ := protocol.Encode(protocol.ErrorMessage{Code: protocol.ErrorState, Message: strings.Repeat("x", 1024)}, 1, 7, 1)
	// Qualified runtime's backing capacity, not just wire length. Each frame is
	// temporary and separate from allocator-owned output tickets/control reserve.
	if cap(output.Body) > 65536 || cap(control.Body) > 65536 {
		t.Fatal("unexpected writer backing allocation exceeds frame scratch bound")
	}
	t.Logf("accepted records: %d bytes; rejection records: %d bytes; connection: %d bytes; event: %d bytes; output frame backing: %d; max control backing: %d", unsafe.Sizeof(x.accepted), unsafe.Sizeof(x.rejected), unsafe.Sizeof(x), unsafe.Sizeof(session.Event{}), cap(output.Body), cap(control.Body))
}
