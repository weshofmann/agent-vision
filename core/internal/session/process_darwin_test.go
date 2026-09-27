//go:build darwin

package session

import (
	"context"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"syscall"
	"testing"
	"time"
	"unsafe"

	"github.com/creack/pty"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
)

func config(shell string) SpawnConfig {
	return SpawnConfig{Rows: 31, Cols: 91, Shell: shell, Env: []string{"PATH=/usr/bin:/bin", "PS1=READY> ", "ENV=/dev/null"}}
}
func spawnTest(t *testing.T, shell string) *Resources {
	t.Helper()
	r, e := (DarwinSpawner{}).Spawn(context.Background(), config(shell))
	t.Cleanup(func() {
		if r == nil {
			return
		}
		c, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		if e := r.Rollback(c); e != nil {
			t.Error(e)
		}
	})
	if e != nil {
		t.Fatal(e)
	}
	return r
}
func result(t *testing.T, p Process) ProcessResult {
	t.Helper()
	select {
	case r := <-p.Done():
		return r
	case <-time.After(2 * time.Second):
		t.Fatal("process watchdog")
		return ProcessResult{}
	}
}

// Darwin tty close can wait for unread output; real sessions have a reader.
func drainResult(t *testing.T, r *Resources) ProcessResult {
	t.Helper()
	b := make([]byte, 8192)
	deadline := time.After(2 * time.Second)
	for {
		select {
		case v := <-r.Process.Done():
			return v
		case <-deadline:
			t.Fatal("drained process watchdog")
			return ProcessResult{}
		default:
		}
		syscall.Read(r.MasterFD, b)
		time.Sleep(time.Millisecond)
	}
}
func writePTY(t *testing.T, r *Resources, s string) {
	t.Helper()
	deadline := time.Now().Add(time.Second)
	b := []byte(s)
	for len(b) > 0 {
		n, e := syscall.Write(r.MasterFD, b)
		if n > 0 {
			b = b[n:]
		}
		if e != nil && e != syscall.EAGAIN && e != syscall.EINTR {
			t.Fatal(e)
		}
		if time.Now().After(deadline) {
			t.Fatal("write watchdog")
		}
		if n <= 0 {
			time.Sleep(time.Millisecond)
		}
	}
}
func readUntil(t *testing.T, r *Resources, want string) string {
	t.Helper()
	var out strings.Builder
	deadline := time.Now().Add(2 * time.Second)
	b := make([]byte, 8192)
	for time.Now().Before(deadline) {
		n, e := syscall.Read(r.MasterFD, b)
		if n > 0 {
			out.Write(b[:n])
			if strings.Contains(out.String(), want) {
				return out.String()
			}
		}
		if e != nil && e != syscall.EAGAIN && e != syscall.EINTR {
			t.Fatalf("read before marker: %v", e)
		}
		time.Sleep(time.Millisecond)
	}
	t.Fatalf("marker %q absent (synthetic output %q)", want, out.String())
	return ""
}
func buildNative(t *testing.T, source string) string {
	t.Helper()
	out := filepath.Join(t.TempDir(), "native-fixture")
	cmd := exec.Command("/usr/bin/clang", "-std=c11", "-Wall", "-Wextra", "-Werror", source, "-o", out)
	if b, e := cmd.CombinedOutput(); e != nil {
		t.Fatalf("native fixture: %v %s", e, b)
	}
	return out
}

