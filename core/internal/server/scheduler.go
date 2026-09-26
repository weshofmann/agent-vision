package server

import (
	"context"
	"errors"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"github.com/weshofmann/agent-vision/core/internal/session"
	"sync"
)

var ErrSchedulerFull = errors.New("scheduler capacity exhausted")
var ErrSchedulerClosed = errors.New("scheduler closed")

// Queue nodes and payloads stay charged while Next lends one event to the sole
// writer. Complete releases control storage; output tickets release raw storage
// at CommitWrite/FailWrite. No scheduler method performs socket I/O.
type scheduled struct {
	event   session.Event
	ordinal uint64
	bytes   int
	output  bool
	barrier bool
}
type eventQueue struct {
	id    protocol.SessionID
	items []scheduled
}

// compactItems releases historical high-water capacity without reallocating on
// every dequeue. At each API boundary retained slots are <=2*queued events,
// including zero slots for an empty queue whose last item is lent to the writer.
func (q *eventQueue) compactItems() {
	if cap(q.items) <= 2*len(q.items) {
		return
	}
	items := make([]scheduled, len(q.items))
	copy(items, q.items)
	q.items = items
}

type Scheduler struct {
	mu                                           sync.Mutex
	queues                                       []eventQueue
	cursor                                       int
	next                                         uint64
	outputBytes, outputs, controlBytes, controls int
	inflight                                     *scheduled
	changed                                      chan struct{}
	closed                                       error
}

const maxControls = 512

func NewScheduler() *Scheduler { return &Scheduler{changed: make(chan struct{})} }
func (s *Scheduler) notify()   { close(s.changed); s.changed = make(chan struct{}) }

// Descriptor capacity obeys the same hysteresis. Each remaining queue owns at
// least one queued or in-flight item, so global node policy bounds both slices.
// Compaction retains payload references, ordering and cursor identities exactly;
// one temporary metadata copy is separate from the retained-capacity bound.
// The 2,048 output +512 control node limits permit at most5,120 retained slots
// in each slice category:480KiB for 64-byte items and32-byte descriptors on the
// qualified target, plus the sole in-flight item and allocator rounding.
func (s *Scheduler) compactQueues() {
	if cap(s.queues) <= 2*len(s.queues) {
		return
	}
	queues := make([]eventQueue, len(s.queues))
	copy(queues, s.queues)
	s.queues = queues
}
func (s *Scheduler) Enqueue(e session.Event) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.closed != nil {
		return s.closed
	}
	item := scheduled{event: e, ordinal: s.next + 1}
	if o, ok := e.Message.(protocol.OutputBytes); ok {
		if len(o.Bytes) == 0 || len(o.Bytes) > 32768 || cap(o.Bytes) > 32768 {
			return ErrSchedulerFull
		}
		item.output = true
		item.bytes = cap(o.Bytes)
		if e.Output != nil {
			item.bytes = e.Output.StorageBytes()
			if item.bytes < len(o.Bytes) {
				return ErrSchedulerFull
			}
		}
		if s.outputs >= policy.MaxSessions*policy.MaxOutputChunks || s.outputBytes+item.bytes > policy.AggregateOutputBytes {
			return ErrSchedulerFull
		}
	} else {
		// Encode once only for schema/size validation; control payloads are bounded
		// and the temporary validation copy is at most one v1 frame (64 KiB).
		frame, err := protocol.Encode(e.Message, e.Request, e.Session, 1)
		if err != nil {
			return err
		}
		item.bytes = protocol.HeaderSize + len(frame.Body)
		if s.controls >= maxControls || s.controlBytes+item.bytes > policy.ControlReserveBytes {
			return ErrSchedulerFull
		}
		if a, ok := e.Message.(protocol.Ack); ok && a.CompletedType == protocol.TypeShutdown {
			item.barrier = true
		}
		if e.Session == 0 {
			if _, ok := e.Message.(protocol.ErrorMessage); ok {
				item.barrier = true
			}
		}
	}
	s.next++
	if item.output {
		s.outputs++
		s.outputBytes += item.bytes
	} else {
		s.controls++
		s.controlBytes += item.bytes
	}
	for i := range s.queues {
		if s.queues[i].id == e.Session {
			s.queues[i].items = append(s.queues[i].items, item)
			s.queues[i].compactItems()
			s.notify()
			return nil
		}
	}
	s.queues = append(s.queues, eventQueue{id: e.Session, items: []scheduled{item}})
	s.compactQueues()
	s.notify()
	return nil
}

// Per-session FIFO plus round robin supplies fairness without a reply priority
// rule. A Shutdown completion waits behind every previously admitted event.
func (s *Scheduler) Next(ctx context.Context) (session.Event, error) {
	for {
		s.mu.Lock()
		if s.closed != nil {
			err := s.closed
			s.mu.Unlock()
			return session.Event{}, err
		}
		if s.inflight != nil {
			s.mu.Unlock()
			return session.Event{}, errors.New("concurrent scheduler writer")
		}
		n := len(s.queues)
		for step := 0; step < n; step++ {
			i := (s.cursor + step) % n
			q := &s.queues[i]
			if len(q.items) == 0 {
				continue
			}
			item := q.items[0]
			if item.barrier {
				older := false
				for _, other := range s.queues {
					if len(other.items) > 0 && other.items[0].ordinal < item.ordinal {
						older = true
						break
					}
				}
				if older {
					continue
				}
			}
			copy(q.items, q.items[1:])
			q.items[len(q.items)-1] = scheduled{}
			q.items = q.items[:len(q.items)-1]
			q.compactItems()
			s.inflight = &item
			s.cursor = (i + 1) % n
			s.mu.Unlock()
			return item.event, nil
		}
		wake := s.changed
		s.mu.Unlock()
		select {
		case <-wake:
		case <-ctx.Done():
			return session.Event{}, ctx.Err()
		}
	}
}
func (s *Scheduler) Complete(e session.Event) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.inflight == nil {
		return
	}
	item := s.inflight
	if item.event.Session != e.Session || item.event.Request != e.Request || item.event.Output != e.Output {
		return
	}
	if item.output {
		s.outputs--
		s.outputBytes -= item.bytes
	} else {
		s.controls--
		s.controlBytes -= item.bytes
	}
	s.inflight = nil
	// Empty retired queues cannot accumulate across repeated create/close cycles.
	for i := len(s.queues) - 1; i >= 0; i-- {
		if len(s.queues[i].items) == 0 {
			copy(s.queues[i:], s.queues[i+1:])
			s.queues[len(s.queues)-1] = eventQueue{}
			s.queues = s.queues[:len(s.queues)-1]
			if i < s.cursor {
				s.cursor--
			}
		}
	}
	s.compactQueues()
	if len(s.queues) > 0 {
		s.cursor %= len(s.queues)
	} else {
		s.cursor = 0
	}
	s.notify()
}

// Close drops owned queue references before invalidating tickets. Callers also
// FailWrite/drop the writer's current frame, then Abort the manager separately.
func (s *Scheduler) Close(err error) {
	if err == nil {
		err = ErrSchedulerClosed
	}
	s.mu.Lock()
	if s.closed != nil {
		s.mu.Unlock()
		return
	}
	s.closed = err
	queues := s.queues
	s.queues = nil
	s.notify()
	s.mu.Unlock()
	for _, q := range queues {
		for i := range q.items {
			ticket := q.items[i].event.Output
			q.items[i] = scheduled{}
			if ticket != nil {
				ticket.FailWrite(err)
			}
		}
	}
}
