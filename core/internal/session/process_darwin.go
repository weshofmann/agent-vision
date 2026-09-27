//go:build darwin

package session

import (
	"context"
	"errors"
	"fmt"
	"os"
	"sync"
	"syscall"
	"time"

	"github.com/creack/pty"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
)

// Each spawn owns its injected boundary operations; production has no mutable hooks.
type DarwinSpawner struct{ ops *darwinSpawnOps }
type darwinSpawnOps struct {
	master   masterOpenOps
	slave    func(*os.File) (*os.File, error)
	termios  func(*os.File) error
	size     func(*os.File, uint16, uint16) error
	nonblock func(int) error
	start    func(SpawnConfig, *os.File) (Process, func(context.Context) error, error)
	close    func(*os.File) error
}

func nativeDarwinSpawnOps() darwinSpawnOps {
	return darwinSpawnOps{
		master: nativeMasterOpenOps(), slave: pty.OpenSlaveFromMaster, termios: configureV0Termios,
		size:     func(m *os.File, rows, cols uint16) error { return pty.Setsize(m, &pty.Winsize{Rows: rows, Cols: cols}) },
		nonblock: func(fd int) error { return syscall.SetNonblock(fd, true) }, start: startDarwinChild,
	}
}
func startDarwinChild(c SpawnConfig, s *os.File) (Process, func(context.Context) error, error) {
	shell, env := shellEnvironment(c)
	child, e := os.StartProcess(shell, []string{shell}, &os.ProcAttr{Dir: c.Cwd, Env: env, Files: []*os.File{s, s, s}, Sys: &syscall.SysProcAttr{Setsid: true, Setctty: true, Ctty: 0}})
	if child == nil {
		return nil, nil, e
	}
	// Capture and adopt a returned child before interpreting the accompanying error.
	pid := child.Pid
	if pid <= 0 {
		return nil, func(context.Context) error { return child.Release() }, errors.Join(e, errors.New("invalid started process identity"))
	}
	p := newOwnedProcess(pid, processOps{wait: func(pid int, status *syscall.WaitStatus) (int, error) {
		return syscall.Wait4(pid, status, syscall.WNOHANG, nil)
	}, signal: syscall.Kill, release: child.Release})
	return p, p.cleanup, e
}
func (d DarwinSpawner) Spawn(ctx context.Context, c SpawnConfig) (r *Resources, err error) {
	if e := ctx.Err(); e != nil {
		return nil, e
	}
	if c.Rows == 0 || c.Cols == 0 || c.Rows > 4096 || c.Cols > 4096 {
		return nil, errors.New("invalid terminal size")
	}
	ops := nativeDarwinSpawnOps()
	if d.ops != nil {
		ops = *d.ops
	}
	m, report, e := acquireDarwinMaster(ctx, ops.master)
	if m == nil {
		return nil, e
	}
	r = &Resources{Master: m, MasterFD: int(m.Fd()), close: ops.close, MasterOpen: report}
	defer func() {
		if err != nil && r.Process == nil {
			cleanupErr := r.Rollback(context.Background())
			err = errors.Join(err, cleanupErr)
			if cleanupErr == nil {
				r = nil
			}
		}
	}()
	if e != nil {
		return r, e
	}
	if e = ctx.Err(); e != nil {
		return r, e
	}
	s, e := ops.slave(m)
	r.Slave = s
	if e != nil {
		return r, e
	}
	if e = ctx.Err(); e != nil {
		return r, e
	}
	if s == nil {
		return r, errors.New("slave open returned no resource")
	}
	for _, configure := range []func() error{
		func() error { return ops.termios(s) }, func() error { return ops.size(m, c.Rows, c.Cols) }, func() error { return ops.nonblock(r.MasterFD) },
	} {
		if e = ctx.Err(); e != nil {
			return r, e
		}
		if e = configure(); e != nil {
			return r, e
		}
	}
	if e = ctx.Err(); e != nil {
		return r, e
	}
	child, cleanup, e := ops.start(c, s)
	r.Process = child
	r.rollback = cleanup
	if e != nil {
		return r, e
	}
	if child == nil {
		return r, errors.New("start returned no child")
	}
	if e = r.CloseParentSlave(); e != nil {
		return r, e
	}
	if e = ctx.Err(); e != nil {
		return r, e
	}
	return r, nil
}

