package session

import (
	"context"
	"errors"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"io"
	"testing"
)

// Charging on send or crediting queued data breaks these literal ledger checks.
func TestCreditLedger(t *testing.T) {
	l := newDeliveryLedger(7, newOutputBudget())
	a, err := l.ReserveRead(1, 32768)
	if err != nil {
		t.Fatal(err)
	}
	if l.available() != 229376 {
		t.Fatal("read was not charged once")
	}
	if err = a.BeginWrite(); err != nil {
		t.Fatal(err)
	}
	if _, err = a.CommitWrite(); err != nil {
		t.Fatal(err)
	}
	d, e := l.ApplyCredit(3, 32768)
	if d != CreditApplied || e == nil || e.Request != 3 {
		t.Fatal("sent credit did not complete")
	}
	if l.available() != 262144 {
		t.Fatal("credit not returned exactly once")
	}
	d, _ = l.ApplyCredit(4, 1)
	if d != CreditRejected {
		t.Fatal("double return accepted")
	}
}
func TestQueuedUnsentCreditRejected(t *testing.T) {
	l := newDeliveryLedger(7, newOutputBudget())
	_, err := l.ReserveRead(1, 10)
	if err != nil {
		t.Fatal(err)
	}
	d, e := l.ApplyCredit(2, 10)
	if d != CreditRejected || e != nil {
		t.Fatal("queued bytes became eligible")
	}
}
func TestCreditBeforeWriteCommit(t *testing.T) {
	l := newDeliveryLedger(7, newOutputBudget())
	a, _ := l.ReserveRead(1, 10)
	if err := a.BeginWrite(); err != nil {
		t.Fatal(err)
	}
	d, e := l.ApplyCredit(2, 10)
	if d != CreditDeferred || e != nil {
		t.Fatal("peer-before-commit credit was not deferred")
	}
	d, _ = l.ApplyCredit(3, 1)
	if d != CreditRejected {
		t.Fatal("second pending credit accepted")
	}
	e, err := a.CommitWrite()
	if err != nil || e == nil || e.Request != 2 {
		t.Fatal("deferred reply missing")
	}
	if l.available() != 262144 {
		t.Fatal("deferred credit returned twice or not at all")
	}
	if _, err = a.CommitWrite(); err == nil {
		t.Fatal("duplicate commit succeeded")
	}
}
func TestPartialWriteAbortLedger(t *testing.T) {
	l := newDeliveryLedger(7, newOutputBudget())
	a, _ := l.ReserveRead(1, 10)
	a.BeginWrite()
	l.ApplyCredit(2, 10)
	a.FailWrite(errors.New("synthetic partial-frame failure"))
	if e, err := a.CommitWrite(); err == nil || e != nil {
		t.Fatal("failed frame fabricated completion")
	}
	if d, _ := l.ApplyCredit(3, 10); d != CreditRejected {
		t.Fatal("failed contact accepts credit")
	}
}
func TestOutputBudgetRetainsClosedQueues(t *testing.T) {
	b := newOutputBudget()
	var tickets []*OutputTicket
	for i := 0; i < 16; i++ {
		l := newDeliveryLedger(protocol.SessionID(i+1), b)
		for seq := uint64(1); seq <= 8; seq++ {
			a, e := l.ReserveRead(seq, 32768)
			if e != nil {
				t.Fatal(e)
			}
			tickets = append(tickets, a)
		}
	}
	fresh := newDeliveryLedger(99, b)
	if _, e := fresh.ReserveRead(1, 1); e == nil {
		t.Fatal("old queues escaped global budget")
	}
	tickets[0].BeginWrite()
	tickets[0].CommitWrite()
	if _, e := fresh.ReserveRead(1, 32768); e != nil {
		t.Fatal("completed payload did not release storage")
	}
}
func TestOutputChunkBound(t *testing.T) {
	l := newDeliveryLedger(1, newOutputBudget())
	for i := uint64(1); i <= 128; i++ {
		if _, e := l.ReserveRead(i, 1); e != nil {
			t.Fatal(e)
		}
	}
	if _, e := l.ReserveRead(129, 1); e == nil {
		t.Fatal("tiny chunks evade metadata bound")
	}
}

