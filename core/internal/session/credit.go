package session

import (
	"errors"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"sync"
	"time"
)

var errOutputLimit = errors.New("output capacity exhausted")
var errDeliveryState = errors.New("invalid output delivery state")

type CreditDisposition uint8

const (
	CreditApplied CreditDisposition = iota
	CreditDeferred
	CreditRejected
)

type ticketState uint8

const (
	ticketQueued ticketState = iota
	ticketWriting
	ticketSent
	ticketFailed
)

// outputBudget counts allocation capacity, including the sole reader's scratch,
// and metadata globally. Closed registry records do not release queued storage.
type outputBudget struct {
	mu            sync.Mutex
	bytes, chunks int
	changed       chan struct{}
}

func newOutputBudget() *outputBudget { return &outputBudget{changed: make(chan struct{})} }
func (b *outputBudget) reserve(n int) bool {
	b.mu.Lock()
	defer b.mu.Unlock()
	if n <= 0 || b.bytes+n > policy.AggregateOutputBytes || b.chunks >= policy.MaxSessions*policy.MaxOutputChunks {
		return false
	}
	b.bytes += n
	b.chunks++
	return true
}
func (b *outputBudget) release(n int) {
	b.mu.Lock()
	b.bytes -= n
	b.chunks--
	close(b.changed)
	b.changed = make(chan struct{})
	b.mu.Unlock()
}
func (b *outputBudget) wake() <-chan struct{} { b.mu.Lock(); defer b.mu.Unlock(); return b.changed }

// DeliveryLedger never holds its mutex across enqueue, descriptor I/O or joins.
// Credit charges successful read bytes once; memory capacity has separate ownership.
type DeliveryLedger struct {
	mu           sync.Mutex
	id           protocol.SessionID
	budget       *outputBudget
	allocated    int
	leased       bool
	retired      bool
	used, sent   int
	next         uint64
	tickets      []*OutputTicket
	writing      *OutputTicket
	pending      protocol.RequestID
	pendingBytes uint32
	failed       bool
	changed      chan struct{}
	progress     time.Time
}
type OutputTicket struct {
	ledger                *DeliveryLedger
	state                 ticketState
	remaining, allocation int
}

func newDeliveryLedger(id protocol.SessionID, b *outputBudget) *DeliveryLedger {
	return &DeliveryLedger{id: id, budget: b, changed: make(chan struct{}), tickets: make([]*OutputTicket, 0, policy.MaxOutputChunks)}
}
func (l *DeliveryLedger) notify() { close(l.changed); l.changed = make(chan struct{}) }
func (l *DeliveryLedger) available() int {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.availableLocked()
}
func (l *DeliveryLedger) availableLocked() int {
	if l.failed || l.retired || len(l.tickets) >= policy.MaxOutputChunks {
		return 0
	}
	return min(policy.OutputWindow-l.used, policy.OutputWindow-l.allocated)
}
func (l *DeliveryLedger) ReserveRead(seq uint64, n uint32) (*OutputTicket, error) {
	return l.reserveRead(seq, n, int(n), false)
}
func (l *DeliveryLedger) reserveRead(seq uint64, n uint32, allocation int, leased bool) (*OutputTicket, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	if n == 0 || n > 32768 || seq == 0 || seq != l.next+1 || (leased && (l.failed || len(l.tickets) >= policy.MaxOutputChunks || policy.OutputWindow-l.used < int(n))) || (!leased && l.availableLocked() < int(n)) {
		return nil, errOutputLimit
	}
	if !leased && !l.budget.reserve(allocation) {
		return nil, errOutputLimit
	}
	if !leased {
		l.allocated += allocation
	} else {
		l.leased = false
	}
	t := &OutputTicket{ledger: l, remaining: int(n), allocation: allocation}
	l.tickets = append(l.tickets, t)
	if l.used == 0 {
		l.progress = time.Now()
	}
	l.used += int(n)
	l.next = seq
	l.notify()
	return t, nil
}
func (t *OutputTicket) BeginWrite() error {
	l := t.ledger
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.failed || t.state != ticketQueued || l.writing != nil {
		return errDeliveryState
	}
	t.state = ticketWriting
	l.writing = t
	return nil
}
func (t *OutputTicket) CommitWrite() (*Event, error) {
	l := t.ledger
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.failed || t.state != ticketWriting || l.writing != t {
		return nil, errDeliveryState
	}
	t.state = ticketSent
	l.writing = nil
	l.sent += t.remaining
	l.budget.release(t.allocation)
	l.allocated -= t.allocation
	t.allocation = 0
	for i, a := range l.tickets {
		if a == t {
			copy(l.tickets[i:], l.tickets[i+1:])
			l.tickets[len(l.tickets)-1] = nil
			l.tickets = l.tickets[:len(l.tickets)-1]
			break
		}
	}
	var e *Event
	if l.pending != 0 {
		e = l.returnCredit(l.pending, l.pendingBytes)
		l.pending = 0
		l.pendingBytes = 0
	}
	l.notify()
	return e, nil
}

