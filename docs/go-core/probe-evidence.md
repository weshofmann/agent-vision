# Go core architecture probe evidence

Synthetic inputs only. No PIDs, device names, home paths, raw environment or
private terminal captures are retained. Executable sources/caches live under
ignored `.probe/go-core`; reviewed source listings and reproduction commands live
in [the disposable fixture area](../../experiments/go-core-architecture/README.md).
This is feasibility evidence, not retained backend qualification.

Environment: macOS 26.6.2 arm64; Go 1.27.0 darwin/arm64; Apple Clang 21.0.0;
creack/pty v1.1.24 (`edfbf75025b0ba4ee17c19f52d9b600fad80a787`). Module checksums
are retained in the fixture `go.sum.txt`. No product source/build was changed.

## Observations

| Probe / question | Command | Observed result | Limitation / design implication |
| --- | --- | --- | --- |
| Can C++ create a nameless stream and exec Go with only its endpoint inherited? | `./parent ./child` after builds above | FD3 inheritance, bidirectional binary echo with one-byte writes/reads, endpoint EOF and child reaping passed; cleanup 0 ms in recorded run | One synthetic frame; no full product decoder. Supports socketpair/posix_spawn, not future daemon discovery. |
| Does no-argument shell startup produce interactive/job-control semantics? | `go test … -run TestInteractiveShellAndForegroundGroup` | `/bin/sh` detected interactive mode; TIOCGPGRP equals owned direct shell PID (only boolean retained); normal exit 0 with continuous master drainage | Synthetic HOME/ENV/PS1, no private startup files. Waiting without drainage initially exceeded 2 s; independent output reading is required. No full Ctrl-Z/fg job-control matrix. |
| Can Go own a real controlling-terminal session with input and resize? | `go test … -run TestPTYInputResizeTTY` | stdin/out/err all TTY; input marker; `stty size` 31×91; exit 7; second wait returns ECHILD | `/bin/sh` synthetic script; no user startup files or interactive job-control matrix. Supports creack/pty. |
| Does Wait mean PTY EOF or close? | `… -run TestWaitDoesNotOwnPTYEOF` | Wait completed with harness-owned slave still open; macOS post-exit read returned 0/EOF | Darwin controlling-terminal teardown can produce EOF even with another slave descriptor. Initial wait-before-draining fixture exceeded 2 s; draining master allowed completion. This is evidence to keep reading independently, not a universal kernel ordering claim. |
| Can single-owner reaping preserve status under natural exit/close? | `… -run TestSerializedReaper` | 50 alternating natural/close cycles; exact natural exit 17; owner Wait4 polling and signalling; release after reaping; race detector passed | Cannot force actual PID reuse; source ownership invariant addresses that risk. No descendant supervisor or kernel worst-case latency proof. |
| Are arbitrary bytes and final output retained? | `… -run TestRawBinaryAndOrdering` | 20 iterations delivered all 11 bytes including NUL, 0xff, ESC/DSR request and FINAL, exit 13 | First recorded iteration observed all 11 before completion notification; scheduling varies. Does not prove Wait/EOF ordering or full proposed event sequencer. Contract requires sealing and bounded drain. |
| Can input back up without blocking cancellation? | `… -run TestBlockedIOCancellationAndHangup` | raw/no-echo input reached EAGAIN; master close + HUP/250 ms/KILL fixture reaped; 251 ms recorded | Canonical mode discarded input instead of backing up in first fixture. Cooperative raw nonblocking loops are tested; no production poll/wakeup implementation or CPU-load claim. |
| Do attached foreground jobs get normal hangup? | `… -run TestForegroundHangup` | synthetic foreground shell HUP trap wrote private marker after master close | Bounded fixture only; no detached-descendant supervision. |
| Is signal status exact? | `… -run TestSignalStatus` | SIGTERM distinguished from numeric exit, race detector passed | No core-dump generation test. Product wire must keep status tagged. |
| Does IPC loss trigger backend/session cleanup? | `python3 ipc_probe.py` | one-byte framed input; real PTY output; resize 33×97; endpoint loss joined reader/reaped child; 1 ms recorded | Reduced protocol, one session, responsive peer. Does not qualify credit handling, blocked socket writes, full handshake/errors or frontend rendering. |

Full command `go test -v -race -count=1 -timeout=30s ./...`: eight tests passed
in 3.372 s (includes race-runtime overhead). C++ probe and Python IPC probe exited
0. Reproduction is required on other hosts; timing numbers are observations,
not hard realtime guarantees.

## Source findings that constrain interpretation

The Go 1.27.0 Darwin build uses `os/wait_unimp.go`; its source explicitly warns
about concurrent Process.Signal and Wait PID reuse. A completion channel alone
does not remove the interval between kernel reap and publication. Proposed
production policy is therefore a single Wait4/signalling owner, not the
concurrent Wait fixture. No `Cmd.Wait` is permitted after that owner reaps.

`creack/pty.StartWithSize` sets Setsid/Setctty and starts the command; its parent
slave is closed after Start. It does not reap, drain, cancel or supervise jobs.
`exec.Cmd.Wait` owns process wait/status and any exec-created pipe copier cleanup;
PTY `*os.File` streams create no such copiers. It does not close/drain the master.

The source listings were copied byte-for-byte from local disposable probes after
the passing run. Public reproduction regenerates ignored code from those listings;
no `/private/tmp` artifact is needed for resumption. No raw runtime outputs or
downloaded dependency caches are committed.
