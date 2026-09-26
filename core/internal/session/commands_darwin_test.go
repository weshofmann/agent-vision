//go:build darwin

package session

import (
	"bytes"
	"context"
	"errors"
	"os"
	"sync"
	"syscall"
	"testing"
	"time"
	"unsafe"

	"github.com/creack/pty"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
)

// A raw, nonreading slave exercises the actual Darwin adapter and EAGAIN; a
// canonical line limit could discard data without applying genuine backpressure.
func rawCommandResources(t *testing.T) *Resources {
	t.Helper()
	master, slave, err := pty.Open()
	if err != nil {
		t.Fatal(err)
	}
	r := fixtureResources(newFixtureProcess())
	r.Master, r.Slave, r.MasterFD = master, slave, int(master.Fd())
	t.Cleanup(func() { r.Rollback(context.Background()) })
	mode := syscall.Termios{Cflag: syscall.CS8 | syscall.CREAD, Ispeed: syscall.B38400, Ospeed: syscall.B38400}
	mode.Cc[syscall.VMIN] = 1
	_, _, errno := syscall.Syscall(syscall.SYS_IOCTL, slave.Fd(), syscall.TIOCSETA, uintptr(unsafe.Pointer(&mode)))
	if errno != 0 {
		t.Fatal(errno)
	}
	if err = syscall.SetNonblock(r.MasterFD, true); err != nil {
		t.Fatal(err)
	}
	return r
}
func commandFlags(t *testing.T, fd int) uintptr {
	t.Helper()
	flags, _, err := syscall.Syscall(syscall.SYS_FCNTL, uintptr(fd), syscall.F_GETFL, 0)
	if err != 0 {
		t.Fatal(err)
	}
	return flags
}

type countedNativeCommands struct {
	nativeCommandIO
	mu          sync.Mutex
	prefix      []byte
	blocked     chan struct{}
	once        sync.Once
	afterCancel func()
}

func (io *countedNativeCommands) Write(fd int, b []byte) (int, error) {
	n, err := io.nativeCommandIO.Write(fd, b)
	io.mu.Lock()
	if n > 0 {
		io.prefix = append(io.prefix, b[:n]...)
	}
	io.mu.Unlock()
	return n, err
}
func (io *countedNativeCommands) Wait(ctx context.Context, fd int) error {
	io.once.Do(func() { close(io.blocked) })
	err := io.nativeCommandIO.Wait(ctx, fd)
	if ctx.Err() != nil && io.afterCancel != nil {
		io.afterCancel()
	}
	return err
}
func (io *countedNativeCommands) snapshot() []byte {
	io.mu.Lock()
	defer io.mu.Unlock()
	return append([]byte(nil), io.prefix...)
}
func nativeCommandManager(t *testing.T, r *Resources, p policy.Policy, io commandIO) (*Manager, *recordingSink, protocol.SessionID) {
	t.Helper()
	s := newSink()
	m := NewManager(p, spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) { return r, nil }), s)
	if io != nil {
		m.commandIO = io
	}
	t.Cleanup(func() { shutdown(t, m) })
	create(t, m, 1)
	return m, s, s.await(t, 1)[0].Session
}

func TestCommandNativeBackpressure(t *testing.T) {
	r := rawCommandResources(t)
	p := policy.Default()
	p.InputTimeout = 60 * time.Millisecond
	io := &countedNativeCommands{blocked: make(chan struct{})}
	m, s, id := nativeCommandManager(t, r, p, io)
	input := bytes.Repeat([]byte("0123456789abcdef"), 2048)
	admitCommand(t, m, protocol.InputBytes{Bytes: input}, 2, id)
	waitGate(t, io.blocked)
	admitCommand(t, m, protocol.ResizeSession{Rows: 31, Cols: 91}, 3, id)
	e := completion(t, s, 2)
	prefix := io.snapshot()
	if len(prefix) == 0 || len(prefix) >= len(input) || !bytes.Equal(prefix, input[:len(prefix)]) {
		t.Fatalf("invalid actual accepted prefix length %d", len(prefix))
	}
	assertError(t, e, protocol.ErrorInputTimeout, uint32(len(prefix)))
	assertError(t, completion(t, s, 3), protocol.ErrorState, 0)
	s.await(t, 5)
	onceCompletions(t, s, 2, 3)
	t.Logf("native raw nonreading slave reached EAGAIN; exact accepted prefix %d bytes", len(prefix))
}

