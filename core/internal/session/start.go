package session

import (
	"context"
	"errors"

	"github.com/weshofmann/agent-vision/core/internal/protocol"
)

func (m *Manager) start(r *reservation, window uint32) {
	defer close(r.startDone)
	resources, err := m.spawner.Spawn(r.ctx, r.config)
	r.resources = resources // adopt even resources returned with error/cancellation
	var done <-chan ProcessResult
	if resources != nil && resources.Process != nil {
		// Capture once outside the admission lock. Unpublished rollback consumes
		// only the owner's separate immutable completion, never this channel.
		done = resources.Process.Done()
	}
	m.mu.Lock()
	if err == nil && resources != nil && resources.Process != nil && r.ctx.Err() == nil && !m.stopping {
		r.commandWake = make(chan struct{}, 1)
		r.commandCtx, r.commandCancel = context.WithCancel(r.ctx)
		r.ledger = newDeliveryLedger(r.id, m.outputBudget)
		r.sealed = make(chan struct{})
		r.naturalDone = make(chan struct{})
		r.monitorDone = make(chan struct{})
		m.monitorWorkers.Add(1)
		r.ioWorkers.Add(2) // registration before Created and startDone publication
		r.published = true
		r.state = live
		m.emitLocked(Event{Session: r.id, Request: r.request, Message: protocol.SessionCreated{Rows: r.config.Rows, Cols: r.config.Cols, AcceptedWindow: window}})
		m.mu.Unlock()
		go m.runCommands(r, resources.MasterFD)
		go m.observe(r, done)
		go m.finishNatural(r)
		go m.runOutput(r, resources.MasterFD)
		go m.watchCredit(r)
		// Reader starts only after Created admission.
		return
	}
	m.mu.Unlock()
	close(r.observed) // unpublished: no public process-result consumer
	// Startup completion includes rollback. Shutdown must join startDone before
	// acknowledging; closing descriptors requires joining all future FD workers.
	ctx, cancel := context.WithTimeout(context.Background(), m.policy.CleanupTimeout)
	defer cancel()
	var cleanupErr error
	if resources != nil {
		cleanupErr = resources.Rollback(ctx)
	}
	if cleanupErr == nil {
		select {
		case <-r.observed:
		case <-ctx.Done():
			cleanupErr = ctx.Err()
		}
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	code := protocol.ErrorSpawn
	if r.ctx.Err() != nil || m.stopping {
		code = protocol.ErrorState
	}
	m.emitLocked(Event{Request: r.request, Message: protocol.ErrorMessage{Code: code, Message: "session startup failed"}})
	if cleanupErr == nil {
		delete(m.records, r.id)
	}
	// Preserve uncertainty and ownership; cleanup retries Rollback on shutdown.
}

// cleanup is a per-reservation completion barrier shared by Close and shutdown.
// Tasks 4/5 must cancel/join every captured-FD worker here BEFORE Rollback.
func (m *Manager) cleanup(ctx context.Context, r *reservation) error {
	r.cleanupMu.Lock()
	if r.cleanupComplete {
		r.cleanupMu.Unlock()
		return nil
	}
	if !r.cleanupRunning {
		r.cleanupRunning = true
		r.cleanupDone = make(chan struct{})
		go func() {
			err := m.cleanupAttempt(ctx, r)
			r.cleanupMu.Lock()
			r.cleanupErr = err
			r.cleanupComplete = err == nil
			r.cleanupRunning = false
			close(r.cleanupDone)
			r.cleanupMu.Unlock()
		}()
	}
	done := r.cleanupDone
	r.cleanupMu.Unlock()
	select {
	case <-done:
		r.cleanupMu.Lock()
		err := r.cleanupErr
		r.cleanupMu.Unlock()
		return err
	case <-ctx.Done():
		return ctx.Err()
	}
}

func (m *Manager) cleanupAttempt(ctx context.Context, r *reservation) (err error) {
	select {
	case <-r.startDone:
	case <-ctx.Done():
		err = ctx.Err()
	}
	if err == nil {
		// Registration ends before startDone. Cancellation has already been sent.
		// A stuck join keeps this owner reachable; callers still have their watchdog.
		r.ioWorkers.Wait()
		if r.resources != nil {
			err = m.rollbackJoined(ctx, r)
		}
		if err == nil {
			select {
			case <-r.observed:
				if r.published {
					select {
					case <-r.naturalDone:
					case <-ctx.Done():
						err = ctx.Err()
					}
				}
			case <-ctx.Done():
				err = ctx.Err()
			}
		}
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	err = errors.Join(err, r.result.Err)
	if err != nil {
		if r.closeRequest != 0 {
			m.emitLocked(Event{Session: r.id, Request: r.closeRequest, Message: protocol.ErrorMessage{Code: protocol.ErrorStatusUnavailable, Message: "session cleanup failed"}})
			r.closeRequest = 0 // one terminal response; a later cleanup Closed is async
		}
		return err
	}
	if r.published {
		m.publishExitLocked(r)
		m.emitLocked(Event{Session: r.id, Request: r.closeRequest, Message: protocol.SessionClosed{LastOutputSequence: r.sequence}})
	}
	if r.ledger != nil {
		r.ledger.retire()
	}
	delete(m.records, r.id)
	return nil
}

// observe retains immutable status; Task5 owns the drain/seal publication step.
func (m *Manager) observe(r *reservation, done <-chan ProcessResult) {
	v, ok := <-done
	if !ok {
		v = ProcessResult{Status: protocol.ExitStatus{Kind: protocol.ExitUnavailable}, Err: errors.New("missing process result")}
	}
	m.mu.Lock()
	r.result = v
	r.exitAt = m.now()
	if r.readWaitCancel != nil {
		r.readWaitCancel()
	}
	r.commandCancel() // end command I/O; lifetime context remains available for Task5 drain
	if r.state == live {
		r.state = draining
	}
	m.mu.Unlock()
	close(r.observed)
}

// Resource completion is serialized for natural seal and explicit cleanup. A
// deadline preserves the same retryable owner; FD workers join before any close.
func (m *Manager) rollbackJoined(ctx context.Context, r *reservation) error {
	r.resourceCleanupMu.Lock()
	defer r.resourceCleanupMu.Unlock()
	return r.resources.Rollback(ctx)
}
