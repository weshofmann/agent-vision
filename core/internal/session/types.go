// Package session owns terminal resources and direct-child lifecycle.
package session

import (
	"context"
	"errors"
	"os"
	"sync"

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

// MasterFD is captured before nonblocking configuration. Users must cancel and
// join every FD worker before Rollback closes it; never call Master.Fd afterwards.
// Returned resources always belong to the caller, including a cancelled spawn.
type Resources struct {
	Master, Slave *os.File
	MasterFD      int
	Process       Process
	closeOnce     sync.Once
	closeErr      error
	rollback      func(context.Context) error
}

// Rollback is retryable after deadline expiry. A timeout never relinquishes child
// ownership or claims cleanup success. The owner continues reaping independently.
func (r *Resources) Rollback(ctx context.Context) error {
	r.closeOnce.Do(func() {
		if r.Slave != nil {
			r.closeErr = errors.Join(r.closeErr, closeFile(r.Slave))
		}
		if r.Master != nil {
			r.closeErr = errors.Join(r.closeErr, closeFile(r.Master))
		}
	})
	if r.rollback != nil {
		return errors.Join(r.closeErr, r.rollback(ctx))
	}
	return r.closeErr
}
func closeFile(f *os.File) error {
	e := f.Close()
	if errors.Is(e, os.ErrClosed) {
		return nil
	}
	return e
}