type processOps struct {
	wait    func(int, *syscall.WaitStatus) (int, error)
	signal  func(int, syscall.Signal) error
	release func() error
}
type ownedProcess struct {
	ownedPID     int
	ops          processOps
	requests     chan struct{}
	mu           sync.Mutex
	hangup, kill bool
	done         chan ProcessResult
	complete     chan struct{}
	result       ProcessResult
}

func newOwnedProcess(pid int, ops processOps) *ownedProcess {
	p := &ownedProcess{ownedPID: pid, ops: ops, requests: make(chan struct{}, 1), done: make(chan ProcessResult, 1), complete: make(chan struct{})}
	go p.run()
	return p
}
func (p *ownedProcess) Done() <-chan ProcessResult { return p.done }
func (p *ownedProcess) RequestHangup()             { p.request(false) }
func (p *ownedProcess) RequestKill()               { p.request(true) }
func (p *ownedProcess) request(kill bool) {
	select {
	case <-p.complete:
		return
	default:
	}
	p.mu.Lock()
	if kill {
		p.kill = true
	} else {
		p.hangup = true
	}
	p.mu.Unlock()
	select {
	case p.requests <- struct{}{}:
	default:
	}
}
func (p *ownedProcess) run() {
	ticker := time.NewTicker(2 * time.Millisecond)
	defer ticker.Stop()
	hupSent, killSent := false, false
	var signalErr error
	finish := func(r ProcessResult) { p.result = r; p.done <- r; close(p.done); close(p.complete) }
	for {
		// Wait precedes each signal batch. EINTR retries without losing requests.
		var status syscall.WaitStatus
		pid, e := p.ops.wait(p.ownedPID, &status)
		if e == syscall.EINTR {
			continue
		}
		if e != nil {
			finish(ProcessResult{Status: protocol.ExitStatus{Kind: protocol.ExitUnavailable}, Err: errors.Join(fmt.Errorf("owned child wait: %w", e), signalErr, p.ops.release())})
			return
		}
		if pid == p.ownedPID {
			r := ProcessResult{Status: protocol.ExitStatus{Kind: protocol.ExitUnavailable}}
			switch {
			case status.Exited():
				r.Status = protocol.ExitStatus{Kind: protocol.ExitNormal, Value: uint32(status.ExitStatus())}
			case status.Signaled():
				r.Status = protocol.ExitStatus{Kind: protocol.ExitSignal, Value: uint32(status.Signal()), CoreDump: status.CoreDump()}
			default:
				r.Err = errors.New("nonterminal owned child status")
			}
			r.Err = errors.Join(r.Err, signalErr, p.ops.release())
			finish(r)
			return
		}
		if pid != 0 {
			finish(ProcessResult{Status: protocol.ExitStatus{Kind: protocol.ExitUnavailable}, Err: errors.Join(errors.New("unexpected wait identity"), p.ops.release())})
			return
		}
		p.mu.Lock()
		hup, kill := p.hangup, p.kill
		p.mu.Unlock()
		// No other waiter exists; a not-yet-reaped child/zombie keeps PID reserved.
		if hup && !hupSent {
			hupSent = true
			if e = p.ops.signal(p.ownedPID, syscall.SIGHUP); e != nil && e != syscall.ESRCH {
				signalErr = errors.Join(signalErr, e)
			}
		}
		if kill && !killSent {
			killSent = true
			if e = p.ops.signal(p.ownedPID, syscall.SIGKILL); e != nil && e != syscall.ESRCH {
				signalErr = errors.Join(signalErr, e)
			}
		}
		select {
		case <-ticker.C:
		case <-p.requests:
		}
	}
}
func (p *ownedProcess) cleanup(ctx context.Context) error {
	p.RequestHangup()
	timer := time.NewTimer(policy.Default().SignalGrace)
	defer timer.Stop()
	select {
	case <-p.complete:
		return p.result.Err
	case <-ctx.Done():
		return ctx.Err()
	case <-timer.C:
		p.RequestKill()
	}
	select {
	case <-p.complete:
		return p.result.Err
	case <-ctx.Done():
		return ctx.Err()
	}
}