// Catches loss of explicit termios, controlling tty, tty streams, and resize.
func TestDarwinPTYAndTermios(t *testing.T) {
	m, s, e := pty.Open()
	if e != nil {
		t.Fatal(e)
	}
	defer m.Close()
	defer s.Close()
	if e = configureV0Termios(s); e != nil {
		t.Fatal(e)
	}
	var got syscall.Termios
	_, _, errno := syscall.Syscall(syscall.SYS_IOCTL, s.Fd(), syscall.TIOCGETA, uintptr(unsafe.Pointer(&got)))
	if errno != 0 {
		t.Fatal(errno)
	}
	if got.Iflag != 0x4300 || got.Oflag != 3 || got.Cflag != 0xb00 || got.Lflag != 0x5cf || got.Ispeed != 38400 || got.Ospeed != 38400 {
		t.Fatalf("V0 flags/speeds mismatch: %#v", got)
	}
	for i, v := range map[int]uint8{syscall.VINTR: 3, syscall.VQUIT: 28, syscall.VERASE: 127, syscall.VKILL: 21, syscall.VEOF: 4, syscall.VEOL: 255, syscall.VEOL2: 255, syscall.VSTART: 17, syscall.VSTOP: 19, syscall.VSUSP: 26, syscall.VREPRINT: 18, syscall.VWERASE: 23, syscall.VLNEXT: 22, syscall.VMIN: 1, syscall.VTIME: 0} {
		if got.Cc[i] != v {
			t.Fatalf("control %d=%d want %d", i, got.Cc[i], v)
		}
	}
	// Exercise session exec while another readiness wait owns wake descriptors.
	pollCtx, pollCancel := context.WithCancel(context.Background())
	pollJoined := make(chan error, 1)
	go func() { pollJoined <- (DarwinPoller{}).Wait(pollCtx, int(m.Fd()), false) }()
	defer func() {
		pollCancel()
		if e := <-pollJoined; !errors.Is(e, context.Canceled) {
			t.Error(e)
		}
	}()
	time.Sleep(10 * time.Millisecond)
	r := spawnTest(t, buildNative(t, "testdata/tty-child.c"))
	readUntil(t, r, "TTY_OK 31 91")
	if e = pty.Setsize(r.Master, &pty.Winsize{Rows: 33, Cols: 97}); e != nil {
		t.Fatal(e)
	}
	readUntil(t, r, "WINCH 33 97")
	writePTY(t, r, "exit\n")
	v := drainResult(t, r)
	if v.Err != nil || v.Status != (protocol.ExitStatus{Kind: 1, Value: 17}) {
		t.Fatalf("%+v", v)
	}
}

// Catches invalid shell acceptance, mismatched argv/env, and login argv.
func TestShellFallback(t *testing.T) {
	file := filepath.Join(t.TempDir(), "noexec")
	if e := os.WriteFile(file, []byte("#!/bin/sh\n"), 0600); e != nil {
		t.Fatal(e)
	}
	for _, shell := range []string{"relative", t.TempDir(), file, "/does-not-exist"} {
		t.Run(filepath.Base(shell), func(t *testing.T) {
			r := spawnTest(t, shell)
			readUntil(t, r, "READY> ")
			writePTY(t, r, "printf 'ENV=%s ARGV=%s TERM=%s COLOR=%s\\n' \"$SHELL\" \"$0\" \"$TERM\" \"$COLORTERM\"\n")
			readUntil(t, r, "ENV=/bin/sh ARGV=/bin/sh TERM=xterm-256color COLOR=truecolor\r\nREADY> ")
			writePTY(t, r, "exit 17\n")
			if v := drainResult(t, r); v.Err != nil || v.Status.Value != 17 {
				t.Fatalf("%+v", v)
			}
		})
	}
}

// Catches broken canonical control-byte/job handling and attached hangup.
func TestInteractiveJobControl(t *testing.T) {
	r := spawnTest(t, "/bin/sh")
	readUntil(t, r, "READY> ")
	writePTY(t, r, "sleep 30\n")
	waitForegroundJob(t, r)
	writePTY(t, r, "\x03")
	readUntil(t, r, "READY> ")
	writePTY(t, r, "sleep 30\n")
	waitForegroundJob(t, r)
	writePTY(t, r, "\x1a")
	readUntil(t, r, "Stopped")
	writePTY(t, r, "fg\n")
	waitForegroundJob(t, r)
	writePTY(t, r, "\x03")
	readUntil(t, r, "READY> ")
	closeErr := closeFixtureMaster(r, func(f *os.File) error { return f.Close() })
	t.Logf("job-control first master Close=%v; known-closed reference cleared=%t FD-invalid=%t", closeErr, r.Master == nil, r.MasterFD == -1)
	if closeErr != nil {
		t.Fatal(closeErr)
	}
	v := drainResult(t, r)
	if v.Err != nil || v.Status.Kind != 2 || v.Status.Value != uint32(syscall.SIGHUP) {
		t.Fatalf("hangup %+v", v)
	}
}

