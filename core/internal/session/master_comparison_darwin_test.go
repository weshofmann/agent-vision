//go:build darwin

package session

import (
	"context"
	"errors"
	"fmt"
	"os"
	"sync"
	"sync/atomic"
	"syscall"
	"testing"
)

// Explicit opt-in keeps the one approved comparison out of whole-module tests.
// Run once only after independent implementation/recipe review is clear.
func TestDarwinMasterOpenComparison(t *testing.T) {
	if os.Getenv("AGENTVISION_DARWIN_COMPARISON") != "1" {
		t.Skip("native comparison requires separately reviewed one-run recipe")
	}
	var raw, logical, live, maxLive atomic.Int32
	type observation struct {
		arm, mode, stage                          string
		index                                     int
		report                                    MasterOpenReport
		err, closeErr                             error
		masterCloseErr, slaveCloseErr             error
		masterCloseAttempted, slaveCloseAttempted bool
	}
	var observations []observation
	acquire := func(arm, mode string, index, limit int) observation {
		logical.Add(1)
		o := observation{arm: arm, mode: mode, index: index, stage: "master"}
		ops := nativeDarwinSpawnOps()
		open := ops.master.open
		ops.master.open = func() (int, error) {
			if raw.Add(1) > 128 {
				return -1, errors.New("raw Open cap exceeded")
			}
			return open()
		}
		master, report, e := acquireDarwinMasterLimit(context.Background(), ops.master, limit)
		o.report = report
		o.err = e
		if master == nil {
			return o
		}
		n := live.Add(1)
		for previous := maxLive.Load(); n > previous && !maxLive.CompareAndSwap(previous, n); previous = maxLive.Load() {
		}
		r := &Resources{Master: master, MasterFD: int(master.Fd()), MasterOpen: report}
		r.close = func(f *os.File) error {
			e := f.Close()
			if f == master {
				o.masterCloseAttempted = true
				o.masterCloseErr = e
			} else {
				o.slaveCloseAttempted = true
				o.slaveCloseErr = e
			}
			return e
		}
		if e == nil {
			o.stage = "post-master"
			slave, e := ops.slave(master)
			r.Slave = slave
			o.err = e
		}
		o.closeErr = r.Rollback(context.Background())
		live.Add(-1)
		return o
	}
	acceptable := func(o observation) bool {
		if o.closeErr != nil {
			return false
		}
		if o.err == nil {
			return !o.report.Exhausted && o.masterCloseAttempted && o.slaveCloseAttempted
		}
		return o.arm == "control" && o.stage == "master" && o.report.Attempts == 1 && o.report.ErrnoObserved[0] && o.report.Errnos[0] == ^syscall.Errno(5) && errors.Is(o.err, ^syscall.Errno(5))
	}
	record := func(o observation) {
		observations = append(observations, o)
		t.Logf("arm=%s mode=%s index=%d stage=%s report=%+v error=%T:%v close=%T:%v slaveCloseAttempted=%t slaveClose=%T:%v masterCloseAttempted=%t masterClose=%T:%v", o.arm, o.mode, o.index, o.stage, o.report, o.err, o.err, o.closeErr, o.closeErr, o.slaveCloseAttempted, o.slaveCloseErr, o.slaveCloseErr, o.masterCloseAttempted, o.masterCloseErr, o.masterCloseErr)
		for i := 0; i < int(o.report.Attempts); i++ {
			if o.report.ErrnoObserved[i] {
				errno := o.report.Errnos[i]
				t.Logf("arm=%s mode=%s index=%d masterAttempt=%d errnoBits=%#016x errnoSigned=%d", o.arm, o.mode, o.index, i+1, uint64(errno), int64(uint64(errno)))
			}
		}
	}
	for _, arm := range []struct {
		name  string
		limit int
	}{{"control", 1}, {"candidate", 3}} {
		for i := 0; i < 16; i++ {
			o := acquire(arm.name, "serial", i, arm.limit)
			record(o)
			if !acceptable(o) {
				t.Fatalf("stop after joined owned cleanup: %s", fmt.Sprint(o))
			}
		}
		results := make(chan observation, 16)
		var wg sync.WaitGroup
		for i := 0; i < 16; i++ {
			wg.Add(1)
			go func(index int) { defer wg.Done(); results <- acquire(arm.name, "concurrent", index, arm.limit) }(i)
		}
		wg.Wait()
		close(results)
		failed := false
		for o := range results {
			record(o)
			failed = failed || !acceptable(o)
		}
		if failed {
			t.Fatal("stop: unexpected outcome/cleanup uncertainty after concurrent join")
		}
	}
	if logical.Load() != 64 || raw.Load() > 128 || maxLive.Load() > 16 || live.Load() != 0 {
		t.Fatalf("caps logical=%d raw=%d maxLive=%d live=%d", logical.Load(), raw.Load(), maxLive.Load(), live.Load())
	}
	recovered := 0
	for _, o := range observations {
		if o.arm == "candidate" && o.report.Recovered {
			recovered++
		}
	}
	t.Logf("comparison logical=%d raw=%d maxLive=%d candidateRecovered=%d; control-first ordering bias; finite observation, kernel cause unproved", logical.Load(), raw.Load(), maxLive.Load(), recovered)
	if recovered == 0 {
		t.Log("no native retry recovery observed; efficacy not demonstrated; no additional comparison authorized")
	}
}
