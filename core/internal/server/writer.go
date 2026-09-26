package server

import (
	"errors"
	"net"
	"os"
	"syscall"
	"time"

	"github.com/weshofmann/agent-vision/core/internal/protocol"
)

// RawConn keeps the descriptor alive; no bare FD escapes a callback. Validate
// connected AF_UNIX/SOCK_STREAM and O_NONBLOCK before admission can begin.
func validateRawConn(c net.Conn) (syscall.RawConn, error) {
	socket, ok := c.(syscall.Conn)
	if !ok {
		return nil, ErrContact
	}
	raw, err := socket.SyscallConn()
	if err != nil {
		return nil, err
	}
	var validation error
	err = raw.Control(func(fd uintptr) {
		flags, _, e := syscall.Syscall(syscall.SYS_FCNTL, fd, syscall.F_GETFL, 0)
		if e != 0 || flags&syscall.O_NONBLOCK == 0 {
			validation = ErrContact
			return
		}
		typ, e1 := syscall.GetsockoptInt(int(fd), syscall.SOL_SOCKET, syscall.SO_TYPE)
		local, e2 := syscall.Getsockname(int(fd))
		peer, e3 := syscall.Getpeername(int(fd))
		_, unixLocal := local.(*syscall.SockaddrUnix)
		_, unixPeer := peer.(*syscall.SockaddrUnix)
		if e1 != nil || e2 != nil || e3 != nil || typ != syscall.SOCK_STREAM || !unixLocal || !unixPeer {
			validation = ErrContact
		}
	})
	return raw, errors.Join(err, validation)
}

// Socket capacity waits are exclusively RawConn's poller, outside admission mu.
// One nonblocking attempt plus final terminal retirement is the linearization
// cutpoint. The sole terminal frame remains separately scheduler-charged until
// Complete, even when its correlation has already retired at this cutpoint.
type socketWriter struct {
	connection *connection
	raw        syscall.RawConn
	request    protocol.RequestID
	remaining  int
	attempt    func(int, []byte) (int, error)
	deadline   time.Time
}

func (w *socketWriter) Write(b []byte) (int, error) {
	x := w.connection
	for {
		if w.deadline.IsZero() {
			w.deadline = time.Now().Add(x.policy.WriteTimeout)
		}
		if !time.Now().Before(w.deadline) {
			return 0, os.ErrDeadlineExceeded
		}
		if err := x.conn.SetWriteDeadline(w.deadline); err != nil {
			return 0, err
		}
		var n int
		var attemptErr error
		err := w.raw.Write(func(fd uintptr) bool {
			x.mu.Lock()
			n, attemptErr = w.attempt(int(fd), b)
			if n < 0 {
				n = 0
			}
			if n > len(b) {
				attemptErr = protocol.ErrInvalid
				n = 0
			}
			if n > 0 {
				w.deadline = time.Now().Add(x.policy.WriteTimeout)
				w.remaining -= n
				if attemptErr == nil && w.remaining == 0 && w.request != 0 {
					x.retireLocked(w.request)
				}
			}
			x.mu.Unlock()
			return attemptErr != syscall.EAGAIN && attemptErr != syscall.EWOULDBLOCK
		})
		if err != nil {
			return n, err
		}
		if attemptErr == syscall.EINTR && n == 0 {
			continue
		}
		return n, attemptErr
	}
}
func (x *connection) writeFrame(f protocol.Frame) error {
	raw := x.raw
	if raw == nil {
		var err error
		raw, err = validateRawConn(x.conn)
		if err != nil {
			return err
		}
	}
	writer := socketWriter{connection: x, raw: raw, request: f.Header.Request, remaining: protocol.HeaderSize + len(f.Body), attempt: syscall.Write}
	return protocol.WriteFrame(&writer, f)
}
