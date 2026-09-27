//go:build darwin

package session

import (
	"context"
	"errors"
	"os"
	"sync"
	"syscall"
	"testing"
)

// Exercises only temporary files: catches a fixture discarding a first close
// error or letting rollback issue a second ambiguous native Close.
func TestDarwinFixtureCloseInjected(t *testing.T) {
	for _, failed := range []bool{false, true} {
		t.Run(map[bool]string{false: "known-closed", true: "first-error-retained"}[failed], func(t *testing.T) {
			f, e := os.CreateTemp(t.TempDir(), "fixture-master")
			if e != nil {
				t.Fatal(e)
			}

			r := &Resources{Master: f, MasterFD: int(f.Fd())}
			beforeFD := r.MasterFD
			calls := 0
			t.Cleanup(func() { _ = r.Rollback(context.Background()) })
			boom := syscall.EIO
			close := func(file *os.File) error {
				calls++
				if file != f {
					t.Fatal("wrong owner")
				}
				e := file.Close()
				if failed {
					return errors.Join(e, boom)
				}
				return e
			}
			e = closeFixtureMaster(r, close)
			if failed {
				if !errors.Is(e, boom) || r.Master != f || r.MasterFD != beforeFD {
					t.Fatal("ambiguous owner/error discarded")
				}
				if e = r.Rollback(context.Background()); !errors.Is(e, boom) {
					t.Fatal("first error erased")
				}
			} else {
				if e != nil || r.Master != nil || r.MasterFD != -1 {
					t.Fatal("known successful close not relinquished")
				}
				if e = r.Rollback(context.Background()); e != nil {
					t.Fatal(e)
				}
			}
			_ = r.Rollback(context.Background())
			if calls != 1 {
				t.Fatalf("actual close attempts=%d", calls)
			}
			if _, e = f.Stat(); !errors.Is(e, os.ErrClosed) {
				t.Fatal("saved operation target not closed")
			}
		})
	}
	t.Run("slave-uses-recorded-close", func(t *testing.T) {
		f, e := os.CreateTemp(t.TempDir(), "fixture-slave")
		if e != nil {
			t.Fatal(e)
		}
		r := &Resources{Slave: f}
		t.Cleanup(func() { _ = r.Rollback(context.Background()) })
		if e := r.CloseParentSlave(); e != nil {
			t.Fatal(e)
		}
		if e := r.Rollback(context.Background()); e != nil {
			t.Fatal(e)
		}
	})
}

// A deliberate fixture Close must share its actual first result with rollback.
// No Resources once guard is marked complete manually. Only known success
// relinquishes this fixture's reference and invalidates its saved numeric FD.
func closeFixtureMaster(r *Resources, closeMaster func(*os.File) error) error {
	master := r.Master
	if master == nil {
		return errors.New("fixture has no master")
	}
	var once sync.Once
	var firstErr error
	firstClose := func() error { once.Do(func() { firstErr = closeMaster(master) }); return firstErr }
	previousClose := r.close
	r.close = func(f *os.File) error {
		if f == master {
			return firstClose()
		}
		if previousClose != nil {
			return previousClose(f)
		}
		return f.Close()
	}
	if e := firstClose(); e != nil {
		return e
	}
	r.Master = nil
	r.MasterFD = -1
	return nil
}