func TestCommandNativeCancelJoinFDReuse(t *testing.T) {
	r := rawCommandResources(t)
	cancelSeen, release := make(chan struct{}), make(chan struct{})
	io := &countedNativeCommands{blocked: make(chan struct{}), afterCancel: func() { close(cancelSeen); <-release }}
	m, s, id := nativeCommandManager(t, r, policy.Default(), io)
	input := bytes.Repeat([]byte{0x61}, 32768)
	admitCommand(t, m, protocol.InputBytes{Bytes: input}, 2, id)
	waitGate(t, io.blocked)
	admitCommand(t, m, protocol.ResizeSession{Rows: 31, Cols: 91}, 3, id)
	admitCommand(t, m, protocol.CloseSession{}, 4, id)
	waitGate(t, cancelSeen)
	if commandFlags(t, r.MasterFD)&syscall.O_NONBLOCK == 0 {
		t.Fatal("master closed or blocking before worker join")
	}
	close(release)
	completion(t, s, 4)
	assertError(t, completion(t, s, 2), protocol.ErrorState, uint32(len(io.snapshot())))
	assertError(t, completion(t, s, 3), protocol.ErrorState, 0)
	if _, err := r.Master.Stat(); !errors.Is(err, os.ErrClosed) {
		t.Fatal("master retained after completed Close")
	}
	// Force reuse after the barrier, then verify the replacement descriptor remains
	// usable. No obsolete readiness/write owner may touch this identity now.
	var f [2]int
	if err := syscall.Pipe(f[:]); err != nil {
		t.Fatal(err)
	}
	defer func() { syscall.Close(f[0]); syscall.Close(f[1]) }()
	syscall.CloseOnExec(f[0])
	syscall.CloseOnExec(f[1])
	target := r.MasterFD
	if f[1] != target {
		if f[0] == target { // reserve another pipe rather than clobber the reader
			var g [2]int
			if err := syscall.Pipe(g[:]); err != nil {
				t.Fatal(err)
			}
			syscall.Close(f[0])
			syscall.Close(f[1])
			f = g
			syscall.CloseOnExec(f[0])
			syscall.CloseOnExec(f[1])
		}
		if err := syscall.Dup2(f[1], target); err != nil {
			t.Fatal(err)
		}
		defer syscall.Close(target)
	}
	if _, err := syscall.Write(target, []byte("reuse")); err != nil {
		t.Fatal(err)
	}
	b := make([]byte, 5)
	if n, err := syscall.Read(f[0], b); err != nil || n != 5 || string(b) != "reuse" {
		t.Fatalf("FD reuse corrupted %q %v", b, err)
	}
	onceCompletions(t, s, 2, 3, 4)
}

func TestCommandNativeResizeSizeSignalAndFlags(t *testing.T) {
	native := buildNative(t, "testdata/tty-child.c")
	var r *Resources
	s := newSink()
	m := NewManager(policy.Default(), spawnFunc(func(ctx context.Context, _ SpawnConfig) (*Resources, error) {
		var err error
		r, err = (DarwinSpawner{}).Spawn(ctx, config(native))
		return r, err
	}), s)
	t.Cleanup(func() { shutdown(t, m) })
	create(t, m, 1)
	id := s.await(t, 1)[0].Session
	outputUntil(t, s, "TTY_OK 31 91")
	before := commandFlags(t, r.MasterFD)
	if before&syscall.O_NONBLOCK == 0 {
		t.Fatal("master not nonblocking before resize")
	}
	admitCommand(t, m, protocol.ResizeSession{Rows: 33, Cols: 97}, 2, id)
	assertAck(t, completion(t, s, 2), protocol.TypeResizeSession)
	if after := commandFlags(t, r.MasterFD); after != before {
		t.Fatalf("resize changed master flags %#x -> %#x", before, after)
	}
	outputUntil(t, s, "WINCH 33 97")
	admitCommand(t, m, protocol.InputBytes{Bytes: []byte("exit\n")}, 3, id)
	assertAck(t, completion(t, s, 3), protocol.TypeInputBytes)
	m.mu.Lock()
	record := m.records[id]
	m.mu.Unlock()
	waitGate(t, record.observed)
	if record.result.Err != nil || record.result.Status != (protocol.ExitStatus{Kind: protocol.ExitNormal, Value: 17}) {
		t.Fatalf("native ordered exit %+v", record.result)
	}
	t.Log("captured-FD ioctl applied size; foreground SIGWINCH arrived; nonblocking flags unchanged")
}

// Real PTY backpressure in A must not prevent independent PTY input/resize in B.
func TestTwoSessionCommandsNative(t *testing.T) {
	a, b := rawCommandResources(t), rawCommandResources(t)
	slaveFD := int(b.Slave.Fd())
	if err := syscall.SetNonblock(slaveFD, true); err != nil {
		t.Fatal(err)
	}
	io := &countedNativeCommands{blocked: make(chan struct{})}
	s := newSink()
	calls := 0
	m := NewManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
		calls++
		if calls == 1 {
			return a, nil
		}
		return b, nil
	}), s)
	m.commandIO = io
	t.Cleanup(func() { shutdown(t, m) })
	create(t, m, 1)
	aID := s.await(t, 1)[0].Session
	admitCommand(t, m, protocol.InputBytes{Bytes: bytes.Repeat([]byte{0x61}, 32768)}, 3, aID)
	waitGate(t, io.blocked)
	create(t, m, 2)
	bID := completion(t, s, 2).Session
	admitCommand(t, m, protocol.InputBytes{Bytes: []byte("B progresses")}, 4, bID)
	assertAck(t, completion(t, s, 4), protocol.TypeInputBytes)
	admitCommand(t, m, protocol.ResizeSession{Rows: 33, Cols: 97}, 5, bID)
	assertAck(t, completion(t, s, 5), protocol.TypeResizeSession)
	out := make([]byte, 32)
	n, err := syscall.Read(slaveFD, out)
	if err != nil || string(out[:n]) != "B progresses" {
		t.Fatalf("B native input not received: %v", err)
	}
	admitCommand(t, m, protocol.CloseSession{}, 6, aID)
	completion(t, s, 6)
	onceCompletions(t, s, 3, 4, 5, 6)
}

func outputUntil(t *testing.T, s *recordingSink, marker string) {
	t.Helper()
	deadline := time.After(time.Second)
	for {
		var b []byte
		for _, e := range s.snapshot() {
			if o, ok := e.Message.(protocol.OutputBytes); ok {
				b = append(b, o.Bytes...)
			}
		}
		if bytes.Contains(b, []byte(marker)) {
			return
		}
		select {
		case <-s.wake:
		case <-deadline:
			t.Fatalf("manager output marker %q absent", marker)
		}
	}
}
