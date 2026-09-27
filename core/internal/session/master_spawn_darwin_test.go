//go:build darwin

package session

import (
	"context"
	"errors"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"os"
	"sync"
	"sync/atomic"
	"syscall"
	"testing"
	"time"
)

// Catches post-master recovery, missed adoption, double close/start, and erased uncertainty.
func TestDarwinMasterSpawnInjected(t *testing.T) {
	for _, phase := range []string{"name", "grant", "unlock", "slave", "termios", "size", "nonblock", "start", "cancel-master", "cancel-slave", "cancel-termios", "cancel-size", "cancel-nonblock", "cancel-start", "start-owned-error", "slave-close", "success"} {
		t.Run(phase, func(t *testing.T) {
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			f := newSpawnFixture(t, phase, cancel)
			r, e := (DarwinSpawner{ops: &f.ops}).Spawn(ctx, config("/bin/sh"))
			if phase == "success" {
				if e != nil || r == nil {
					t.Fatalf("success %v", e)
				}
			} else if e == nil {
				t.Fatal("failure accepted")
			}
			if phase == "cancel-master" && (f.slaves != 0 || f.starts != 0) {
				t.Fatal("work after cancellation")
			}
			if r != nil {
				_ = r.Rollback(context.Background())
				_ = r.Rollback(context.Background())
			}
			if f.opens != 1 || f.masterCloses != 1 || f.slaveCloses > 1 {
				t.Fatalf("ownership: opens=%d masterClose=%d slaveClose=%d", f.opens, f.masterCloses, f.slaveCloses)
			}
			slaveOwned := phase != "name" && phase != "grant" && phase != "unlock" && phase != "cancel-master"
			if slaveOwned != (f.slaveCloses == 1) {
				t.Fatalf("slave closes=%d", f.slaveCloses)
			}
			wantStarts := 0
			switch phase {
			case "start", "cancel-start", "start-owned-error", "slave-close", "success":
				wantStarts = 1
			}
			if f.starts != wantStarts {
				t.Fatalf("starts=%d want %d", f.starts, wantStarts)
			}
			if phase == "slave-close" {
				if r == nil || !errors.Is(e, f.boom) || !errors.Is(r.Rollback(context.Background()), f.boom) {
					t.Fatal("first post-child close erased")
				}
			}
			if wantStarts == 1 && phase != "start" {
				if f.releases.Load() != 1 || f.waits.Load() != 1 {
					t.Fatalf("sole reaper waits=%d releases=%d", f.waits.Load(), f.releases.Load())
				}
			}
		})
	}
	t.Run("later-negative-six-close-failure-retains-owner", func(t *testing.T) {
		f := newSpawnFixture(t, "name", func() {})
		f.boom = ^syscall.Errno(5)
		f.masterCloseErr = syscall.EACCES
		r, e := (DarwinSpawner{ops: &f.ops}).Spawn(context.Background(), config("/bin/sh"))
		if r == nil || !errors.Is(e, f.boom) || !errors.Is(e, syscall.EACCES) {
			t.Fatalf("owner/error lost %v %v", r, e)
		}
		if e := r.Rollback(context.Background()); !errors.Is(e, syscall.EACCES) {
			t.Fatal("cleanup uncertainty erased")
		}
		if f.opens != 1 || f.masterCloses != 1 || f.starts != 0 {
			t.Fatal("reacquisition or duplicate cleanup")
		}
	})

	t.Run("cancel-master-close-error-retains-owner", func(t *testing.T) {
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		f := newSpawnFixture(t, "cancel-master", cancel)
		f.masterCloseErr = syscall.EACCES
		r, e := (DarwinSpawner{ops: &f.ops}).Spawn(ctx, config("/bin/sh"))
		if r == nil || !errors.Is(e, context.Canceled) || !errors.Is(e, syscall.EACCES) {
			t.Fatalf("lost late owner/error: %v %v", r, e)
		}
		if e := r.Rollback(context.Background()); !errors.Is(e, syscall.EACCES) {
			t.Fatal("close error erased")
		}
		if f.opens != 1 || f.slaves != 0 || f.starts != 0 || f.masterCloses != 1 {
			t.Fatal("work/reclose after cancellation")
		}
	})
	t.Run("concurrent-rollback-one-close-one-reaper", func(t *testing.T) {
		f := newSpawnFixture(t, "success", func() {})
		r, e := (DarwinSpawner{ops: &f.ops}).Spawn(context.Background(), config("/bin/sh"))
		if e != nil {
			t.Fatal(e)
		}
		var wg sync.WaitGroup
		errs := make(chan error, 16)
		for i := 0; i < 16; i++ {
			wg.Add(1)
			go func() { defer wg.Done(); errs <- r.Rollback(context.Background()) }()
		}
		wg.Wait()
		close(errs)
		for e := range errs {
			if e != nil {
				t.Fatal(e)
			}
		}
		if f.opens != 1 || f.starts != 1 || f.masterCloses != 1 || f.slaveCloses != 1 || f.waits.Load() != 1 || f.releases.Load() != 1 {
			t.Fatal("duplicate resource lifecycle")
		}
	})
	t.Run("post-child-close-manager-shutdown", func(t *testing.T) {
		f := newSpawnFixture(t, "slave-close", func() {})
		sink := newSink()
		m := newFixtureManager(policy.Default(), DarwinSpawner{ops: &f.ops}, sink)
		create(t, m, 1)
		sink.await(t, 1)
		m.mu.Lock()
		var reservation *reservation
		for _, r := range m.records {
			reservation = r
		}
		m.mu.Unlock()
		if reservation == nil || reservation.resources == nil {
			t.Fatal("uncertain reservation lost")
		}
		<-reservation.startDone
		if e := reservation.resources.Rollback(context.Background()); !errors.Is(e, f.boom) {
			t.Fatal("repeated rollback erased first close")
		}
		if e := m.Admit(frame(t, protocol.Shutdown{}, 2, 0)); e != nil {
			t.Fatal(e)
		}
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		if e := m.Shutdown(ctx); !errors.Is(e, f.boom) {
			t.Fatalf("Shutdown success despite close failure: %v", e)
		}
		events := sink.await(t, 2)
		for _, e := range events {
			if _, ok := e.Message.(protocol.Ack); ok {
				t.Fatal("successful Shutdown Ack")
			}
		}
		m.mu.Lock()
		retained := len(m.records)
		m.mu.Unlock()
		if retained != 1 {
			t.Fatal("reservation discarded")
		}
		if f.opens != 1 || f.starts != 1 || f.masterCloses != 1 || f.slaveCloses != 1 || f.waits.Load() != 1 || f.releases.Load() != 1 {
			t.Fatalf("counts %+v", f)
		}
	})
	t.Run("first-ErrClosed-is-not-erased", func(t *testing.T) {
		f := newSpawnFixture(t, "slave-close", func() {})
		f.boom = os.ErrClosed
		r, e := (DarwinSpawner{ops: &f.ops}).Spawn(context.Background(), config("/bin/sh"))
		if r == nil || !errors.Is(e, os.ErrClosed) {
			t.Fatal("missing first error")
		}
		if e := r.Rollback(context.Background()); !errors.Is(e, os.ErrClosed) {
			t.Fatal("ErrClosed erased")
		}
		if f.slaveCloses != 1 {
			t.Fatal("reclose")
		}
	})
}

