//go:build darwin

package session

import (
	"os"
	"syscall"
	"unsafe"
)

// Explicit policy audited against pinned tvterm createTermios and the accepted
// V0 readback fixture. Fresh PTY defaults and outer terminal mode differ.
func configureV0Termios(slave *os.File) error {
	mode := syscall.Termios{Iflag: syscall.ICRNL | syscall.IXON | syscall.IUTF8, Oflag: syscall.OPOST | syscall.ONLCR, Cflag: syscall.CS8 | syscall.CREAD, Lflag: syscall.ISIG | syscall.ICANON | syscall.IEXTEN | syscall.ECHO | syscall.ECHOE | syscall.ECHOK | syscall.ECHOCTL | syscall.ECHOKE, Ispeed: syscall.B38400, Ospeed: syscall.B38400}
	for k, v := range map[int]uint8{syscall.VINTR: 3, syscall.VQUIT: 28, syscall.VERASE: 127, syscall.VKILL: 21, syscall.VEOF: 4, syscall.VEOL: 255, syscall.VEOL2: 255, syscall.VSTART: 17, syscall.VSTOP: 19, syscall.VSUSP: 26, syscall.VREPRINT: 18, syscall.VWERASE: 23, syscall.VLNEXT: 22, syscall.VMIN: 1, syscall.VTIME: 0} {
		mode.Cc[k] = v
	}
	_, _, e := syscall.Syscall(syscall.SYS_IOCTL, slave.Fd(), syscall.TIOCSETA, uintptr(unsafe.Pointer(&mode)))
	if e != 0 {
		return e
	}
	return nil
}
