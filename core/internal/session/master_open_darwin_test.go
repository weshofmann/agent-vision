//go:build darwin

package session

import (
	"context"
	"errors"
	"fmt"
	"os"
	"reflect"
	"syscall"
	"testing"
	"time"
)

// Catches wrong classification, excess retries, late timer admission, and lost late FDs.
func TestDarwinMasterOpenInjected(t *testing.T) {
	neg := ^syscall.Errno(5)
	for _, tc := range []struct {
		name                 string
		errs                 []error
		slow, oversleep      time.Duration
		calls                int
		waits                []time.Duration
		recovered, exhausted bool
	}{
		{name: "immediate", errs: []error{nil}, calls: 1},
		{name: "one-recovery", errs: []error{neg, nil}, calls: 2, waits: []time.Duration{5 * time.Millisecond}, recovered: true},
		{name: "two-recoveries", errs: []error{neg, neg, nil}, calls: 3, waits: []time.Duration{5 * time.Millisecond, 10 * time.Millisecond}, recovered: true},
		{name: "exhaustion", errs: []error{neg, neg, neg}, calls: 3, waits: []time.Duration{5 * time.Millisecond, 10 * time.Millisecond}, exhausted: true},
		{name: "slow-failure", errs: []error{neg}, slow: 50 * time.Millisecond, calls: 1, exhausted: true},
		{name: "overslept", errs: []error{neg}, oversleep: 50 * time.Millisecond, calls: 1, waits: []time.Duration{5 * time.Millisecond}, exhausted: true},
		{name: "positive-six", errs: []error{syscall.Errno(6)}, calls: 1},
		{name: "eintr", errs: []error{syscall.EINTR}, calls: 1},
		{name: "other", errs: []error{syscall.EIO}, calls: 1},
		{name: "wrapped", errs: []error{fmt.Errorf("wrapper: %w", neg)}, calls: 1},
		{name: "joined", errs: []error{errors.Join(neg, syscall.EIO)}, calls: 1},
		{name: "text", errs: []error{errors.New(neg.Error())}, calls: 1},
	} {
		t.Run(tc.name, func(t *testing.T) {
			origin := time.Unix(1, 0)
			now := origin
			calls := 0
			var waits []time.Duration
			ops := masterOpenOps{now: func() time.Time { return now }, wait: func(ctx context.Context, d time.Duration) error {
				waits = append(waits, d)
				now = now.Add(d + tc.oversleep)
				return ctx.Err()
			}, open: func() (int, error) {
				calls++
				if calls > len(tc.errs) {
					t.Fatal("unexpected extra open")
				}
				now = now.Add(tc.slow)
				e := tc.errs[calls-1]
				if e != nil {
					return -1, e
				}
				return duplicateTemp(t), nil
			}}
			f, r, e := acquireDarwinMaster(context.Background(), ops)
			if f != nil {
				if e := f.Close(); e != nil {
					t.Fatal(e)
				}
			}
			if calls != tc.calls || !reflect.DeepEqual(waits, tc.waits) {
				t.Fatalf("calls/waits %d %v want %d %v", calls, waits, tc.calls, tc.waits)
			}
			if r.Attempts != uint8(tc.calls) || r.Recovered != tc.recovered || r.Exhausted != tc.exhausted || r.Cancelled || r.Elapsed != now.Sub(origin) {
				t.Fatalf("report=%+v", r)
			}
			if tc.errs[len(tc.errs)-1] == nil && !tc.exhausted {
				if f == nil || e != nil {
					t.Fatalf("success %v %v", f, e)
				}
			} else {
				if f != nil || e == nil {
					t.Fatalf("failure %v %v", f, e)
				}
				if tc.name != "text" && !errors.Is(e, tc.errs[len(tc.errs)-1]) {
					t.Fatalf("last error lost: %v", e)
				}
			}
			for i := 0; i < tc.calls; i++ {
				errno, typed := tc.errs[i].(syscall.Errno)
				if r.ErrnoObserved[i] != typed || (typed && r.Errnos[i] != errno) {
					t.Fatalf("typed observation %d: %+v", i, r)
				}
			}
			if tc.exhausted {
				var me *MasterOpenError
				if !errors.As(e, &me) || me.Attempts != uint8(tc.calls) || me.Elapsed != r.Elapsed {
					t.Fatalf("exhaustion detail: %v", e)
				}
			}
		})
	}
	for _, when := range []string{"before", "during-wait", "after-wait", "racing-success", "racing-recovered-success", "late-success"} {
		t.Run(when, func(t *testing.T) {
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			now := time.Unix(1, 0)
			calls := 0
			waits := 0
			entered := make(chan struct{})
			release := make(chan struct{})
			if when == "before" {
				cancel()
			}
			ops := masterOpenOps{now: func() time.Time { return now }, wait: func(context.Context, time.Duration) error {
				waits++
				if when == "racing-recovered-success" {
					now = now.Add(5 * time.Millisecond)
					return nil
				}
				cancel()
				if when == "during-wait" {
					return ctx.Err()
				}
				return nil
			}, open: func() (int, error) {
				calls++
				if when == "racing-recovered-success" && calls == 1 {
					return -1, neg
				}
				if when == "racing-success" || when == "racing-recovered-success" {
					close(entered)
					<-release
					return duplicateTemp(t), nil
				}
				if when == "late-success" {
					now = now.Add(75 * time.Millisecond)
					return duplicateTemp(t), nil
				}
				return -1, neg
			}}
			type outcome struct {
				f *os.File
				r MasterOpenReport
				e error
			}
			ch := make(chan outcome, 1)
			go func() { f, r, e := acquireDarwinMaster(ctx, ops); ch <- outcome{f, r, e} }()
			if when == "racing-success" || when == "racing-recovered-success" {
				<-entered
				cancel()
				close(release)
			}
			got := <-ch
			want := 1
			if when == "racing-recovered-success" {
				want = 2
			}
			if when == "before" {
				want = 0
			}
			if calls != want {
				t.Fatalf("calls %d", calls)
			}
			if when == "late-success" {
				if got.f == nil || got.e != nil || got.r.Elapsed != 75*time.Millisecond {
					t.Fatalf("late success %+v", got)
				}
			} else {
				if !errors.Is(got.e, context.Canceled) || !got.r.Cancelled {
					t.Fatalf("cancel %+v", got)
				}
				if waits > 0 && !errors.Is(got.e, neg) {
					t.Fatalf("last acquisition error lost: %v", got.e)
				}
			}
			if (when == "racing-success" || when == "racing-recovered-success" || when == "late-success") != (got.f != nil) {
				t.Fatal("late fd adoption")
			}
			if got.f != nil {
				if e := got.f.Close(); e != nil {
					t.Fatal(e)
				}
				if _, e := got.f.Stat(); !errors.Is(e, os.ErrClosed) {
					t.Fatal("adopted fd not closed")
				}
			}
		})
	}
	for _, kind := range []string{"owned-with-error", "negative-fd-success", "invalid-failure-fd"} {
		t.Run(kind, func(t *testing.T) {
			calls := 0
			ops := masterOpenOps{now: time.Now, wait: func(context.Context, time.Duration) error { t.Fatal("invalid outcome retried"); return nil }, open: func() (int, error) {
				calls++
				switch kind {
				case "owned-with-error":
					return duplicateTemp(t), neg
				case "negative-fd-success":
					return -1, nil
				default:
					return -2, neg
				}
			}}
			f, r, e := acquireDarwinMaster(context.Background(), ops)
			if calls != 1 || r.Attempts != 1 || e == nil {
				t.Fatalf("invalid outcome %d %+v %v", calls, r, e)
			}
			if (kind == "owned-with-error") != (f != nil) {
				t.Fatal("owned outcome discarded")
			}
			if f != nil {
				f.Close()
			}
		})
	}
}
func duplicateTemp(t *testing.T) int {
	t.Helper()
	f, e := os.CreateTemp(t.TempDir(), "owned")
	if e != nil {
		t.Fatal(e)
	}
	fd, e := syscall.Dup(int(f.Fd()))
	f.Close()
	if e != nil {
		t.Fatal(e)
	}
	return fd
}
