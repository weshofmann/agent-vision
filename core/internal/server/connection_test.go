package server

import (
	"context"
	"errors"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"github.com/weshofmann/agent-vision/core/internal/session"
	"io"
	"net"
	"os"
	"syscall"
	"testing"
	"time"
)

type spawnFunc func(context.Context, session.SpawnConfig) (*session.Resources, error)

func (s spawnFunc) Spawn(c context.Context, f session.SpawnConfig) (*session.Resources, error) {
	return s(c, f)
}
func startConnection(t *testing.T, p policy.Policy, s session.Spawner) (net.Conn, *session.Manager, <-chan error) {
	t.Helper()
	a, b := unixPair(t)
	sched := NewScheduler()
	m := session.NewManager(p, s, sched)
	done := make(chan error, 1)
	go func() { done <- Serve(context.Background(), a, m, p) }()
	b.SetDeadline(time.Now().Add(3 * time.Second))
	t.Cleanup(func() {
		b.Close()
		select {
		case <-m.Done():
		case <-time.After(3 * time.Second):
			t.Error("manager cleanup not joined")
		}
	})
	return b, m, done
}
func send(t *testing.T, c net.Conn, v protocol.Message, r protocol.RequestID, s protocol.SessionID) {
	t.Helper()
	ver := uint16(1)
	if _, ok := v.(protocol.Hello); ok {
		ver = 0
	}
	f, e := protocol.Encode(v, r, s, ver)
	if e != nil {
		t.Fatal(e)
	}
	if e = protocol.WriteFrame(c, f); e != nil {
		t.Fatal(e)
	}
}
func receive(t *testing.T, c net.Conn) protocol.Frame {
	t.Helper()
	f, e := protocol.ReadFrame(c)
	if e != nil {
		t.Fatal(e)
	}
	return f
}
func hello(t *testing.T, c net.Conn) {
	send(t, c, protocol.Hello{MinMajor: 1, MaxMajor: 1}, 1, 0)
	if f := receive(t, c); f.Header.Type != protocol.TypeHelloAck || f.Header.Version != 0 {
		t.Fatal("invalid handshake reply")
	}
}
func noSpawn(context.Context, session.SpawnConfig) (*session.Resources, error) {
	return nil, errors.New("synthetic spawn failure")
}
func TestHandshakeState(t *testing.T) {
	for _, kind := range []string{"normal", "prehello", "incompatible", "duplicate", "request-reuse", "wrap", "direction"} {
		t.Run(kind, func(t *testing.T) {
			c, _, done := startConnection(t, policy.Default(), spawnFunc(noSpawn))
			if kind == "prehello" {
				send(t, c, protocol.Shutdown{}, 1, 0)
			} else if kind == "incompatible" {
				send(t, c, protocol.Hello{MinMajor: 2, MaxMajor: 2}, 1, 0)
			} else {
				hello(t, c)
				switch kind {
				case "normal":
					send(t, c, protocol.Shutdown{}, 2, 0)
				case "duplicate":
					send(t, c, protocol.Hello{MinMajor: 1, MaxMajor: 1}, 2, 0)
				case "request-reuse":
					send(t, c, protocol.Shutdown{}, 1, 0)
				case "wrap":
					send(t, c, protocol.CreateSession{Rows: 24, Cols: 80, ReceiveWindow: 262144}, ^protocol.RequestID(0), 0)
					receive(t, c)
					send(t, c, protocol.Shutdown{}, 2, 0)
				case "direction":
					send(t, c, protocol.Ack{CompletedType: protocol.TypeShutdown}, 2, 0)
				}
			}
			f := receive(t, c)
			wantVersion := uint16(1)
			if kind == "prehello" || kind == "incompatible" {
				wantVersion = 0
			}
			if f.Header.Version != wantVersion {
				t.Fatal("error/reply version violates negotiation")
			}
			if kind == "normal" {
				if f.Header.Type != protocol.TypeAck {
					t.Fatal("shutdown missing Ack")
				}
			} else if f.Header.Type != protocol.TypeError {
				t.Fatal("fatal protocol error missing response")
			}
			_, e := protocol.ReadFrame(c)
			if !errors.Is(e, io.EOF) {
				t.Fatalf("no terminal EOF: %v", e)
			}
			if e := <-done; (e == nil) != (kind == "normal") {
				t.Fatalf("Serve disposition: %v", e)
			}
		})
	}
}
func TestSlowDripFrameDeadline(t *testing.T) {
	p := policy.Default()
	p.FrameTimeout = 60 * time.Millisecond
	c, _, done := startConnection(t, p, spawnFunc(noSpawn))
	hello(t, c)
	start := time.Now()
	go func() {
		for i := 0; i < 12; i++ {
			if _, e := c.Write([]byte{'A'}); e != nil {
				return
			}
			time.Sleep(20 * time.Millisecond)
		}
	}()
	select {
	case e := <-done:
		if e == nil {
			t.Fatal("drip considered orderly")
		}
		if time.Since(start) > 180*time.Millisecond {
			t.Fatal("deadline reset by drips")
		}
	case <-time.After(300 * time.Millisecond):
		t.Fatal("partial-frame deadline absent")
	}
}
func TestEstablishedIdle(t *testing.T) {
	p := policy.Default()
	p.FrameTimeout = 20 * time.Millisecond
	p.HandshakeTimeout = 20 * time.Millisecond
	c, _, done := startConnection(t, p, spawnFunc(noSpawn))
	hello(t, c)
	time.Sleep(70 * time.Millisecond)
	send(t, c, protocol.Shutdown{}, 2, 0)
	receive(t, c)
	if e := <-done; e != nil {
		t.Fatal(e)
	}
}
func TestMalformedDisconnectCleanup(t *testing.T) {
	c, m, done := startConnection(t, policy.Default(), spawnFunc(noSpawn))
	hello(t, c)
	c.Write([]byte("broken"))
	c.Close()
	if e := <-done; e == nil {
		t.Fatal("malformed stream success")
	}
	select {
	case <-m.Done():
	default:
		t.Fatal("return before cleanup")
	}
}
func TestIPCDisappears(t *testing.T) {
	entered := make(chan struct{})
	returned := make(chan struct{})
	c, m, done := startConnection(t, policy.Default(), spawnFunc(func(ctx context.Context, _ session.SpawnConfig) (*session.Resources, error) {
		close(entered)
		<-ctx.Done()
		close(returned)
		return nil, ctx.Err()
	}))
	hello(t, c)
	send(t, c, protocol.CreateSession{Rows: 24, Cols: 80, ReceiveWindow: 262144}, 2, 0)
	<-entered
	c.Close()
	if e := <-done; e != nil {
		t.Fatal(e)
	}
	select {
	case <-returned:
	default:
		t.Fatal("start unjoined")
	}
	select {
	case <-m.Done():
	default:
		t.Fatal("manager unjoined")
	}
}

