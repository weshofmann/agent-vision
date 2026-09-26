package session

import (
	"context"
	"errors"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"io"
	"syscall"
	"time"
)

type outputIO interface {
	Read(int, []byte) (int, error)
	Wait(context.Context, int) error
}

// One reader owns successful payloads, sequence and seal. Wait observation wakes
// readiness, but cannot publish exit independently of an in-flight read result.
func (m *Manager) runOutput(r *reservation, fd int) {
	defer r.ioWorkers.Done()
	defer close(r.sealed)
	reason := protocol.DrainExplicitClose
	drained := 0
	defer func() { m.mu.Lock(); r.drainReason = reason; m.mu.Unlock() }()
	for {
		m.mu.Lock()
		observed := !r.exitAt.IsZero()
		since := r.exitAt
		cancelled := r.ctx.Err() != nil
		m.mu.Unlock()
		if cancelled {
			reason = protocol.DrainExplicitClose
			return
		}
		if observed {
			if drained >= m.policy.DrainBytes {
				reason = protocol.DrainByteCap
				return
			}
			if m.now().Sub(since) >= m.policy.DrainTime {
				reason = protocol.DrainTimeCap
				return
			}
		}
		available, ledgerWake := r.ledger.readCapacity()
		capacity := min(32768, available)
		if observed {
			capacity = min(capacity, m.policy.DrainBytes-drained)
		}
		budgetWake := m.outputBudget.wake()
		if capacity == 0 || !r.ledger.leaseRead(capacity) {
			if observed {
				reason = protocol.DrainCreditCap
				return
			}
			select {
			case <-ledgerWake:
			case <-budgetWake:
			case <-r.observed:
			case <-r.ctx.Done():
			}
			continue
		}
		// Allocation capacity, not only the returned length, stays charged through
		// writer completion. No second payload copy is made by reader or scheduler.
		b := make([]byte, capacity)
		n, err := m.outputIO.Read(fd, b)
		if n < 0 || n > len(b) {
			n = 0
			err = errors.New("invalid PTY read count")
		}
		if n > 0 {
			m.mu.Lock()
			seq := r.sequence + 1
			ticket, e := r.ledger.reserveRead(seq, uint32(n), capacity, true)
			if e == nil {
				r.sequence = seq
				m.emitLocked(Event{Session: r.id, Message: protocol.OutputBytes{Sequence: seq, Bytes: b[:n:n]}, Output: ticket})
			}
			nowObserved := !r.exitAt.IsZero()
			m.mu.Unlock()
			if e != nil {
				r.ledger.releaseRead(capacity)
				m.Abort(e)
				return
			}
			if nowObserved {
				drained += n
			}
		} else {
			r.ledger.releaseRead(capacity)
		}
		if err == syscall.EINTR {
			continue
		}
		if err != nil && err != syscall.EAGAIN && err != syscall.EWOULDBLOCK {
			if errors.Is(err, io.EOF) || err == syscall.EIO {
				reason = protocol.DrainEOF
			} else {
				reason = protocol.DrainIOError
				m.mu.Lock()
				m.emitLocked(Event{Session: r.id, Message: protocol.ErrorMessage{Code: protocol.ErrorPTYIO, Message: "PTY read failed"}})
				m.mu.Unlock()
			}
			m.outputEnded(r, reason == protocol.DrainIOError)
			return
		}
		if n == 0 {
			m.mu.Lock()
			observed = !r.exitAt.IsZero()
			if observed {
				m.mu.Unlock()
				reason = protocol.DrainNoData
				return
			}
			waitCtx, cancel := context.WithCancel(r.ctx)
			r.readWaitCancel = cancel
			m.mu.Unlock()
			e := m.outputIO.Wait(waitCtx, fd)
			waitErr := waitCtx.Err()
			cancel()
			m.mu.Lock()
			r.readWaitCancel = nil
			m.mu.Unlock()
			if e != nil && waitErr == nil {
				reason = protocol.DrainIOError
				m.mu.Lock()
				m.emitLocked(Event{Session: r.id, Message: protocol.ErrorMessage{Code: protocol.ErrorPTYIO, Message: "PTY readiness failed"}})
				m.mu.Unlock()
				m.outputEnded(r, true)
				return
			}
		}
	}
}
func (m *Manager) outputEnded(r *reservation, fatal bool) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if r.state == closing {
		return
	}
	if fatal {
		r.state = closing
		r.cancel()
		go m.closeSession(r)
	} else if r.exitAt.IsZero() {
		r.state = draining
		r.cancel()
	}
}
func (m *Manager) watchCredit(r *reservation) {
	defer m.monitorWorkers.Done()
	defer close(r.monitorDone)
	defer func() { m.mu.Lock(); m.monitorCount--; m.mu.Unlock() }()
	for {
		l := r.ledger
		l.mu.Lock()
		wake := l.changed
		used := l.used
		deadline := l.progress.Add(m.policy.CreditTimeout)
		failed := l.failed || (l.retired && len(l.tickets) == 0)
		l.mu.Unlock()
		if failed {
			return
		}
		if used == 0 {
			select {
			case <-wake:
			case <-m.creditCtx.Done():
				return
			}
			continue
		}
		timer := time.NewTimer(time.Until(deadline))
		select {
		case <-wake:
			timer.Stop()
		case <-m.creditCtx.Done():
			timer.Stop()
			return
		case <-timer.C:
			l.mu.Lock()
			stalled := l.used > 0 && !time.Now().Before(l.progress.Add(m.policy.CreditTimeout))
			l.mu.Unlock()
			if stalled {
				m.Abort(errors.New("output credit stalled"))
				return
			}
		}
	}
}
func (m *Manager) publishExitLocked(r *reservation) {
	if r.exitedPublished || !r.published || r.result.Err != nil {
		return
	}
	r.exitedPublished = true
	if r.state != closing {
		r.state = exited
	}
	m.emitLocked(Event{Session: r.id, Message: protocol.SessionExited{Status: r.result.Status, LastOutputSequence: r.sequence, DrainReason: r.drainReason}})
}

// Natural completion closes the master after all captured-FD workers join while
// retaining the exited record and delivery ledger until explicit Close.
func (m *Manager) finishNatural(r *reservation) {
	defer close(r.naturalDone)
	select {
	case <-r.observed:
	case <-r.sealed:
	}
	<-r.startDone
	r.ioWorkers.Wait()
	// Explicit/failed-session cleanup owns its attempt and retry result. A reader
	// seal caused by Close must not let this finisher consume its first failure.
	m.mu.Lock()
	isClosing := r.state == closing || m.stopping
	m.mu.Unlock()
	if isClosing {
		return
	}
	ctx, cancel := context.WithTimeout(context.Background(), m.policy.CleanupTimeout)
	defer cancel()
	err := m.rollbackJoined(ctx, r)
	if err == nil {
		select {
		case <-r.observed:
		case <-ctx.Done():
			err = ctx.Err()
		}
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	if err != nil {
		if r.state == closing || m.stopping || m.records[r.id] != r {
			return
		}
		m.emitLocked(Event{Session: r.id, Message: protocol.ErrorMessage{Code: protocol.ErrorStatusUnavailable, Message: "session cleanup failed"}})
		return
	}
	m.publishExitLocked(r)
}
