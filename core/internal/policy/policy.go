// Package policy names tunable implementation bounds separately from v1 limits.
package policy

import "time"

// V1 resource limits are part of the negotiated contract, not timing policy.
const (
	MaxSessions          = 16
	OrdinarySlots        = 48
	CreditSlots          = 16
	CloseSlots           = 16
	ShutdownSlots        = 1
	OutputWindow         = 256 * 1024
	AggregateOutputBytes = 4 * 1024 * 1024
	ControlReserveBytes  = 128 * 1024
	InputBytesPerSession = 64 * 1024
)

// MaxOutputChunks caps allocation/metadata overhead in addition to raw bytes.
// It includes queued and in-flight output tickets and is local policy.
const MaxOutputChunks = 128

// Policy holds implementation timings; changing these does not change v1 schemas.
type Policy struct {
	DrainBytes       int
	DrainTime        time.Duration
	HandshakeTimeout time.Duration
	FrameTimeout     time.Duration
	WriteTimeout     time.Duration
	CreditTimeout    time.Duration
	CleanupTimeout   time.Duration
	InputTimeout     time.Duration
	CreditFlush      time.Duration
	WorkerJoinTarget time.Duration
	SignalGrace      time.Duration
	CoreReapTimeout  time.Duration
}

func Default() Policy {
	return Policy{
		DrainBytes: 64 * 1024, DrainTime: 100 * time.Millisecond,
		HandshakeTimeout: 2 * time.Second, FrameTimeout: 2 * time.Second,
		WriteTimeout: 2 * time.Second, CreditTimeout: 2 * time.Second, CleanupTimeout: 2 * time.Second,
		InputTimeout: time.Second, CreditFlush: 100 * time.Millisecond, WorkerJoinTarget: 100 * time.Millisecond,
		SignalGrace: 250 * time.Millisecond, CoreReapTimeout: time.Second,
	}
}
