package main

import (
	"bytes"
	"errors"
	"net"
	"os"
	"syscall"
	"testing"
)

func TestInheritedFDValidation(t *testing.T) {
	for _, mode := range []string{"", "daemon", "listener"} {
		if _, e := inheritedConn(3, mode); e == nil {
			t.Fatal("arbitrary mode accepted")
		}
	}
	for _, fd := range []int{-1, 0, 1, 2, 999999} {
		if _, e := inheritedConn(fd, "frontend-spawned"); e == nil {
			t.Fatal("invalid descriptor accepted")
		}
	}
	r, w, e := os.Pipe()
	if e != nil {
		t.Fatal(e)
	}
	defer r.Close()
	defer w.Close()
	if _, e = inheritedConn(int(r.Fd()), "frontend-spawned"); e == nil {
		t.Fatal("pipe accepted as Unix stream")
	}
	fds, e := syscall.Socketpair(syscall.AF_UNIX, syscall.SOCK_STREAM, 0)
	if e != nil {
		t.Fatal(e)
	}
	defer syscall.Close(fds[1])
	fd := fds[0]
	c, e := inheritedConn(fd, "frontend-spawned")
	if e != nil {
		t.Fatal(e)
	}
	defer c.Close()
	if _, ok := c.(*net.UnixConn); !ok {
		t.Fatal("non Unix connection")
	}
	if _, e = syscall.Getpeername(fd); !errors.Is(e, syscall.EBADF) {
		t.Fatal("original duplicate retained")
	}
}
func TestInheritedUnconnectedAndDatagram(t *testing.T) {
	for _, typ := range []int{syscall.SOCK_STREAM, syscall.SOCK_DGRAM} {
		fd, e := syscall.Socket(syscall.AF_UNIX, typ, 0)
		if e != nil {
			t.Fatal(e)
		}
		defer syscall.Close(fd)
		if c, e := inheritedConn(fd, "frontend-spawned"); e == nil {
			c.Close()
			t.Fatal("unconnected socket accepted")
		}
	}
}
func TestCLIArguments(t *testing.T) {
	for _, args := range [][]string{{}, {"--ipc-fd=3"}, {"--mode=daemon", "--ipc-fd=3"}, {"--ipc-fd=1", "--mode=frontend-spawned"}, {"--listen=ignored"}, {"--ipc-fd=3", "--mode=frontend-spawned", "exec"}} {
		var b bytes.Buffer
		if code := run(args, &b); code == 0 {
			t.Fatal("invalid options succeeded")
		}
		if b.Len() == 0 {
			t.Fatal("missing sanitized stderr")
		}
	}
}