type spawnFixture struct {
	ops                                              darwinSpawnOps
	boom, masterCloseErr                             error
	opens, slaves, starts, masterCloses, slaveCloses int
	waits, releases                                  atomic.Int32
	master, slave                                    *os.File
}

func newSpawnFixture(t *testing.T, phase string, cancel func()) *spawnFixture {
	t.Helper()
	f := &spawnFixture{boom: syscall.EIO}
	master, e := os.CreateTemp(t.TempDir(), "master")
	if e != nil {
		t.Fatal(e)
	}
	f.master = master
	slave, e := os.CreateTemp(t.TempDir(), "slave")
	if e != nil {
		t.Fatal(e)
	}
	f.slave = slave
	t.Cleanup(func() { master.Close(); slave.Close() })
	fail := func(at string) error {
		if phase == "cancel-"+at {
			cancel()
		}
		if phase == at {
			return f.boom
		}
		return nil
	}
	f.ops = darwinSpawnOps{
		master: masterOpenOps{now: time.Now, wait: func(context.Context, time.Duration) error { t.Fatal("post-master failure entered retry"); return nil }, open: func() (int, error) {
			f.opens++
			if phase == "cancel-master" {
				cancel()
			}
			fd, e := syscall.Dup(int(master.Fd()))
			return fd, e
		}},
		slave: func(m *os.File) (*os.File, error) {
			f.slaves++
			if m == nil {
				t.Fatal("missing master")
			}
			for _, at := range []string{"name", "grant", "unlock"} {
				if e := fail(at); e != nil {
					return nil, e
				}
			}
			return slave, fail("slave")
		},
		termios: func(s *os.File) error {
			if s != slave {
				t.Fatal("slave identity")
			}
			return fail("termios")
		},
		size: func(m *os.File, rows, cols uint16) error {
			if (rows != 31 || cols != 91) && (rows != 24 || cols != 80) {
				t.Fatal("size arguments")
			}
			return fail("size")
		},
		nonblock: func(fd int) error {
			if fd < 0 {
				t.Fatal("missing fd")
			}
			return fail("nonblock")
		},
		close: func(file *os.File) error {
			e := file.Close()
			if file == slave {
				f.slaveCloses++
				if f.slaveCloses == 1 {
					return errors.Join(e, fail("slave-close"))
				}
				return e
			}
			f.masterCloses++
			return errors.Join(e, f.masterCloseErr)
		},
		start: func(c SpawnConfig, s *os.File) (Process, func(context.Context) error, error) {
			f.starts++
			if s != slave {
				t.Fatal("start slave")
			}
			if e := fail("start"); e != nil {
				return nil, nil, e
			}
			p := newOwnedProcess(123, processOps{wait: func(pid int, status *syscall.WaitStatus) (int, error) { f.waits.Add(1); *status = 0; return pid, nil }, signal: func(int, syscall.Signal) error { t.Fatal("signal after reap"); return nil }, release: func() error { f.releases.Add(1); return nil }})
			if phase == "cancel-start" {
				cancel()
			}
			if phase == "start-owned-error" {
				return p, p.cleanup, f.boom
			}
			return p, p.cleanup, nil
		},
	}
	return f
}