// Wait for driver foreground ownership, rather than assuming a scheduling delay.
func waitForegroundJob(t *testing.T, r *Resources) {
	t.Helper()
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		var group int32
		_, _, e := syscall.Syscall(syscall.SYS_IOCTL, uintptr(r.MasterFD), syscall.TIOCGPGRP, uintptr(unsafe.Pointer(&group)))
		if e != 0 {
			t.Fatal(e)
		}
		if group > 0 && int(group) != r.Process.(*ownedProcess).ownedPID {
			return
		}
		time.Sleep(time.Millisecond)
	}
	t.Fatal("foreground job watchdog")
}

// Catches wait/copier misuse, resource leakage and post-reap signalling.
func TestSerializedReaper(t *testing.T) {
	warm := spawnTest(t, "/bin/sh")
	readUntil(t, warm, "READY> ")
	writePTY(t, warm, "exit\n")
	drainResult(t, warm)
	warm.Rollback(context.Background())
	runtime.GC()
	bf, bg := fdCount(), runtime.NumGoroutine()
	for i := 0; i < 50; i++ {
		r := spawnTest(t, "/bin/sh")
		readUntil(t, r, "READY> ")
		saved := r.Process.(*ownedProcess).ownedPID
		if saved <= 0 {
			t.Fatal("invalid PID")
		}
		if i%2 == 0 {
			writePTY(t, r, "exit 17\n")
		} else {
			r.Process.RequestKill()
		}
		v := drainResult(t, r)
		want := protocol.ExitStatus{Kind: 1, Value: 17}
		if i%2 != 0 {
			want = protocol.ExitStatus{Kind: 2, Value: 9}
		}
		if v.Err != nil || v.Status != want {
			t.Fatalf("cycle %d: %+v", i, v)
		}
		r.Process.RequestHangup()
		r.Process.RequestKill()
		var ws syscall.WaitStatus
		if _, e := syscall.Wait4(saved, &ws, syscall.WNOHANG, nil); e != syscall.ECHILD {
			t.Fatalf("second saved-PID wait %v", e)
		}
		if e := r.Rollback(context.Background()); e != nil {
			t.Fatal(e)
		}
		runtime.GC()
	}
	runtime.GC()
	t.Logf("50 cycles: descriptor baseline %d restored; goroutine baseline %d restored", bf, bg)
	if n := fdCount(); n != bf {
		t.Fatalf("FD baseline %d -> %d", bf, n)
	}
	if n := runtime.NumGoroutine(); n > bg {
		t.Fatalf("goroutine baseline %d -> %d", bg, n)
	}
}
func fdCount() int {
	n := 0
	for fd := 0; fd < 1024; fd++ {
		_, _, e := syscall.Syscall(syscall.SYS_FCNTL, uintptr(fd), syscall.F_GETFD, 0)
		if e == 0 {
			n++
		}
	}
	return n
}

