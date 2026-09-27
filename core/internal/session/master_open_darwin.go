//go:build darwin

package session

import (
	"context"
	"errors"
	"fmt"
	"os"
	"syscall"
	"time"
)

const (
	masterOpenMaxAttempts = 3
	masterOpenFirstWait   = 5 * time.Millisecond
	masterOpenSecondWait  = 10 * time.Millisecond
	masterOpenRetryWindow = 50 * time.Millisecond
)

type masterOpenOps struct {
	open func() (int, error)
	now  func() time.Time
	wait func(context.Context, time.Duration) error
}
type MasterOpenError struct {
	Operation string
	Attempts  uint8
	Elapsed   time.Duration
	Err       error
}

func (e *MasterOpenError) Error() string {
	return fmt.Sprintf("%s after %d attempt(s), %s: %v", e.Operation, e.Attempts, e.Elapsed, e.Err)
}
func (e *MasterOpenError) Unwrap() error { return e.Err }

// Admission limits do not bound an already-entered native call. No detached
// acquisition worker exists: a late descriptor is returned to its owner.
func acquireDarwinMaster(ctx context.Context, ops masterOpenOps) (*os.File, MasterOpenReport, error) {
	return acquireDarwinMasterLimit(ctx, ops, masterOpenMaxAttempts)
}

// limit=1 is the predeclared comparison control; production always uses three.
func acquireDarwinMasterLimit(ctx context.Context, ops masterOpenOps, limit int) (*os.File, MasterOpenReport, error) {
	start := ops.now()
	r := MasterOpenReport{}
	var last error
	finish := func(f *os.File, e error) (*os.File, MasterOpenReport, error) {
		r.Elapsed = ops.now().Sub(start)
		if e != nil && !r.Cancelled {
			e = &MasterOpenError{Operation: "master open", Attempts: r.Attempts, Elapsed: r.Elapsed, Err: e}
		}
		return f, r, e
	}
	cancelled := func() (*os.File, MasterOpenReport, error) {
		r.Cancelled = true
		return finish(nil, errors.Join(ctx.Err(), last))
	}
	if limit < 1 || limit > masterOpenMaxAttempts {
		return finish(nil, errors.New("invalid master attempt limit"))
	}
	for {
		if ctx.Err() != nil {
			return cancelled()
		}
		if r.Attempts > 0 && ops.now().Sub(start) >= masterOpenRetryWindow {
			r.Exhausted = true
			return finish(nil, last)
		}
		fd, e := ops.open()
		index := r.Attempts
		r.Attempts++
		if e != nil {
			last = e
		}
		if errno, ok := e.(syscall.Errno); ok {
			r.Errnos[index] = errno
			r.ErrnoObserved[index] = true
		}
		var f *os.File
		if fd >= 0 {
			f = os.NewFile(uintptr(fd), "/dev/ptmx")
		}
		// Ownership is established before cancellation/error interpretation.
		if ctx.Err() != nil {
			r.Cancelled = true
			return finish(f, errors.Join(ctx.Err(), last))
		}
		if fd >= 0 {
			r.Recovered = e == nil && r.Attempts > 1
			return finish(f, e)
		}
		if e == nil {
			return finish(nil, errors.New("master open returned invalid descriptor"))
		}
		errno, typed := e.(syscall.Errno)
		if fd != -1 || !typed || errno != ^syscall.Errno(5) {
			return finish(nil, e)
		}
		if int(r.Attempts) >= limit || ops.now().Sub(start) >= masterOpenRetryWindow {
			r.Exhausted = true
			return finish(nil, last)
		}
		delay := masterOpenFirstWait
		if r.Attempts == 2 {
			delay = masterOpenSecondWait
		}
		if e = ops.wait(ctx, delay); e != nil {
			if ctx.Err() != nil {
				return cancelled()
			}
			return finish(nil, errors.Join(last, e))
		}
		// Loop rechecks cancellation and admission after an overslept wait.
	}
}

func nativeMasterOpenOps() masterOpenOps {
	return masterOpenOps{
		open: func() (int, error) { return syscall.Open("/dev/ptmx", syscall.O_RDWR|syscall.O_CLOEXEC, 0) },
		now:  time.Now,
		wait: func(ctx context.Context, delay time.Duration) error {
			timer := time.NewTimer(delay)
			defer timer.Stop()
			select {
			case <-ctx.Done():
				return ctx.Err()
			case <-timer.C:
				return nil
			}
		},
	}
}
