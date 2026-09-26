//go:build darwin

package server

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"runtime"
	"syscall"
	"testing"
	"time"

	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"github.com/weshofmann/agent-vision/core/internal/session"
)

// Forces accepted Deferred Credit to outlive registry removal and the manager's
// already-enqueued Shutdown Ack. Changing the writer to trust ordinal alone must
// make this fail by delivering Shutdown before the credit's terminal response.
func TestShutdownDeferredCreditAfterRemoval(t *testing.T) {
	a, b := unixPair(t)
	defer b.Close()
	b.SetDeadline(time.Now().Add(3 * time.Second))
	raw, _ := a.(syscall.Conn).SyscallConn()
	gate := &outerGate{RawConn: raw, target: 6, entered: make(chan struct{}), release: make(chan struct{})}
	released := false
	defer func() {
		if !released {
			close(gate.release)
		}
	}()
	shell := filepath.Join(t.TempDir(), "synthetic-shell")
	if e := os.WriteFile(shell, []byte("#!/bin/sh\nprintf z\nread synthetic\n"), 0700); e != nil {
		t.Fatal(e)
	}
	p := policy.Default()
	s := NewScheduler()
	m := session.NewManager(p, spawnFunc(func(ctx context.Context, c session.SpawnConfig) (*session.Resources, error) {
		c.Shell = shell
		c.Env = []string{"PATH=/usr/bin:/bin", "HOME=/nonexistent"}
		return (session.DarwinSpawner{}).Spawn(ctx, c)
	}), s)
	done := make(chan error, 1)
	go func() { done <- Serve(context.Background(), rawGateConn{a, gate}, m, p) }()
	hello(t, b)
	send(t, b, protocol.CreateSession{Rows: 24, Cols: 80, ReceiveWindow: 262144}, 2, 0)
	created := receive(t, b)
	id := created.Header.Session
	if created.Header.Type != 4 {
		t.Fatal("not Created")
	}
	output := receive(t, b)
	if output.Header.Type != 9 || len(output.Body) != 9 || output.Body[8] != 'z' {
		t.Fatal("wrong synthetic output")
	}
	<-gate.entered
	send(t, b, protocol.OutputCredit{RawBytes: 1}, 3, id)
	send(t, b, protocol.Shutdown{}, 4, 0)
	select {
	case <-m.Done():
	case <-time.After(time.Second):
		t.Fatal("cleanup waited for Writing ticket")
	}
	f, _ := protocol.Encode(protocol.OutputCredit{RawBytes: 1}, 99, id, 1)
	var admission *session.AdmissionError
	if e := m.Admit(f); !errors.As(e, &admission) || admission.Code != protocol.ErrorUnknownSession {
		t.Fatal("shutdown has not removed registry")
	}
	close(gate.release)
	released = true
	creditCount := 0
	exitedSeen, closedSeen := false, false
	for {
		frame := receive(t, b)
		if frame.Header.Request == 3 {
			msg, _ := protocol.Decode(frame)
			ack, ok := msg.(protocol.Ack)
			if !ok || ack.CompletedType != 14 {
				t.Fatal("deferred Credit lost completion")
			}
			creditCount++
		}
		if frame.Header.Type == 10 {
			exitedSeen = true
		}
		if frame.Header.Type == 11 {
			closedSeen = true
		}
		if frame.Header.Request == 4 {
			if frame.Header.Type != 13 || creditCount != 1 || !exitedSeen || !closedSeen {
				t.Fatal("Shutdown overtook accepted response/lifecycle")
			}
			break
		}
	}
	if _, e := protocol.ReadFrame(b); e == nil {
		t.Fatal("missing EOF")
	}
	if e := <-done; e != nil {
		t.Fatal(e)
	}
}

func TestNativeEndpointLossOwnedCleanup(t *testing.T) {
	c, m, done := startConnection(t, policy.Default(), session.DarwinSpawner{})
	hello(t, c)
	send(t, c, protocol.CreateSession{Rows: 24, Cols: 80, ReceiveWindow: 262144}, 2, 0)
	created := receive(t, c)
	if created.Header.Type != 4 {
		t.Fatal("not Created")
	}
	c.Close()
	if e := <-done; e != nil {
		t.Fatal(e)
	}
	select {
	case <-m.Done():
	default:
		t.Fatal("manager not joined")
	}
}

func TestRepeatCloseAfterPeerReceipt(t *testing.T) {
	a, b := unixPair(t)
	defer b.Close()
	b.SetDeadline(time.Now().Add(3 * time.Second))
	raw, _ := a.(syscall.Conn).SyscallConn()
	gate := &outerGate{RawConn: raw, target: 8, entered: make(chan struct{}), release: make(chan struct{})}
	shell := filepath.Join(t.TempDir(), "silent-shell")
	os.WriteFile(shell, []byte("#!/bin/sh\nread synthetic\n"), 0700)
	p := policy.Default()
	s := NewScheduler()
	m := session.NewManager(p, spawnFunc(func(ctx context.Context, c session.SpawnConfig) (*session.Resources, error) {
		c.Shell = shell
		c.Env = []string{"PATH=/usr/bin:/bin", "HOME=/nonexistent"}
		return (session.DarwinSpawner{}).Spawn(ctx, c)
	}), s)
	done := make(chan error, 1)
	go func() { done <- Serve(context.Background(), rawGateConn{a, gate}, m, p) }()
	hello(t, b)
	send(t, b, protocol.CreateSession{Rows: 24, Cols: 80, ReceiveWindow: 262144}, 2, 0)
	created := receive(t, b)
	id := created.Header.Session
	send(t, b, protocol.CloseSession{}, 3, id)
	for {
		f := receive(t, b)
		if f.Header.Type == 11 {
			break
		}
	}
	<-gate.entered
	send(t, b, protocol.CloseSession{}, 4, id)
	close(gate.release)
	f := receive(t, b)
	v, _ := protocol.Decode(f)
	e, ok := v.(protocol.ErrorMessage)
	if f.Header.Request != 4 || !ok || e.Code != protocol.ErrorUnknownSession {
		t.Fatal("received Closed followed by stale Close STATE")
	}
	send(t, b, protocol.Shutdown{}, 5, 0)
	receive(t, b)
	if e := <-done; e != nil {
		t.Fatal(e)
	}
}

