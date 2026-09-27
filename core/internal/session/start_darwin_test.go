//go:build darwin

package session

import (
	"context"
	"errors"
	"os"
	"runtime"
	"syscall"
	"testing"
	"time"

	"github.com/creack/pty"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
)

// Native qualification: real termios/size/spawn failures, plus a returned child
// after a failing spawn stage. Only its known PID is checked, never any-child wait.
func TestStartupNativeFailureBaselines(t *testing.T) {
	baseline := fdCount()
	verifiedCases := 0
	for _, step := range []string{"termios", "size", "spawn", "post-spawn"} {
		t.Run(step, func(t *testing.T) {
			saved := 0
			s := newSink()
			m := NewManager(policy.Default(), spawnFunc(func(ctx context.Context, c SpawnConfig) (*Resources, error) {
				if step == "spawn" {
					c.Cwd = "/does-not-exist"
					return (DarwinSpawner{}).Spawn(ctx, c)
				}
				if step == "post-spawn" {
					r, e := (DarwinSpawner{}).Spawn(ctx, c)
					if e != nil {
						return r, e
					}
					saved = r.Process.(*ownedProcess).ownedPID
					return r, errors.New("synthetic post-spawn failure")
				}
				master, slave, e := pty.Open()
				if e != nil {
					return nil, e
				}
				r := &Resources{Master: master, Slave: slave, MasterFD: int(master.Fd())}
				if step == "termios" {
					e = r.CloseParentSlave()
					t.Logf("termios fixture first parent-slave Close: %v", e)
					if e != nil {
						return r, e
					}
					e = configureV0Termios(slave)
				} else {
					e = closeFixtureMaster(r, func(f *os.File) error { return f.Close() })
					t.Logf("size fixture first master Close: %v; known-closed reference cleared=%t FD-invalid=%t", e, r.Master == nil, r.MasterFD == -1)
					if e != nil {
						return r, e
					}
					e = pty.Setsize(master, &pty.Winsize{Rows: 24, Cols: 80})
				}
				if e == nil {
					return r, errors.New("failure fixture did not fail")
				}
				return r, e
			}), s)
			t.Cleanup(func() {
				ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
				defer cancel()
				if e := m.Shutdown(ctx); e != nil {
					t.Errorf("failed-start cleanup: %v", e)
				}
			})
			create(t, m, 1)
			events := s.await(t, 1)
			v, ok := events[0].Message.(protocol.ErrorMessage)
			if !ok || v.Code != protocol.ErrorSpawn {
				t.Fatalf("native failed start response %+v", events)
			}
			shutdown(t, m)
			if saved != 0 {
				var status syscall.WaitStatus
				if _, e := syscall.Wait4(saved, &status, syscall.WNOHANG, nil); e != syscall.ECHILD {
					t.Fatalf("failed startup direct child not reaped: %v", e)
				}
			}
			runtime.GC()
			if n := fdCount(); n != baseline {
				t.Fatalf("native failed-start descriptor baseline %d -> %d", baseline, n)
			}
			verifiedCases++
			t.Logf("%s: failure response, joined Shutdown, direct-child check and descriptor baseline reached", step)
		})
	}
	if verifiedCases == 4 {
		t.Log("all four real failed-start descriptor baselines reached; returned direct child reaped")
	} else {
		t.Logf("only %d of four failed-start descriptor baselines reached", verifiedCases)
	}
}