// Injection below real owner covers unforceable authority-loss paths.
func TestUncertainWaitStopsSignals(t *testing.T) {
	p := newOwnedProcess(123, processOps{wait: func(pid int, s *syscall.WaitStatus) (int, error) {
		if pid != 123 {
			t.Error("wrong PID")
		}
		return 0, syscall.ECHILD
	}, signal: func(int, syscall.Signal) error { t.Error("signal after uncertain wait"); return nil }, release: func() error { return nil }})
	p.RequestHangup()
	p.RequestKill()
	v := result(t, p)
	if !errors.Is(v.Err, syscall.ECHILD) || v.Status.Kind != 3 {
		t.Fatalf("%+v", v)
	}
	p.RequestKill()
}
func TestWaitInterruptReleaseAndMapping(t *testing.T) {
	for _, tt := range []struct {
		name   string
		status syscall.WaitStatus
		want   protocol.ExitStatus
	}{{"exit", 17 << 8, protocol.ExitStatus{Kind: 1, Value: 17}}, {"signal", 9, protocol.ExitStatus{Kind: 2, Value: 9}}, {"core", 6 | 0x80, protocol.ExitStatus{Kind: 2, Value: 6, CoreDump: true}}} {
		t.Run(tt.name, func(t *testing.T) {
			calls := 0
			releaseErr := errors.New("release failed")
			p := newOwnedProcess(123, processOps{wait: func(pid int, s *syscall.WaitStatus) (int, error) {
				if pid != 123 {
					t.Error("wrong PID")
				}
				calls++
				if calls == 1 {
					return 0, syscall.EINTR
				}
				*s = tt.status
				return 123, nil
			}, signal: func(int, syscall.Signal) error { t.Error("signal after wait"); return nil }, release: func() error { return releaseErr }})
			p.RequestKill()
			v := result(t, p)
			if calls != 2 || v.Status != tt.want || !errors.Is(v.Err, releaseErr) {
				t.Fatalf("calls %d result %+v", calls, v)
			}
		})
	}
}
func TestPollCancelBeforeFDReuse(t *testing.T) {
	var f [2]int
	if e := syscall.Pipe(f[:]); e != nil {
		t.Fatal(e)
	}
	defer syscall.Close(f[1])
	fd := f[0]
	c, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() { done <- (DarwinPoller{}).Wait(c, fd, false) }()
	time.Sleep(10 * time.Millisecond)
	cancel()
	select {
	case e := <-done:
		if !errors.Is(e, context.Canceled) {
			t.Fatal(e)
		}
	case <-time.After(100 * time.Millisecond):
		t.Fatal("cancel watchdog")
	}
	syscall.Close(fd)
	var g [2]int
	if e := syscall.Pipe(g[:]); e != nil {
		t.Fatal(e)
	}
	defer syscall.Close(g[0])
	defer syscall.Close(g[1])
	if g[0] != fd {
		t.Fatal("fixture did not recycle FD")
	}
	if _, e := syscall.Write(g[1], []byte{1}); e != nil {
		t.Fatal(e)
	}
	c2, cancel2 := context.WithTimeout(context.Background(), time.Second)
	defer cancel2()
	if e := (DarwinPoller{}).Wait(c2, g[0], false); e != nil {
		t.Fatal(e)
	}
}

// Failed delivery of a signal does not release a still-owned live child.
func TestSignalFailureRetainsOwner(t *testing.T) {
	gate := make(chan struct{})
	signalErr := errors.New("synthetic signal failure")
	released := make(chan struct{}, 1)
	p := newOwnedProcess(123, processOps{wait: func(_ int, s *syscall.WaitStatus) (int, error) {
		select {
		case <-gate:
			*s = 17 << 8
			return 123, nil
		default:
			return 0, nil
		}
	}, signal: func(int, syscall.Signal) error { return signalErr }, release: func() error { released <- struct{}{}; return nil }})
	p.RequestHangup()
	select {
	case <-released:
		t.Fatal("released live child after signal error")
	case <-time.After(20 * time.Millisecond):
	}
	close(gate)
	v := result(t, p)
	if v.Status != (protocol.ExitStatus{Kind: 1, Value: 17}) || !errors.Is(v.Err, signalErr) {
		t.Fatalf("retained status %+v", v)
	}
}