// One-byte syscall results must not conceal their 32KiB backing allocations.
func TestOutputPartialReadCapacityBound(t *testing.T) {
	l := newDeliveryLedger(1, newOutputBudget())
	for i := uint64(1); i <= 8; i++ {
		if !l.leaseRead(32768) {
			t.Fatal("early capacity rejection")
		}
		if _, e := l.reserveRead(i, 1, 32768, true); e != nil {
			t.Fatal(e)
		}
	}
	if l.leaseRead(1) {
		t.Fatal("tiny reads bypass session storage bound")
	}
}

func TestCreditDeferredDoesNotBlockManager(t *testing.T) {
	s := newSink()
	m := newFixtureManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
		return fixtureResources(newFixtureProcess()), nil
	}), s)
	t.Cleanup(func() { shutdown(t, m) })
	create(t, m, 1)
	a := completion(t, s, 1).Session
	m.mu.Lock()
	ledger := m.records[a].ledger
	m.mu.Unlock()
	ticket, err := ledger.ReserveRead(1, 10)
	if err != nil {
		t.Fatal(err)
	}
	ticket.BeginWrite()
	admitCommand(t, m, protocol.OutputCredit{RawBytes: 10}, 2, a)
	admitCommand(t, m, protocol.CloseSession{}, 3, a)
	create(t, m, 4)
	completion(t, s, 4)
	completion(t, s, 3)
	for _, e := range s.snapshot() {
		if e.Request == 2 {
			t.Fatal("deferred credit completed before full write")
		}
	}
	e, err := ticket.CommitWrite()
	if err != nil || e == nil {
		t.Fatal("held writing credit lost after Close")
	}
	s.Enqueue(*e)
	assertAck(t, completion(t, s, 2), protocol.TypeOutputCredit)
	onceCompletions(t, s, 2, 3, 4)
}

type prefixFailureWriter struct{ remaining int }

func (w *prefixFailureWriter) Write(b []byte) (int, error) {
	n := min(w.remaining, len(b))
	w.remaining -= n
	return n, io.ErrUnexpectedEOF
}
func TestPartialWriteAbortCleanup(t *testing.T) {
	s := newSink()
	m := newFixtureManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
		return fixtureResources(newFixtureProcess()), nil
	}), s)
	create(t, m, 1)
	id := completion(t, s, 1).Session
	m.mu.Lock()
	r := m.records[id]
	m.mu.Unlock()
	ticket, _ := r.ledger.ReserveRead(1, 10)
	ticket.BeginWrite()
	admitCommand(t, m, protocol.OutputCredit{RawBytes: 10}, 2, id)
	f := frame(t, protocol.OutputBytes{Sequence: 1, Bytes: []byte("0123456789")}, 0, id)
	err := protocol.WriteFrame(&prefixFailureWriter{remaining: 8}, f)
	if err == nil {
		t.Fatal("prefix failure did not fail frame")
	}
	ticket.FailWrite(err)
	m.Abort(err)
	waitGate(t, m.Done())
	for _, e := range s.snapshot() {
		if e.Request == 2 {
			t.Fatal("failed frame fabricated credit Ack")
		}
		switch e.Message.(type) {
		case protocol.SessionExited, protocol.SessionClosed:
			t.Fatal("contact loss fabricated lifecycle success")
		}
	}
	if e, err := ticket.CommitWrite(); err == nil || e != nil {
		t.Fatal("failed frame committed")
	}
}

func TestOutputReadWakeGeneration(t *testing.T) {
	l := newDeliveryLedger(1, newOutputBudget())
	for i := uint64(1); i <= 8; i++ {
		ticket, err := l.ReserveRead(i, 32768)
		if err != nil {
			t.Fatal(err)
		}
		ticket.BeginWrite()
		ticket.CommitWrite()
	}
	capacity, wake := l.readCapacity()
	if capacity != 0 {
		t.Fatal("full credit window still reads")
	}
	l.ApplyCredit(9, 1)
	select {
	case <-wake:
	default:
		t.Fatal("credit wake lost between capacity snapshot and wait")
	}
	if capacity, _ := l.readCapacity(); capacity != 1 {
		t.Fatal("returned credit unavailable")
	}
}
