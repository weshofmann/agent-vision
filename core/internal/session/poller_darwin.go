//go:build darwin

package session

import (
	"context"
	"errors"
	"syscall"
)

type DarwinPoller struct{}

// Wait owns its kqueue and wake pipe until its cancellation goroutine is joined.
// Caller retains the target FD and must join Wait before target close/reuse.
func (DarwinPoller) Wait(ctx context.Context, fd int, writable bool) error {
	if e := ctx.Err(); e != nil {
		return e
	}
	// Darwin kqueue(2): the queue is not inherited across fork. CLOEXEC
	// additionally protects exec in this process; no fork leak interval exists.
	kq, e := syscall.Kqueue()
	if e != nil {
		return e
	}
	syscall.CloseOnExec(kq)
	defer syscall.Close(kq)
	var pipe [2]int
	// ForkLock prevents a concurrent session exec from inheriting descriptors in
	// the interval between Pipe and CLOEXEC (Darwin has no syscall.Pipe2).
	syscall.ForkLock.RLock()
	e = syscall.Pipe(pipe[:])
	if e == nil {
		for _, v := range pipe {
			syscall.CloseOnExec(v)
		}
	}
	syscall.ForkLock.RUnlock()
	if e != nil {
		return e
	}
	defer syscall.Close(pipe[0])
	defer syscall.Close(pipe[1])
	for _, v := range pipe {
		if e = syscall.SetNonblock(v, true); e != nil {
			return e
		}
	}
	filter := int16(syscall.EVFILT_READ)
	if writable {
		filter = syscall.EVFILT_WRITE
	}
	changes := []syscall.Kevent_t{{Ident: uint64(fd), Filter: filter, Flags: syscall.EV_ADD | syscall.EV_ENABLE}, {Ident: uint64(pipe[0]), Filter: syscall.EVFILT_READ, Flags: syscall.EV_ADD | syscall.EV_ENABLE}}
	stop := make(chan struct{})
	joined := make(chan struct{})
	go func() {
		defer close(joined)
		select {
		case <-ctx.Done():
			for {
				_, e := syscall.Write(pipe[1], []byte{1})
				if e != syscall.EINTR {
					break
				}
			}
		case <-stop:
		}
	}()
	defer func() { close(stop); <-joined }()
	events := make([]syscall.Kevent_t, 2)
	for {
		n, e := syscall.Kevent(kq, changes, events, nil)
		if e == syscall.EINTR {
			continue
		}
		if e != nil {
			return e
		}
		changes = nil
		if e = ctx.Err(); e != nil {
			return e
		}
		for _, ev := range events[:n] {
			if ev.Flags&syscall.EV_ERROR != 0 {
				return syscall.Errno(ev.Data)
			}
			if ev.Ident == uint64(fd) {
				return nil
			}
		}
		if n == 0 {
			return errors.New("empty readiness result")
		}
	}
}