// FailWrite is contact loss, not a refund that permits further reads. The writer
// or scheduler must drop its payload references at this release cutpoint.
func (t *OutputTicket) FailWrite(_ error) {
	l := t.ledger
	l.mu.Lock()
	defer l.mu.Unlock()
	l.failed = true
	l.pending = 0
	l.pendingBytes = 0
	if t.allocation != 0 {
		l.budget.release(t.allocation)
		l.allocated -= t.allocation
		t.allocation = 0
	}
	t.state = ticketFailed
	if l.writing == t {
		l.writing = nil
	}
	for i, a := range l.tickets {
		if a == t {
			copy(l.tickets[i:], l.tickets[i+1:])
			l.tickets[len(l.tickets)-1] = nil
			l.tickets = l.tickets[:len(l.tickets)-1]
			break
		}
	}
	l.notify()
}
func (l *DeliveryLedger) ApplyCredit(req protocol.RequestID, n uint32) (CreditDisposition, *Event) {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.failed || req == 0 || n == 0 || l.pending != 0 {
		return CreditRejected, nil
	}
	if int(n) <= l.sent {
		e := l.returnCredit(req, n)
		l.notify()
		return CreditApplied, e
	}
	writing := 0
	if l.writing != nil {
		writing = l.writing.remaining
	}
	if int(n) > l.sent+writing {
		return CreditRejected, nil
	}
	l.pending = req
	l.pendingBytes = n
	return CreditDeferred, nil
}
func (l *DeliveryLedger) returnCredit(req protocol.RequestID, n uint32) *Event {
	l.sent -= int(n)
	l.used -= int(n)
	l.progress = time.Now()
	return &Event{Session: l.id, Request: req, Message: protocol.Ack{CompletedType: protocol.TypeOutputCredit}}
}

// leaseRead bounds the entire backing allocation before the syscall. Only the
// reader can lease; unused scratch is released without returning raw credit.
func (l *DeliveryLedger) leaseRead(n int) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.leased || n <= 0 || l.availableLocked() < n || !l.budget.reserve(n) {
		return false
	}
	l.leased = true
	l.allocated += n
	return true
}
func (l *DeliveryLedger) releaseRead(n int) {
	l.mu.Lock()
	l.leased = false
	l.allocated -= n
	l.budget.release(n)
	l.notify()
	l.mu.Unlock()
}
func (l *DeliveryLedger) retire() { l.mu.Lock(); l.retired = true; l.notify(); l.mu.Unlock() }

// StorageBytes exposes current backing capacity to the queue's independent
// allocation bound. The ticket remains opaque with respect to wire semantics.
func (t *OutputTicket) StorageBytes() int {
	l := t.ledger
	l.mu.Lock()
	defer l.mu.Unlock()
	return t.allocation
}

// Snapshot capacity and its wake generation together: credit arriving between
// the capacity check and wait must wake the reader rather than be lost.
func (l *DeliveryLedger) readCapacity() (int, <-chan struct{}) {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.availableLocked(), l.changed
}
