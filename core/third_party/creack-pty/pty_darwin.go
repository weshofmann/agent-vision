//go:build darwin
// +build darwin

package pty

import (
	"errors"
	"fmt"
	"os"
	"syscall"
	"unsafe"
)

func open() (pty, tty *os.File, err error) {
	pFD, err := syscall.Open("/dev/ptmx", syscall.O_RDWR|syscall.O_CLOEXEC, 0)
	if err != nil {
		return nil, nil, err
	}
	p := os.NewFile(uintptr(pFD), "/dev/ptmx")
	// In case of error after this point, make sure we close the ptmx fd.
	defer func() {
		if err != nil {
			_ = p.Close() // Best effort.
		}
	}()

	t, err := OpenSlaveFromMaster(p)
	if err != nil {
		return nil, nil, err
	}
	return p, t, nil
}

// OpenSlaveFromMaster borrows master for the Darwin post-master sequence.
// It never closes or reopens master. Any returned slave belongs to the caller,
// including one returned with an error. There is no acquisition retry here.
func OpenSlaveFromMaster(master *os.File) (*os.File, error) {
	return openSlaveFromMaster(master, slaveOpenOps{ptsname, grantpt, unlockpt, os.OpenFile})
}

type slaveOpenOps struct {
	name          func(*os.File) (string, error)
	grant, unlock func(*os.File) error
	open          func(string, int, os.FileMode) (*os.File, error)
}

func openSlaveFromMaster(master *os.File, ops slaveOpenOps) (*os.File, error) {
	if master == nil {
		return nil, errors.New("nil borrowed master")
	}
	name, err := ops.name(master)
	if err != nil {
		return nil, fmt.Errorf("pty name: %w", err)
	}
	if err = ops.grant(master); err != nil {
		return nil, fmt.Errorf("pty grant: %w", err)
	}
	if err = ops.unlock(master); err != nil {
		return nil, fmt.Errorf("pty unlock: %w", err)
	}
	slave, err := ops.open(name, os.O_RDWR|syscall.O_NOCTTY, 0)
	if err != nil {
		return slave, fmt.Errorf("pty slave open: %w", err)
	}
	return slave, nil
}

func ptsname(f *os.File) (string, error) {
	n := make([]byte, _IOC_PARM_LEN(syscall.TIOCPTYGNAME))

	err := ioctl(f, syscall.TIOCPTYGNAME, uintptr(unsafe.Pointer(&n[0])))
	if err != nil {
		return "", err
	}

	for i, c := range n {
		if c == 0 {
			return string(n[:i]), nil
		}
	}
	return "", errors.New("TIOCPTYGNAME string not NUL-terminated")
}

func grantpt(f *os.File) error {
	return ioctl(f, syscall.TIOCPTYGRANT, 0)
}

func unlockpt(f *os.File) error {
	return ioctl(f, syscall.TIOCPTYUNLK, 0)
}
