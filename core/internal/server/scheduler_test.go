package server

import (
	"context"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"github.com/weshofmann/agent-vision/core/internal/session"
	"testing"
	"unsafe"
)

func enqueue(t *testing.T, s *Scheduler, e session.Event) {
	t.Helper()
	if err := s.Enqueue(e); err != nil {
		t.Fatal(err)
	}
}
func next(t *testing.T, s *Scheduler) session.Event {
	t.Helper()
	e, err := s.Next(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	s.Complete(e)
	return e
}

// A chatty A cannot starve B; terminal records cannot overtake their own output.
func TestOutputFairness(t *testing.T) {
	s := NewScheduler()
	for _, id := range []protocol.SessionID{1, 2} {
		enqueue(t, s, session.Event{Session: id, Request: protocol.RequestID(id), Message: protocol.SessionCreated{Rows: 24, Cols: 80, AcceptedWindow: 262144}})
		for seq := uint64(1); seq <= 3; seq++ {
			enqueue(t, s, session.Event{Session: id, Message: protocol.OutputBytes{Sequence: seq, Bytes: []byte("x")}})
		}
		enqueue(t, s, session.Event{Session: id, Message: protocol.SessionExited{Status: protocol.ExitStatus{Kind: 1}, LastOutputSequence: 3, DrainReason: 1}})
	}
	for i := 0; i < 10; i++ {
		e := next(t, s)
		want := protocol.SessionID(i%2 + 1)
		if e.Session != want {
			t.Fatalf("B starved at %d: %d", i, e.Session)
		}
		if i < 2 {
			if _, ok := e.Message.(protocol.SessionCreated); !ok {
				t.Fatal("Created overtaken")
			}
		}
		if i >= 8 {
			if _, ok := e.Message.(protocol.SessionExited); !ok {
				t.Fatal("Exited overtook output")
			}
		}
	}
}
func TestOutputShutdownBarrier(t *testing.T) {
	s := NewScheduler()
	enqueue(t, s, session.Event{Session: 1, Message: protocol.OutputBytes{Sequence: 1, Bytes: []byte("a")}})
	enqueue(t, s, session.Event{Session: 1, Message: protocol.SessionClosed{LastOutputSequence: 1}})
	enqueue(t, s, session.Event{Request: 9, Message: protocol.Ack{CompletedType: protocol.TypeShutdown}})
	for i := 0; i < 2; i++ {
		if next(t, s).Session != 1 {
			t.Fatal("shutdown Ack strands terminal queue")
		}
	}
	if next(t, s).Request != 9 {
		t.Fatal("shutdown completion missing")
	}
}
func TestOutputControlReserveAndMetadata(t *testing.T) {
	s := NewScheduler()
	for i := 0; i < 2048; i++ {
		enqueue(t, s, session.Event{Session: 1, Message: protocol.OutputBytes{Sequence: uint64(i + 1), Bytes: []byte{1}}})
	}
	if s.Enqueue(session.Event{Session: 1, Message: protocol.OutputBytes{Sequence: 2049, Bytes: []byte{1}}}) == nil {
		t.Fatal("output metadata unbounded")
	}
	enqueue(t, s, session.Event{Session: 2, Request: 3, Message: protocol.Ack{CompletedType: protocol.TypeOutputCredit}})
	if next(t, s).Session != 1 || next(t, s).Session != 2 {
		t.Fatal("reserved control starved")
	}
}
func TestOutputControlByteBound(t *testing.T) {
	s := NewScheduler()
	body := ""
	for i := 0; i < 1024; i++ {
		body += "x"
	}
	for i := 0; i < 123; i++ {
		enqueue(t, s, session.Event{Session: 1, Request: protocol.RequestID(i + 1), Message: protocol.ErrorMessage{Code: protocol.ErrorState, Message: body}})
	}
	if s.Enqueue(session.Event{Session: 1, Request: 124, Message: protocol.ErrorMessage{Code: protocol.ErrorState, Message: body}}) == nil {
		t.Fatal("control reserve exceeds128KiB")
	}
	enqueue(t, s, session.Event{Session: 2, Request: 125, Message: protocol.Ack{CompletedType: protocol.TypeOutputCredit}})
}

// Repeatedly growing then draining new queues while refilling nonempty old
// queues must release historical slice capacity. These IDs include already
// Closed records with queued Credit completions; registry removal is irrelevant
// to scheduler ownership. The public API workload is not a wire-client claim.
func TestOutputRetainedSchedulerCapacity(t *testing.T) {
	s := NewScheduler()
	request := protocol.RequestID(1)
	last := make(map[protocol.SessionID]protocol.RequestID)
	add := func(id protocol.SessionID, closed bool) {
		t.Helper()
		var message protocol.Message = protocol.Ack{CompletedType: protocol.TypeOutputCredit}
		if closed {
			message = protocol.SessionClosed{}
		}
		enqueue(t, s, session.Event{Session: id, Request: request, Message: message})
		request++
	}
	take := func() session.Event {
		t.Helper()
		e := next(t, s)
		if e.Request <= last[e.Session] {
			t.Fatalf("queue%d FIFO reversed: request%d after%d", e.Session, e.Request, last[e.Session])
		}
		last[e.Session] = e.Request
		return e
	}
	for id := protocol.SessionID(1); id <= 128; id++ {
		for s.controls < 510 {
			add(id, false)
		}
		add(id, true)
		for {
			remaining := 0
			for _, q := range s.queues {
				if q.id == id {
					remaining = len(q.items)
				}
			}
			if remaining <= 2 {
				break
			}
			e := take()
			if e.Session != id {
				add(e.Session, false)
			}
		}
	}
	slots, queued := 0, 0
	for _, q := range s.queues {
		slots += cap(q.items)
		queued += len(q.items)
	}
	t.Logf("repeated grow/drain/refill: queues=%d controls=%d control-bytes=%d retained-event-slots=%d descriptor-slots=%d", len(s.queues), s.controls, s.controlBytes, slots, cap(s.queues))
	if len(s.queues) != 128 || queued != 256 || s.controls != 256 {
		t.Fatal("workload did not retain128 nonempty old queues")
	}
	// Hand-derived limit:2048 output +512 control nodes, at most2 retained slots
	// per queued event. This distinguishes the old71648-slot high-water behavior.
	if slots > 5120 {
		t.Fatalf("historical metadata escaped global retained bound: %d slots >5120", slots)
	}
	for _, q := range s.queues {
		if cap(q.items) > 2*len(q.items) {
			t.Fatalf("queue%d retained%d slots for%d events", q.id, cap(q.items), len(q.items))
		}
	}
	for s.controls > 0 {
		take()
	}
	if len(s.queues) != 0 || cap(s.queues) != 0 {
		t.Fatal("fully drained descriptor allocation retained")
	}
}

// The sole in-flight event is still charged even when its queue becomes empty;
// compaction must not alter writer ownership or retain an empty backing array.
func TestOutputCapacityIncludesInFlight(t *testing.T) {
	s := NewScheduler()
	for i := 0; i < 512; i++ {
		enqueue(t, s, session.Event{Session: 1, Request: protocol.RequestID(i + 1), Message: protocol.Ack{CompletedType: protocol.TypeInputBytes}})
	}
	for i := 0; i < 511; i++ {
		next(t, s)
	}
	e, err := s.Next(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if s.controls != 1 || s.controlBytes != 34 || s.inflight == nil {
		t.Fatal("dequeue released in-flight control ownership")
	}
	if cap(s.queues[0].items) != 0 {
		t.Fatalf("empty in-flight queue retains%d metadata slots", cap(s.queues[0].items))
	}
	s.Complete(e)
	if s.controls != 0 || s.controlBytes != 0 || cap(s.queues) != 0 {
		t.Fatal("completed in-flight metadata retained")
	}
}

// Outer descriptor arrays must also release their historical peak while old
// queues remain nonempty. The maximum node workload includes both reserves.
func TestOutputRetainedDescriptorCapacity(t *testing.T) {
	s := NewScheduler()
	for id := protocol.SessionID(1); id <= 2560; id++ {
		var message protocol.Message = protocol.OutputBytes{Sequence: 1, Bytes: []byte{1}}
		request := protocol.RequestID(0)
		if id > 2048 {
			message = protocol.Ack{CompletedType: protocol.TypeOutputCredit}
			request = protocol.RequestID(id)
		}
		enqueue(t, s, session.Event{Session: id, Request: request, Message: message})
	}
	peak := cap(s.queues)
	for i := 0; i < 2432; i++ {
		next(t, s)
	}
	if len(s.queues) != 128 || s.controls != 128 || s.outputs != 0 {
		t.Fatal("workload did not retain128 old queues")
	}
	if cap(s.queues) > 256 {
		t.Fatalf("retained descriptor high-water: %d slots for128 queues", cap(s.queues))
	}
	slots := 0
	for _, q := range s.queues {
		slots += cap(q.items)
	}
	t.Logf("descriptor high-water%d ->%d; retained metadata%d bytes, scheduled%d descriptor%d", peak, cap(s.queues), uintptr(slots)*unsafe.Sizeof(scheduled{})+uintptr(cap(s.queues))*unsafe.Sizeof(eventQueue{}), unsafe.Sizeof(scheduled{}), unsafe.Sizeof(eventQueue{}))
	s.Close(nil)
	if cap(s.queues) != 0 {
		t.Fatal("Close retained descriptor allocation")
	}
}

func TestHandshakeScheduler(t *testing.T) {
	s := NewScheduler()
	if err := s.Enqueue(session.Event{Request: 1, Message: protocol.HelloAck{SelectedMajor: 1, MaxPayload: 65536, Epoch: [16]byte{1}}}); err != nil {
		t.Fatalf("structurally valid handshake rejected: %v", err)
	}
}