// Caller deadline is not cleanup completion; retry preserves result and ownership.
func TestRollbackDeadlineAndConcurrentDone(t *testing.T) {
	gate := make(chan struct{})
	p := newOwnedProcess(123, processOps{wait: func(_ int, s *syscall.WaitStatus) (int, error) {
		select {
		case <-gate:
			*s = 17 << 8
			return 123, nil
		default:
			return 0, nil
		}
	}, signal: func(int, syscall.Signal) error { return nil }, release: func() error { return nil }})
	r := &Resources{Process: p, rollback: p.cleanup}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if e := r.Rollback(ctx); !errors.Is(e, context.Canceled) {
		t.Fatal(e)
	}
	public := make(chan ProcessResult, 1)
	go func() { public <- <-p.Done() }()
	done := make(chan error, 1)
	go func() { done <- r.Rollback(context.Background()) }()
	close(gate)
	select {
	case v := <-public:
		if v.Err != nil || v.Status != (protocol.ExitStatus{Kind: 1, Value: 17}) {
			t.Fatalf("lost public result %+v", v)
		}
	case <-time.After(time.Second):
		t.Fatal("public result watchdog")
	}
	select {
	case e := <-done:
		if e != nil {
			t.Fatal(e)
		}
	case <-time.After(time.Second):
		t.Fatal("rollback watchdog")
	}
	if e := r.Rollback(context.Background()); e != nil {
		t.Fatal(e)
	}
	uncertain := newOwnedProcess(123, processOps{wait: func(int, *syscall.WaitStatus) (int, error) { return 0, syscall.ECHILD }, signal: func(int, syscall.Signal) error { t.Error("uncertain signal"); return nil }, release: func() error { return nil }})
	ur := &Resources{Process: uncertain, rollback: uncertain.cleanup}
	result(t, uncertain)
	for i := 0; i < 2; i++ {
		if e := ur.Rollback(context.Background()); !errors.Is(e, syscall.ECHILD) {
			t.Fatalf("lost uncertain cleanup %v", e)
		}
	}
}

