//go:build darwin

package pty

import (
	"errors"
	"os"
	"syscall"
	"testing"
)

// Catches retries, borrowed-master closes, and loss of a late slave at each phase.
func TestDarwinBorrowedMasterInjected(t *testing.T) {
	for _, phase := range []string{"name", "grant", "unlock", "slave", "success"} {
		t.Run(phase, func(t *testing.T) {
			master, e := os.CreateTemp(t.TempDir(), "master")
			if e != nil {
				t.Fatal(e)
			}
			defer master.Close()
			slave, e := os.CreateTemp(t.TempDir(), "slave")
			if e != nil {
				t.Fatal(e)
			}
			defer slave.Close()
			boom := syscall.EIO
			var calls []string
			fail := func(s string) error {
				calls = append(calls, s)
				if s == phase {
					return boom
				}
				return nil
			}
			ops := slaveOpenOps{
				name: func(f *os.File) (string, error) {
					if f != master {
						t.Fatal("master identity")
					}
					return "synthetic-slave", fail("name")
				},
				grant: func(f *os.File) error {
					if f != master {
						t.Fatal("master identity")
					}
					return fail("grant")
				},
				unlock: func(f *os.File) error {
					if f != master {
						t.Fatal("master identity")
					}
					return fail("unlock")
				},
				open: func(name string, flags int, mode os.FileMode) (*os.File, error) {
					if name != "synthetic-slave" || flags != os.O_RDWR|syscall.O_NOCTTY || mode != 0 {
						t.Fatal("slave open arguments")
					}
					return slave, fail("slave")
				}}
			got, err := openSlaveFromMaster(master, ops)
			want := map[string]int{"name": 1, "grant": 2, "unlock": 3, "slave": 4, "success": 4}[phase]
			if len(calls) != want {
				t.Fatalf("calls=%v", calls)
			}
			if phase == "success" {
				if err != nil || got != slave {
					t.Fatalf("success: %v %v", got, err)
				}
			} else if !errors.Is(err, boom) {
				t.Fatalf("typed phase error: %v", err)
			}
			if phase == "slave" && got != slave {
				t.Fatal("late slave ownership lost")
			}
			if phase != "slave" && phase != "success" && got != nil {
				t.Fatal("unacquired slave")
			}
			if _, e := master.Stat(); e != nil {
				t.Fatalf("borrowed master closed: %v", e)
			}
		})
	}
	t.Run("nil-public-api", func(t *testing.T) {
		if f, e := OpenSlaveFromMaster(nil); e == nil || f != nil {
			t.Fatal("nil master accepted")
		}
	})
}