func nativeFDCount() int {
	n := 0
	for fd := 0; fd < 4096; fd++ {
		_, _, e := syscall.Syscall(syscall.SYS_FCNTL, uintptr(fd), syscall.F_GETFD, 0)
		if e == 0 {
			n++
		}
	}
	return n
}
func TestNativeSixteenStartsEndpointBaselines(t *testing.T) {
	// Warm runtime network/PTY poll resources before observing baselines.
	warm, _, warmDone := startConnection(t, policy.Default(), session.DarwinSpawner{})
	hello(t, warm)
	send(t, warm, protocol.Shutdown{}, 2, 0)
	receive(t, warm)
	<-warmDone
	warm.Close()
	runtime.GC()
	beforeFD, beforeWorkers := nativeFDCount(), runtime.NumGoroutine()
	resources := make(chan *session.Resources, 16)
	spawnErrors := make(chan error, 16)
	c, m, done := startConnection(t, policy.Default(), spawnFunc(func(ctx context.Context, c session.SpawnConfig) (*session.Resources, error) {
		c.Shell = "/bin/sh"
		c.Env = []string{"PATH=/usr/bin:/bin", "HOME=/nonexistent", "ENV=/dev/null", "PS1="}
		r, e := (session.DarwinSpawner{}).Spawn(ctx, c)
		if e != nil {
			spawnErrors <- e
			return r, e
		}
		resources <- r
		<-ctx.Done()
		return r, ctx.Err()
	}))
	hello(t, c)
	for i := 2; i <= 17; i++ {
		send(t, c, protocol.CreateSession{Rows: 24, Cols: 80, ReceiveWindow: 262144}, protocol.RequestID(i), 0)
	}
	owned := make([]*session.Resources, 0, 16)
	for i := 0; i < 16; i++ {
		select {
		case r := <-resources:
			owned = append(owned, r)
		case e := <-spawnErrors:
			t.Fatalf("native start failed: %v", e)
		case <-time.After(2 * time.Second):
			t.Fatal("native starts failed")
		}
	}
	c.Close()
	if e := <-done; e != nil {
		t.Fatal(e)
	}
	select {
	case <-m.Done():
	default:
		t.Fatal("cleanup not joined")
	}
	for _, r := range owned {
		if _, e := r.Master.Stat(); !errors.Is(e, os.ErrClosed) {
			t.Fatal("owned PTY master still open")
		}
	}
	runtime.GC()
	if n := nativeFDCount(); n != beforeFD {
		t.Fatalf("FD baseline differs: before%d after%d", beforeFD, n)
	}
	deadline := time.Now().Add(200 * time.Millisecond)
	for runtime.NumGoroutine() > beforeWorkers && time.Now().Before(deadline) {
		runtime.GC()
		time.Sleep(time.Millisecond)
	}
	if n := runtime.NumGoroutine(); n > beforeWorkers {
		t.Fatalf("userspace workers remain: before%d after%d", beforeWorkers, n)
	}
	t.Log("sixteen returned native children adopted/rolled back; masters closed; FD and goroutine baselines restored")
}
func TestNativeBlockedSocketAndPTYCleanup(t *testing.T) {
	a, b := unixPair(t)
	defer b.Close()
	b.SetDeadline(time.Now().Add(3 * time.Second))
	raw, _ := a.(syscall.Conn).SyscallConn()
	raw.Control(func(fd uintptr) { syscall.SetsockoptInt(int(fd), syscall.SOL_SOCKET, syscall.SO_SNDBUF, 1024) })
	shell := filepath.Join(t.TempDir(), "flood-shell")
	os.WriteFile(shell, []byte("#!/bin/sh\nwhile :; do printf 'SYNTHETIC_OUTPUT_BLOCK_0123456789'; done\n"), 0700)
	resources := make(chan *session.Resources, 1)
	p := policy.Default()
	p.WriteTimeout = 40 * time.Millisecond
	p.CreditTimeout = 60 * time.Millisecond
	s := NewScheduler()
	m := session.NewManager(p, spawnFunc(func(ctx context.Context, c session.SpawnConfig) (*session.Resources, error) {
		c.Shell = shell
		c.Env = []string{"PATH=/usr/bin:/bin", "HOME=/nonexistent"}
		r, e := (session.DarwinSpawner{}).Spawn(ctx, c)
		if r != nil {
			resources <- r
		}
		return r, e
	}), s)
	done := make(chan error, 1)
	go func() { done <- Serve(context.Background(), a, m, p) }()
	hello(t, b)
	send(t, b, protocol.CreateSession{Rows: 24, Cols: 80, ReceiveWindow: 262144}, 2, 0)
	created := receive(t, b)
	if created.Header.Type != 4 {
		t.Fatal("not Created")
	}
	r := <-resources
	select {
	case e := <-done:
		if e == nil {
			t.Fatal("slow consumer falsely orderly")
		}
	case <-time.After(time.Second):
		t.Fatal("blocked socket/PTY prevented cleanup")
	}
	if _, e := r.Master.Stat(); !errors.Is(e, os.ErrClosed) {
		t.Fatal("master remains open")
	}
	select {
	case <-m.Done():
	default:
		t.Fatal("manager not joined")
	}
}
