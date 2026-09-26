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
| Can single-owner reaping preserve status under natural exit/close? | `… -run TestSerializedReaper` | Final fixture uses pty.Open/os.StartProcess and exact V0 termios readback; 50 alternating natural/close cycles, exit 17, explicit GC and FD/goroutine baseline checks; ten stress repeats (500 cycles) passed in 6.301 s | Prior exec.Cmd external-reap integration failed independent execwait diagnostics; superseded. No forced PID reuse or kernel latency guarantee. |
| Are arbitrary bytes and final output retained? | `… -run TestRawBinaryAndOrdering` | 20 iterations delivered all 11 bytes including NUL, 0xff, ESC/DSR request and FINAL, exit 13 | First recorded iteration observed all 11 before completion notification; scheduling varies. Does not prove Wait/EOF ordering or full proposed event sequencer. Contract requires sealing and bounded drain. |
| Can input back up without blocking cancellation? | `… -run TestBlockedIOCancellationAndHangup` | raw/no-echo input reached EAGAIN; master close + HUP/250 ms/KILL fixture reaped; 251 ms recorded | Canonical mode discarded input instead of backing up in first fixture. Cooperative raw nonblocking loops are tested; no production poll/wakeup implementation or CPU-load claim. |
| Do attached foreground jobs get normal hangup? | `… -run TestForegroundHangup` | synthetic foreground shell HUP trap wrote private marker after master close | Bounded fixture only; no detached-descendant supervision. |
| Is signal status exact? | `… -run TestSignalStatus` | SIGTERM distinguished from numeric exit, race detector passed | No core-dump generation test. Product wire must keep status tagged. |
| Does IPC loss trigger backend/session cleanup? | `python3 ipc_probe.py` | one-byte framed input; real PTY output; resize 33×97; endpoint loss joined reader/reaped child; 1 ms recorded | Reduced protocol, one session, responsive peer. Does not qualify credit handling, blocked socket writes, full handshake/errors or frontend rendering. |

Final full command `GODEBUG=execwait=2 GOGC=1 go test -v -race -count=1 -timeout=30s ./...`:
eight tests passed in 3.413 s (includes race-runtime overhead). The original
normal run at 3b9f576 passed in 3.372 s but did not expose R1's finalizer failure.
C++ probe and Python IPC probe exited
0. Reproduction is required on other hosts; timing numbers are observations,
not hard realtime guarantees.

## Source findings that constrain interpretation

The Go 1.27.0 Darwin build uses `os/wait_unimp.go`; its source explicitly warns
about concurrent Process.Signal and Wait PID reuse. A completion channel alone
does not remove the interval between kernel reap and publication. Proposed
production policy is therefore a single Wait4/signalling owner using os.StartProcess
and pty.Open, not an exec.Cmd custom-reap path or concurrent Wait fixture. No
Process.Wait or Cmd.Wait runs concurrently/after that owner reaps. Independent
Astra review reproduced an execwait finalizer panic in the old proposal under
GODEBUG=execwait=2/GOGC=1: Release leaves Cmd.ProcessState unset. Remediation
uses a spawn API with no Cmd contract to bypass; full stress command is in README.

The [second review](https://github.com/weshofmann/agent-vision/pull/5#issuecomment-5845357080)
identified a verification defect: Release changes Process.Pid to -1, so the old
second-wait assertion used an any-child wait. The final fixture saves a positive
ownedPID before Release, uses it throughout, checks Release's error, and requires
ECHILD for that exact PID. The full eight-test run and 500-cycle stress above were
repeated after this correction. Earlier timings do not qualify this corrected
assertion.

`creack/pty.StartWithSize` sets Setsid/Setctty and starts the command; its parent
slave is closed after Start. It does not reap, drain, cancel or supervise jobs.
`exec.Cmd.Wait` owns process wait/status and any exec-created pipe copier cleanup;
PTY `*os.File` streams create no such copiers. It does not close/drain the master.

The source listings were copied byte-for-byte from local disposable probes after
the passing run. Public reproduction regenerates ignored code from those listings;
no `/private/tmp` artifact is needed for resumption. No raw runtime outputs or
downloaded dependency caches are committed.

## Independent-review remediation probes

Independent findings [R1–R4](https://github.com/weshofmann/agent-vision/pull/5#issuecomment-5845237954)
are anchored to 3b9f576. These are additional bounded evidence, not product migration.

| Question | Reproduction command (see README setup) | Result | Limitation/design effect |
| --- | --- | --- | --- |
| R1: is Cmd-independent spawn compatible with GC/finalizer diagnostics? | `GODEBUG=execwait=2 GOGC=1 go run -race ../reaper-alternative/main.go` | 100 serialized natural/close cycles passed in the initial alternative | Used fresh termios; final 500-cycle test additionally configures/readbacks accepted V0 policy and checks baselines. Supports os.StartProcess choice. |
| Do fresh PTY defaults equal V0? | `go run ../termios-compare/main.go` | Input/control/local flags and speed differ; output/Ctrl-C/erase match | Narrow Darwin policy comparison, not full interactive behavior. Configure accepted explicit policy before spawn. |
| R2: can credit/cancel progress when ordinary slots are saturated? | `python3 .probe/protocol-model.py` | 2 tests passed: 48 ordinary slots saturated, credit and Close still admitted; 81 total lane capacity; partial input cancelled with one reply each; successful read held across exit seal; output seq1/seq2 before Exited(seq2); send does not charge credit again; double/late-credit handling checked | Deterministic ledger/sequencer model, not real IPC implementation or exhaustive concurrency. Supports separate control lanes/bypass. |
| R3: does Shutdown/EOF own in-flight Create resources? | `python3 .probe/startup-model.py` | 10 threaded barrier cases passed before open/after open/after spawn/before Created/after Created, for Shutdown and EOF; conceptual FD/child balances zero; one Create reply if deliverable; cleanup before Shutdown Ack | No actual kernel PTY/FD/child qualification; model enforces chosen schedules. Defines reservation/commit/rollback boundary. |
| R4: does frontend policy finish local cleanup if core stops responding? | Build command in README; `.probe/core-watchdog/watchdog_probe --self-test` | 3 native cases: running-stalled, SIGSTOP-stalled, handshake-stalled. Asserted 2 s unanswered stage, ≤5 s total, local worker joined/restoration marker before TERM, TERM then KILL, actual SIGKILL wait status, second wait ECHILD; observed stage 2000–2002 ms and total 2266–2268 ms | Real direct child and local worker, reduced byte protocol and synthetic restoration marker; no actual Turbo Vision restoration API or shell/descendant cleanup after forced death. Supports explicit frontend watchdog policy. |
