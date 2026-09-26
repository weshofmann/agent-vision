package server

import (
	"context"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"github.com/weshofmann/agent-vision/core/internal/session"
	"testing"
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