func TestSpawnFailureRollback(t *testing.T) {
	baseline := fdCount()
	c := config("/bin/sh")
	c.Cwd = "/does-not-exist"
	if r, e := (DarwinSpawner{}).Spawn(context.Background(), c); e == nil || r != nil {
		t.Fatalf("failed cwd leaked resources: %v", e)
	}
	c = config("/bin/sh")
	c.Rows = 0
	if r, e := (DarwinSpawner{}).Spawn(context.Background(), c); e == nil || r != nil {
		t.Fatalf("invalid size admitted: %v", e)
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if r, e := (DarwinSpawner{}).Spawn(ctx, config("/bin/sh")); !errors.Is(e, context.Canceled) || r != nil {
		t.Fatal("cancelled spawn acquired resources")
	}
	if n := fdCount(); n != baseline {
		t.Fatalf("failed spawn FD %d -> %d", baseline, n)
	}
}

// Parent normalization occurs before exec; the Go runtime owns its own handlers.
func TestNativeNormalizedSIGCHLD(t *testing.T) {
	launcher := buildNative(t, "testdata/sigchld-launch.c")
	binary, e := os.Executable()
	if e != nil {
		t.Fatal(e)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, launcher, binary, "-test.run=^TestSerializedReaper$", "-test.timeout=10s")
	cmd.Env = []string{"PATH=/usr/bin:/bin", "GODEBUG=execwait=2", "GOGC=1"}
	b, e := cmd.CombinedOutput()
	if e != nil {
		t.Fatalf("normalized runtime lifecycle: %v %s", e, b)
	}
	if !strings.Contains(string(b), "NORMALIZED_SIGCHLD") || !strings.Contains(string(b), "PASS") {
		t.Fatalf("missing synthetic normalized result: %s", b)
	}
}

func TestPollWriteAndFailure(t *testing.T) {
	var f [2]int
	if e := syscall.Pipe(f[:]); e != nil {
		t.Fatal(e)
	}
	defer syscall.Close(f[0])
	defer syscall.Close(f[1])
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	if e := (DarwinPoller{}).Wait(ctx, f[1], true); e != nil {
		t.Fatal(e)
	}
	baseline := fdCount()
	if e := (DarwinPoller{}).Wait(ctx, -1, false); e == nil {
		t.Fatal("invalid descriptor considered ready")
	}
	if n := fdCount(); n != baseline {
		t.Fatalf("failed poll FD %d -> %d", baseline, n)
	}
}

// Cancel causally after the real native start returns its actual child owner.
func TestCancelledSpawnReturnsOwnedResources(t *testing.T) {
	baseline := fdCount()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	ops := nativeDarwinSpawnOps()
	starts := 0
	var actualChild Process
	ops.start = func(c SpawnConfig, s *os.File) (Process, func(context.Context) error, error) {
		starts++
		child, cleanup, e := startDarwinChild(c, s)
		actualChild = child
		registerKnownFixtureChildCleanup(t.Cleanup, func(e error) { t.Errorf("known native-returned child cleanup: %v", e) }, cleanup)
		if child != nil {
			cancel()
		}
		return child, cleanup, e
	}
	r, e := (DarwinSpawner{ops: &ops}).Spawn(ctx, config("/bin/sh"))
	registerReturnedFixtureCleanup(t.Cleanup, func(e error) { t.Errorf("cancelled returned owner cleanup: %v", e) }, r)

	if !errors.Is(e, context.Canceled) || r == nil || r.Process == nil || r.Process != actualChild || starts != 1 {
		t.Fatalf("post-spawn cancellation failed adoption: starts=%d exact-owner=%t err=%v", starts, r != nil && r.Process == actualChild, e)
	}
	cleanupCtx, cleanupCancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cleanupCancel()
	if e = r.Rollback(cleanupCtx); e != nil {
		t.Fatal(e)
	}
	v := result(t, r.Process)
	if v.Err != nil || v.Status.Kind == 3 {
		t.Fatalf("adopted child status %+v", v)
	}
	if n := fdCount(); n != baseline {
		t.Fatalf("cancelled spawn FD %d -> %d", baseline, n)
	}
	t.Logf("one real start; same cancelled child owner reaped with available status %+v; FD baseline restored", v.Status)
}

func TestDarwinResizeWaitInterleavings(t *testing.T) {
	native := buildNative(t, "testdata/tty-wait-interleaving.c")
	for _, schedule := range []string{"before", "after"} {
		t.Run(schedule, func(t *testing.T) {
			c := config(native)
			c.Env = append(c.Env, "WAIT_TEST_MODE="+schedule)
			r, e := (DarwinSpawner{}).Spawn(context.Background(), c)
			if e != nil {
				t.Fatal(e)
			}
			t.Cleanup(func() {
				ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
				defer cancel()
				if e := r.Rollback(ctx); e != nil {
					t.Error(e)
				}
			})
			marker := "WAIT_ENTERED"
			if schedule == "before" {
				marker = "BEFORE_WAIT"
			}
			output := readUntil(t, r, marker)
			if !strings.Contains(output, "TTY_OK 31 91") {
				t.Fatal("initial tty/size checks did not precede wait barrier")
			}
			if e := pty.Setsize(r.Master, &pty.Winsize{Rows: 33, Cols: 97}); e != nil {
				t.Fatal(e)
			}
			if schedule == "before" {
				writePTY(t, r, "continue\n")
			}
			readUntil(t, r, "WINCH 33 97")
			writePTY(t, r, "exit\n")
			v := drainResult(t, r)
			if v.Err != nil || v.Status != (protocol.ExitStatus{Kind: 1, Value: 17}) {
				t.Fatalf("single-resize child status %+v", v)
			}
		})
	}
}
