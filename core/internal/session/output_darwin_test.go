//go:build darwin

package session

import (
	"context"
	"errors"
	"github.com/weshofmann/agent-vision/core/internal/policy"
	"github.com/weshofmann/agent-vision/core/internal/protocol"
	"os"
	"syscall"
	"testing"
	"time"
	"unsafe"
)

// A real shell must complete while the parent retains the slave: EOF is absent,
// and the manager must drain concurrently with the sole Wait4 owner.
func TestOutputNativeHeldSlaveNaturalExit(t *testing.T) {
	s := newSink()
	pumpRecordedOutput(t, s)
	var r *Resources
	var held *os.File
	m := NewManager(policy.Default(), spawnFunc(func(ctx context.Context, _ SpawnConfig) (*Resources, error) {
		var err error
		r, err = (DarwinSpawner{}).Spawn(ctx, config("/bin/sh"))
		if err != nil {
			return r, err
		}
		var name [128]byte
		_, _, errno := syscall.Syscall(syscall.SYS_IOCTL, uintptr(r.MasterFD), syscall.TIOCPTYGNAME, uintptr(unsafe.Pointer(&name[0])))
		if errno != 0 {
			return r, errno
		}
		n := 0
		for n < len(name) && name[n] != 0 {
			n++
		}
		held, err = os.OpenFile(string(name[:n]), os.O_RDWR|syscall.O_NOCTTY, 0)
		return r, err
	}), s)
	t.Cleanup(func() {
		shutdown(t, m)
		if held != nil {
			held.Close()
		}
	})
	create(t, m, 1)
	id := s.await(t, 1)[0].Session
	outputUntil(t, s, "READY> ")
	admitCommand(t, m, protocol.InputBytes{Bytes: []byte("printf 'TAIL_MARKER'; exit 17\n")}, 2, id)
	assertAck(t, completion(t, s, 2), protocol.TypeInputBytes)
	v := awaitExit(t, s)
	if v.Status != (protocol.ExitStatus{Kind: 1, Value: 17}) || (v.DrainReason != protocol.DrainNoData && v.DrainReason != protocol.DrainEOF) || v.LastOutputSequence == 0 {
		t.Fatalf("held slave exit %+v", v)
	}
	outputUntil(t, s, "TAIL_MARKER")
	if _, e := r.Master.Stat(); !errors.Is(e, os.ErrClosed) {
		t.Fatal("natural seal retained master")
	}
	m.mu.Lock()
	record := m.records[id]
	m.mu.Unlock()
	if record == nil {
		t.Fatal("natural exit freed metadata")
	}
	admitCommand(t, m, protocol.CloseSession{}, 3, id)
	completion(t, s, 3)
	n := 0
	for _, e := range s.snapshot() {
		if _, ok := e.Message.(protocol.SessionExited); ok {
			n++
		}
	}
	if n != 1 {
		t.Fatal("explicit Close duplicated Exited")
	}
	t.Log("real shell exit17; held slave bounded by EOF/no-data; master closed, metadata retained")
}

type heldNativeReader struct {
	nativeOutputIO
	entered, release chan struct{}
	held             bool
}

func (r *heldNativeReader) Read(fd int, b []byte) (int, error) {
	n, e := r.nativeOutputIO.Read(fd, b)
	if n > 0 && !r.held {
		r.held = true
		close(r.entered)
		<-r.release
	}
	return n, e
}

// The raw kernel queue continues producing after direct-child observation. The
// held successful native read is admitted before seal despite crossing Wait.
func TestHeldContinuousSlaveDrainNative(t *testing.T) {
	r := rawCommandResources(t)
	slaveFD, dupErr := syscall.Dup(int(r.Slave.Fd()))
	if dupErr != nil {
		t.Fatal(dupErr)
	}
	syscall.CloseOnExec(slaveFD)
	defer syscall.Close(slaveFD)
	if e := syscall.SetNonblock(slaveFD, true); e != nil {
		t.Fatal(e)
	}
	rd := &heldNativeReader{entered: make(chan struct{}), release: make(chan struct{})}
	s := newSink()
	p := policy.Default()
	p.DrainTime = time.Second
	m := NewManager(p, spawnFunc(func(context.Context, SpawnConfig) (*Resources, error) { return r, nil }), s)
	m.outputIO = rd
	t.Cleanup(func() { shutdown(t, m) })
	create(t, m, 1)
	id := s.await(t, 1)[0].Session
	stop, joined := make(chan struct{}), make(chan struct{})
	go func() {
		defer close(joined)
		b := make([]byte, 8192)
		for i := range b {
			b[i] = 'x'
		}
		for {
			select {
			case <-stop:
				return
			default:
			}
			_, e := syscall.Write(slaveFD, b)
			if e != nil && e != syscall.EAGAIN && e != syscall.EINTR {
				return
			}
			if e == syscall.EAGAIN {
				time.Sleep(time.Millisecond)
			}
		}
	}()
	waitGate(t, rd.entered)
	r.Process.(*fixtureProcess).finish(protocol.ExitStatus{Kind: 1, Value: 17})
	m.mu.Lock()
	record := m.records[id]
	m.mu.Unlock()
	waitGate(t, record.observed)
	close(rd.release)
	v := awaitExit(t, s)
	close(stop)
	waitGate(t, joined)
	if v.LastOutputSequence == 0 || v.Status.Value != 17 {
		t.Fatal("lost native held read")
	}
	if v.DrainReason != protocol.DrainByteCap && (v.DrainReason != protocol.DrainNoData && v.DrainReason != protocol.DrainEOF) {
		t.Fatalf("unbounded native drain %+v", v)
	}
	total := 0
	for _, e := range s.snapshot() {
		if o, ok := e.Message.(protocol.OutputBytes); ok {
			total += len(o.Bytes)
		}
	}
	if total > 98304 {
		t.Fatal("native cap exceeded in-flight allowance")
	}
	t.Logf("native held/continuous slave sealed %d bytes reason%d", total, v.DrainReason)
}

// Model successful writer/receiver consumption for native resource tests. The
// recording sink retains a small synthetic transcript solely for assertions.
func pumpRecordedOutput(t *testing.T, s *recordingSink) {
	t.Helper()
	stop, joined := make(chan struct{}), make(chan struct{})
	t.Cleanup(func() { close(stop); waitGate(t, joined) })
	go func() {
		defer close(joined)
		ticker := time.NewTicker(time.Millisecond)
		defer ticker.Stop()
		seen := 0
		for {
			select {
			case <-stop:
				return
			case <-ticker.C:
				events := s.snapshot()
				for ; seen < len(events); seen++ {
					e := events[seen]
					if e.Output != nil {
						if err := e.Output.BeginWrite(); err != nil {
							return
						}
						if _, err := e.Output.CommitWrite(); err != nil {
							return
						}
						o := e.Message.(protocol.OutputBytes)
						e.Output.ledger.ApplyCredit(protocol.RequestID(seen+1000), uint32(len(o.Bytes)))
					}
				}
			}
		}
	}()
}
