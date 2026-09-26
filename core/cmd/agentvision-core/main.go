package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"os/signal"
	"runtime"
	"syscall"

	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/server"
	"github.com/weshofmann/agent-vision/core/internal/session"
)

// Only a connected inherited Unix stream is supported. FileConn duplicates the
// FD; the original is closed on every conversion path and both are CLOEXEC.
func inheritedConn(fd int, mode string) (net.Conn, error) {
	if mode != "frontend-spawned" || fd < 3 {
		return nil, errors.New("invalid startup configuration")
	}
	typ, err := syscall.GetsockoptInt(fd, syscall.SOL_SOCKET, syscall.SO_TYPE)
	if err != nil || typ != syscall.SOCK_STREAM {
		return nil, errors.New("invalid IPC stream")
	}
	local, err := syscall.Getsockname(fd)
	if err != nil {
		return nil, errors.New("invalid IPC stream")
	}
	if _, ok := local.(*syscall.SockaddrUnix); !ok {
		return nil, errors.New("invalid IPC family")
	}
	peer, err := syscall.Getpeername(fd)
	if err != nil {
		return nil, errors.New("IPC is not connected")
	}
	if _, ok := peer.(*syscall.SockaddrUnix); !ok {
		return nil, errors.New("invalid IPC peer")
	}
	syscall.CloseOnExec(fd)
	file := os.NewFile(uintptr(fd), "inherited-ipc")
	c, err := net.FileConn(file)
	closeErr := file.Close()
	if err != nil {
		return nil, errors.New("IPC conversion failed")
	}
	if closeErr != nil {
		c.Close()
		return nil, errors.New("IPC descriptor close failed")
	}
	if _, ok := c.(*net.UnixConn); !ok {
		c.Close()
		return nil, errors.New("invalid IPC connection")
	}
	return c, nil
}
func run(args []string, stderr io.Writer) int {
	fail := func(message string) int { fmt.Fprintln(stderr, "agentvision-core:", message); return 1 }
	flags := flag.NewFlagSet("agentvision-core", flag.ContinueOnError)
	flags.SetOutput(io.Discard)
	fd := flags.Int("ipc-fd", -1, "inherited Unix stream descriptor")
	mode := flags.String("mode", "", "frontend-spawned")
	if err := flags.Parse(args); err != nil || flags.NArg() != 0 {
		return fail("invalid startup arguments")
	}
	if runtime.GOOS != "darwin" || runtime.GOARCH != "arm64" {
		return fail("unsupported core target")
	}
	c, err := inheritedConn(*fd, *mode)
	if err != nil {
		return fail("invalid inherited IPC configuration")
	}
	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()
	p := policy.Default()
	s := server.NewScheduler()
	m := session.NewManager(p, session.DarwinSpawner{}, s)
	if err = server.Serve(ctx, c, m, p); err != nil {
		return fail("connection or owned cleanup failed")
	}
	return 0
}
func main() { os.Exit(run(os.Args[1:], os.Stderr)) }
