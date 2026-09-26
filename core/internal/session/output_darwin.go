//go:build darwin

package session

import (
	"context"
	"io"
	"syscall"
)

type nativeOutputIO struct{}

func (nativeOutputIO) Read(fd int, b []byte) (int, error) {
	n, e := syscall.Read(fd, b)
	if n < 0 && e != nil {
		n = 0
	}
	if n == 0 && e == nil {
		e = io.EOF
	}
	return n, e
}
func (nativeOutputIO) Wait(ctx context.Context, fd int) error {
	return (DarwinPoller{}).Wait(ctx, fd, false)
}
