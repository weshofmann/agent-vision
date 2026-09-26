//go:build darwin

package session

import (
	"context"
	"errors"
	"runtime"
	"syscall"
	"testing"

	"github.com/creack/pty"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
)

// Native qualification: real termios/size/spawn failures, plus a returned child
// after a failing spawn stage. Only its known PID is checked, never any-child wait.
func TestStartupNativeFailureBaselines(t *testing.T) {
	baseline := fdCount()
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
					slave.Close()
					e = configureV0Termios(slave)
				} else {
					master.Close()
					e = pty.Setsize(master, &pty.Winsize{Rows: 24, Cols: 80})
				}
				if e == nil {
					return r, errors.New("failure fixture did not fail")
				}
				return r, e
			}), s)
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
		})
	}
	t.Log("real failed-start descriptors restored; returned direct child reaped")
}