func TestCleanupUncertainNoAck(t *testing.T) {
	p := policy.Default()
	p.CleanupTimeout = 30 * time.Millisecond
	entered, release := make(chan struct{}), make(chan struct{})
	c, _, done := startConnection(t, p, spawnFunc(func(context.Context, session.SpawnConfig) (*session.Resources, error) {
		close(entered)
		<-release
		return nil, errors.New("synthetic late spawn")
	}))
	defer close(release)
	hello(t, c)
	send(t, c, protocol.CreateSession{Rows: 24, Cols: 80, ReceiveWindow: 262144}, 2, 0)
	<-entered
	send(t, c, protocol.Shutdown{}, 3, 0)
	f := receive(t, c)
	v, e := protocol.Decode(f)
	if e != nil {
		t.Fatal(e)
	}
	em, ok := v.(protocol.ErrorMessage)
	if !ok || em.Code != protocol.ErrorStatusUnavailable || f.Header.Request != 3 {
		t.Fatal("uncertain cleanup claimed success")
	}
	if e := <-done; e == nil {
		t.Fatal("uncertain cleanup exit success")
	}
}
func TestShutdownJoinsStarts(t *testing.T) {
	entered, release := make(chan struct{}, 16), make(chan struct{})
	c, _, done := startConnection(t, policy.Default(), spawnFunc(func(ctx context.Context, _ session.SpawnConfig) (*session.Resources, error) {
		entered <- struct{}{}
		<-release
		return nil, ctx.Err()
	}))
	hello(t, c)
	for i := 2; i <= 17; i++ {
		send(t, c, protocol.CreateSession{Rows: 24, Cols: 80, ReceiveWindow: 262144}, protocol.RequestID(i), 0)
	}
	for i := 0; i < 16; i++ {
		<-entered
	}
	send(t, c, protocol.Shutdown{}, 18, 0)
	c.SetReadDeadline(time.Now().Add(20 * time.Millisecond))
	if _, e := protocol.ReadFrame(c); e == nil {
		t.Fatal("completion before startup join")
	}
	c.SetReadDeadline(time.Now().Add(3 * time.Second))
	close(release)
	requests := map[protocol.RequestID]bool{}
	for i := 0; i < 17; i++ {
		f := receive(t, c)
		if requests[f.Header.Request] {
			t.Fatal("duplicate response")
		}
		requests[f.Header.Request] = true
		if i < 16 && f.Header.Type != protocol.TypeError {
			t.Fatal("shutdown overtook startup responses")
		}
		if i == 16 && f.Header.Type != protocol.TypeAck {
			t.Fatal("missing final shutdown Ack")
		}
	}
	if e := <-done; e != nil {
		t.Fatal(e)
	}
}

func unixPair(t *testing.T) (net.Conn, net.Conn) {
	t.Helper()
	fds, e := syscall.Socketpair(syscall.AF_UNIX, syscall.SOCK_STREAM, 0)
	if e != nil {
		t.Fatal(e)
	}
	files := []*os.File{os.NewFile(uintptr(fds[0]), "synthetic-a"), os.NewFile(uintptr(fds[1]), "synthetic-b")}
	a, e := net.FileConn(files[0])
	files[0].Close()
	if e != nil {
		t.Fatal(e)
	}
	b, e := net.FileConn(files[1])
	files[1].Close()
	if e != nil {
		a.Close()
		t.Fatal(e)
	}
	return a, b
}
