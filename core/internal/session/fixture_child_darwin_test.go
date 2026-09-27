//go:build darwin

package session

import (
	"context"
	"errors"
	"sync/atomic"
	"syscall"
	"testing"
	"time"
)

// No native child/PTY: catches fixture cleanup tied to the adoption assertion,
// forgotten returned resources, unjoined sole reapers, and erased cleanup errors.
func TestDarwinFixtureChildCleanupInjected(t *testing.T) {
	for _, ownership := range []string{"nil-resources", "missing-child", "mismatched-child"} {
		t.Run(ownership, func(t *testing.T) {
			var ready atomic.Bool
			var waits, releases atomic.Int32
			known := newOwnedProcess(123, processOps{
				wait: func(pid int, status *syscall.WaitStatus) (int, error) {
					waits.Add(1)
					if !ready.Load() {
						return 0, nil
					}
					*status = 0
					return pid, nil
				},
				signal: func(pid int, sig syscall.Signal) error {
					if pid != 123 {
						t.Error("signal identity")
					}
					ready.Store(true)
					return nil
				},
				release: func() error { releases.Add(1); return nil },
			})
			// Regression failure must not itself abandon its synthetic worker.
			t.Cleanup(func() {
				ready.Store(true)
				ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
				defer cancel()
				_ = known.cleanup(ctx)
			})
			var callbacks []func()
			var reported []error
			register := func(f func()) { callbacks = append(callbacks, f) }
			report := func(e error) { reported = append(reported, e) }
			cleanupCalls := 0
			boom := errors.New("known child cleanup uncertainty")
			actualCleanup := func(ctx context.Context) error {
				cleanupCalls++
				deadline, ok := ctx.Deadline()
				if !ok || time.Until(deadline) > 2*time.Second || time.Until(deadline) < time.Second {
					t.Error("cleanup bound changed")
				}
				return errors.Join(known.cleanup(ctx), boom)
			}
			// Same registration used at the native start seam, before forwarding ownership.
			registerKnownFixtureChildCleanup(register, report, actualCleanup)
			var returned *Resources
			returnedCalls := 0
			returnedErr := errors.New("returned owner cleanup uncertainty")
			var wrong *ownedProcess
			var wrongReleases atomic.Int32
			if ownership != "nil-resources" {
				returned = &Resources{rollback: func(context.Context) error { returnedCalls++; return returnedErr }}
				if ownership == "mismatched-child" {
					wrong = newOwnedProcess(456, processOps{wait: func(pid int, status *syscall.WaitStatus) (int, error) { *status = 0; return pid, nil }, signal: func(int, syscall.Signal) error { t.Error("wrong signal after reap"); return nil }, release: func() error { wrongReleases.Add(1); return nil }})
					returned.Process = wrong
					returned.rollback = func(ctx context.Context) error { returnedCalls++; return errors.Join(wrong.cleanup(ctx), returnedErr) }
				}
			}
			t.Cleanup(func() {
				if returned != nil {
					ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
					defer cancel()
					_ = returned.Rollback(ctx)
				}
			})
			registerReturnedFixtureCleanup(register, report, returned)
			for i := len(callbacks) - 1; i >= 0; i-- {
				callbacks[i]()
			}
			if cleanupCalls != 1 {
				t.Fatalf("known native-returned cleanup called %d times", cleanupCalls)
			}
			select {
			case <-known.complete:
			default:
				t.Fatal("known sole reaper not joined")
			}
			if releases.Load() != 1 || waits.Load() < 1 {
				t.Fatal("known child lifecycle not completed")
			}
			sawKnown, sawReturned := false, false
			for _, e := range reported {
				sawKnown = sawKnown || errors.Is(e, boom)
				sawReturned = sawReturned || errors.Is(e, returnedErr)
			}
			if !sawKnown {
				t.Fatal("known cleanup error hidden")
			}
			if returned != nil && (!sawReturned || returnedCalls != 1) {
				t.Fatal("returned resource cleanup/error lost")
			}
			if wrong != nil {
				select {
				case <-wrong.complete:
				default:
					t.Fatal("returned mismatched sole reaper not joined")
				}
				if wrongReleases.Load() != 1 {
					t.Fatal("returned owner not released")
				}
			}
		})
	}
}

func registerKnownFixtureChildCleanup(register func(func()), report func(error), cleanup func(context.Context) error) {
	if cleanup == nil {
		return
	}
	register(func() {
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		if e := cleanup(ctx); e != nil {
			report(e)
		}
	})
}
func registerReturnedFixtureCleanup(register func(func()), report func(error), returned *Resources) {
	register(func() {
		if returned == nil {
			return
		}
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		if e := returned.Rollback(ctx); e != nil {
			report(e)
		}
	})
}
