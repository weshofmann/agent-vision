package session

import (
	"context"
	"errors"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"os"
	"sync"
	"testing"
	"time"
)

type fixtureProcess struct {
	done     chan ProcessResult
	complete chan struct{}
	once     sync.Once
	gate     func()
}

func newFixtureProcess() *fixtureProcess {
	return &fixtureProcess{done: make(chan ProcessResult, 1), complete: make(chan struct{})}
}
func (p *fixtureProcess) finish(s protocol.ExitStatus) {
	p.once.Do(func() { p.done <- ProcessResult{Status: s}; close(p.done); close(p.complete) })
}
func (p *fixtureProcess) Done() <-chan ProcessResult {
	if p.gate != nil {
		p.gate()
	}
	return p.done
}
func (p *fixtureProcess) RequestHangup() { p.finish(protocol.ExitStatus{Kind: 2, Value: 1}) }
func (p *fixtureProcess) RequestKill()   { p.finish(protocol.ExitStatus{Kind: 2, Value: 9}) }
func fixtureResources(p *fixtureProcess) *Resources {
	return &Resources{Process: p, rollback: func(ctx context.Context) error {
		p.RequestHangup()
		select {
		case <-p.complete:
			return nil
		case <-ctx.Done():
			return ctx.Err()
		}
	}}
}
func completedResources(s protocol.ExitStatus) *Resources {
	p := newFixtureProcess()
	p.finish(s)
	return fixtureResources(p)
}
func waitGate(t *testing.T, c <-chan struct{}) {
	t.Helper()
	select {
	case <-c:
	case <-time.After(time.Second):
		t.Fatal("barrier watchdog")
	}
}

// Catches lost resources returned after cancellation, Created after cancellation,
// and Ack preceding startup ownership completion.
func TestStartingShutdownBarriers(t *testing.T) { testStartingBarriers(t, false) }
func TestStartingEOFBarriers(t *testing.T)      { testStartingBarriers(t, true) }
func testStartingBarriers(t *testing.T, eof bool) {
	for _, stage := range []string{"before-open", "after-open", "after-spawn", "before-created", "after-created"} {
		t.Run(stage, func(t *testing.T) {
			entered, release := make(chan struct{}), make(chan struct{})
			cancelSeen := make(chan struct{})
			var r *Resources
			s := newSink()
			p := newFixtureProcess()
			sp := spawnFunc(func(ctx context.Context, _ SpawnConfig) (*Resources, error) {
				go func() { <-ctx.Done(); close(cancelSeen) }()
				if stage == "before-open" {
					close(entered)
					<-release
					if ctx.Err() != nil {
						return nil, ctx.Err()
					}
				}
				a, b, e := os.Pipe()
				if e != nil {
					return nil, e
				}
				r = &Resources{Master: a, Slave: b}
				if stage == "after-open" {
					close(entered)
					<-release
				}
				if stage != "after-open" {
					r.Process = p
					r.rollback = fixtureResources(p).rollback
				}
				if stage == "after-spawn" {
					close(entered)
					<-release
				}
				if stage == "before-created" {
					p.gate = func() { close(entered); <-release }
				}
				return r, nil
			})
			m := NewManager(policy.Default(), sp, s)
			create(t, m, 1)
			if stage == "after-created" {
				s.await(t, 1)
			} else {
				waitGate(t, entered)
			}
			stop := make(chan error, 1)
			if eof {
				m.Abort(errors.New("synthetic EOF"))
				go func() { stop <- m.Shutdown(context.Background()) }()
			} else {
				if e := m.Admit(frame(t, protocol.Shutdown{}, 2, 0)); e != nil {
					t.Fatal(e)
				}
				go func() { stop <- m.Shutdown(context.Background()) }()
			}
			waitGate(t, cancelSeen)
			if stage != "after-created" {
				select {
				case <-stop:
					t.Fatal("cleanup completed before startup joined")
				default:
				}
				close(release)
			}
			select {
			case e := <-stop:
				if e != nil {
					t.Fatal(e)
				}
			case <-time.After(time.Second):
				t.Fatal("cleanup watchdog")
			}
			if r != nil {
				if _, e := r.Master.Stat(); !errors.Is(e, os.ErrClosed) {
					t.Fatal("master leaked")
				}
				if _, e := r.Slave.Stat(); !errors.Is(e, os.ErrClosed) {
					t.Fatal("slave leaked")
				}
			}
			if r != nil && r.Process != nil {
				select {
				case <-p.complete:
				default:
					t.Fatal("child owner not joined")
				}
			}
			events := s.snapshot()
			if eof {
				want := 0
				if stage == "after-created" {
					want = 1
				}
				if len(events) != want {
					t.Fatalf("EOF events %+v", events)
				}
				return
			}
			if stage == "after-created" {
				if len(events) != 4 {
					t.Fatalf("published lifecycle %+v", events)
				}
				if _, ok := events[0].Message.(protocol.SessionCreated); !ok {
					t.Fatal("Created ordering")
				}
				if _, ok := events[1].Message.(protocol.SessionExited); !ok {
					t.Fatal("Exited ordering")
				}
				if _, ok := events[2].Message.(protocol.SessionClosed); !ok {
					t.Fatal("Closed ordering")
				}
			} else {
				if len(events) != 2 {
					t.Fatalf("unpublished events %+v", events)
				}
				v, ok := events[0].Message.(protocol.ErrorMessage)
				if !ok || v.Code != protocol.ErrorState || events[0].Request != 1 || events[0].Session != 0 {
					t.Fatalf("cancel completion %+v", events[0])
				}
			}
			if a, ok := events[len(events)-1].Message.(protocol.Ack); !ok || a.CompletedType != protocol.TypeShutdown {
				t.Fatal("Ack must follow ownership cleanup")
			}
		})
	}
}

