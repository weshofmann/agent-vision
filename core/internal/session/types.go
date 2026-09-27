// Package session owns terminal resources and direct-child lifecycle.
package session

import (
	"context"
	"errors"
	"os"
	"sync"
	"syscall"
	"time"

	"github.com/weshofmann/agent-vision/core/internal/protocol"
)

type ProcessResult struct {
	Status protocol.ExitStatus
	Err    error
}

// Done publishes one result then closes; the session sequencer is its sole reader.
// Hangup/Kill are requests to the lifecycle owner, never caller-side signals.
type Process interface {
	RequestHangup()
	RequestKill()
	Done() <-chan ProcessResult
}
type SpawnConfig struct {
	Rows, Cols uint16
	Shell, Cwd string
	Env        []string
}
type Spawner interface {
	Spawn(context.Context, SpawnConfig) (*Resources, error)
}
type Poller interface {
	Wait(context.Context, int, bool) error
}

// MasterOpenReport has bounded same-call observations; it is not wire telemetry.
type MasterOpenReport struct {
	Attempts                        uint8
	Elapsed                         time.Duration
	Errnos                          [3]syscall.Errno
	ErrnoObserved                   [3]bool
	Recovered, Exhausted, Cancelled bool
}

// MasterFD is captured before nonblocking configuration. Users must cancel and
// join every FD worker before Rollback closes it; never call Master.Fd afterwards.
// Returned resources always belong to the caller, including a cancelled spawn.
type Resources struct {
	Master, Slave  *os.File
	MasterFD       int
	Process        Process
	closeOnce      sync.Once
	closeErr       error
	slaveCloseOnce sync.Once
	slaveCloseErr  error
	rollback       func(context.Context) error
	close          func(*os.File) error
	MasterOpen     MasterOpenReport
}

// Rollback is retryable after deadline expiry. A timeout never relinquishes child
// ownership or claims cleanup success. The owner continues reaping independently.
func (r *Resources) Rollback(ctx context.Context) error {
	r.closeOnce.Do(func() {
		if r.Slave != nil {
			r.closeErr = errors.Join(r.closeErr, r.CloseParentSlave())
		}
		if r.Master != nil {
			r.closeErr = errors.Join(r.closeErr, r.closeOwned(r.Master))
		}
	})
	if r.rollback != nil {
		return errors.Join(r.closeErr, r.rollback(ctx))
	}
	return r.closeErr
}

// CloseParentSlave records the first attempt, including ambiguous ErrClosed.
// Immediate post-child close and every rollback share this immutable result.
func (r *Resources) CloseParentSlave() error {
	r.slaveCloseOnce.Do(func() {
		if r.Slave != nil {
			r.slaveCloseErr = r.closeOwned(r.Slave)
		}
	})
	return r.slaveCloseErr
}
func closeFile(f *os.File) error { return f.Close() }

func (r *Resources) closeOwned(f *os.File) error {
	if r.close != nil {
		return r.close(f)
	}
	return closeFile(f)
}
