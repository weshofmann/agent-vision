//go:build darwin

package session

import (
	"context"
	"syscall"
	"unsafe"

	"github.com/creack/pty"
)

type nativeCommandIO struct{}

func (nativeCommandIO) Write(fd int, b []byte) (int, error) {
	n, err := syscall.Write(fd, b)
	// Darwin syscall.Write reports -1 on errors; no bytes were accepted then.
	if n < 0 && err != nil {
		n = 0
	}
	return n, err
}
func (nativeCommandIO) Wait(ctx context.Context, fd int) error {
	return (DarwinPoller{}).Wait(ctx, fd, true)
}
func (nativeCommandIO) Resize(fd int, rows, cols uint16) error {
	// pty.Setsize calls File.Fd, while Resources requires the immutable descriptor
	// captured before SetNonblock. Preserve that invariant without relying on
	// os.File construction or nonblocking bookkeeping details.
	size := pty.Winsize{Rows: rows, Cols: cols}
	_, _, err := syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), syscall.TIOCSWINSZ, uintptr(unsafe.Pointer(&size)))
	if err != 0 {
		return err
	}
	return nil
}