// Catches forgetting rollback at any individual partial startup failure.
func TestStartupFailureRollback(t *testing.T) {
	for _, step := range []string{"open", "termios", "size", "spawn"} {
		t.Run(step, func(t *testing.T) {
			var r *Resources
			s := newSink()
			m := NewManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
				if step != "open" {
					a, b, e := os.Pipe()
					if e != nil {
						return nil, e
					}
					r = &Resources{Master: a, Slave: b}
				}
				return r, errors.New("synthetic " + step + " failure")
			}), s)
			create(t, m, 1)
			v := s.await(t, 1)
			e, ok := v[0].Message.(protocol.ErrorMessage)
			if !ok || e.Code != protocol.ErrorSpawn || v[0].Session != 0 {
				t.Fatalf("failed start response %+v", v)
			}
			if r != nil {
				if _, e := r.Master.Stat(); !errors.Is(e, os.ErrClosed) {
					t.Fatal("failed master leaked")
				}
				if _, e := r.Slave.Stat(); !errors.Is(e, os.ErrClosed) {
					t.Fatal("failed slave leaked")
				}
			}
			shutdown(t, m)
		})
	}
}

// Catches a failed spawn selecting SPAWN before a later cancellation boundary.
func TestStartingCancellationDuringRollback(t *testing.T) {
	entered, release := make(chan struct{}), make(chan struct{})
	s := newSink()
	var once sync.Once
	m := NewManager(policy.Default(), spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) {
		return &Resources{rollback: func(context.Context) error { once.Do(func() { close(entered) }); <-release; return nil }}, errors.New("synthetic spawn failure")
	}), s)
	create(t, m, 1)
	waitGate(t, entered)
	if e := m.Admit(frame(t, protocol.Shutdown{}, 2, 0)); e != nil {
		t.Fatal(e)
	}
	close(release)
	shutdown(t, m)
	e := s.snapshot()
	v, ok := e[0].Message.(protocol.ErrorMessage)
	if !ok || v.Code != protocol.ErrorState {
		t.Fatalf("cancellation lost during rollback: %+v", e)
	}
}
