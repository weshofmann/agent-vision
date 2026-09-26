package server

import (
	"context"
	"encoding/binary"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"net"
	"sync"
	"syscall"
	"testing"
	"time"
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
	x := &connection{conn: rawGateConn{a, gate}, policy: policy.Default(), changed: make(chan struct{})}
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
	x := &connection{changed: make(chan struct{})}
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
			x := &connection{conn: a, policy: p, changed: make(chan struct{})}
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

var _ = context.Background
var _ = binary.BigEndian
