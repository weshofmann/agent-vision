# Go core migration acceptance

## PR2 scope and gate

Status: Tasks 7–9 have passed their independent task gates. Task 10 begins at
`875fb1277fc53849d078921d819ca48f34b98e4c`; real-core presentation and complete
qualification are pending. The [Task 9 gate closure](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5851235133)
authorizes this bounded harness milestone.
Base main: `3bc0b00d691739f3b472f3e0509f2db4c0ecf850`, the merged PR1.
[PR1 operator acceptance](https://github.com/weshofmann/agent-vision/pull/8#issuecomment-5849812647)
authorizes the next boundary when separately assigned. This PR executes only
Tasks 7–10 of the [approved migration plan](../superpowers/plans/2026-09-26-go-core-migration-plan.md).

The C++ client and presentation adapter are opt-in. The existing local C++ PTY
desktop remains the default; its lifecycle path and tests remain in place.
Go is authoritative for IPC session/process lifecycle. The frontend owns only
its direct Go-core child and never infers shell exit from EOF or contact loss.
No PR3 cutover, dynamic terminals, daemon, persistence, reconnect, remote access,
or platform expansion is part of this PR.

| Task | Deliverable | Required evidence | Current gate |
| --- | --- | --- | --- |
| 7 | C++ shared-corpus AVCP codec and core-child owner | Every golden frame, bounded malformed parsing, native FD3/exec/reap/escalation | Independently gated |
| 8 | Shared connection and bounded per-session endpoints | Fragmentation, partial I/O, control/reply reserves, isolated cancellation, consumed-credit and staged final status | Independently gated |
| 9 | tvterm transport seam and IPC adapter | Owned payloads, real DSR/CPR, silent resize, lock callbacks, final flush, local selection and default local lifecycle | Independently gated |
| 10 | Opt-in real-core presentation harness | Two sessions, actual emulator/view locks, loss/close retention, repeated heavy resource observations, complete default and opt-in suites | Pending |

Task checkpoints receive independent specification and quality review.
Final handoff requires exact head/tested SHAs, commands/results, qualification
limits and deviations, plus independent Astra/high whole-PR review. Keep Draft;
stop at operator PR2 acceptance without starting cutover or removing local PTYs.

## Carry-forward qualification

PR1's heavier workload showed an unclassified kernel-thread increase from 8 to
14. PR2 must measure repeated heavier frontend/core sessions within one long-lived
core to determine whether the count stabilizes or grows. Runtime initialization
is a hypothesis, not the accepted explanation. FD/child cleanup and Go worker
joins remain separate observations. Escalate only on evidence of session-related
unbounded growth or another owned-resource defect; this is not runtime-redesign
scope.

The historical native sixteen-start timeout remains an unresolved evidence
limitation. If it recurs, preserve exact SHA/source, stage, session/resource counts,
cleanup result and bounded diagnostics before retrying. Prior passing runs do not
diagnose that failure. PR1's overwritten original desktop capture remains lost;
its separate reproduced fixture correction and evidence retain their provenance.

Implementation and qualification results will be recorded here at their tested
checkpoints; no result is claimed by this initial documentation checkpoint.

## Actual-core presentation qualification

The opt-in `ipc_terminal` CTest runs the real production connection, bound IPC
transport, concrete controller, libvterm, BasicTerminalWindow, TerminalView and
scrollbar/render locks inside a real outer PTY. Its synthetic executable uses
only the approved absolute `SHELL` startup policy and ordinary Input/PTY bytes.
It is separate from the real `/bin/sh` Go native tests and unchanged default
`desktop_pty` qualification.

Each normal/loss invocation uses one long-lived core: B stays alive while sixteen
successive A sessions each emit exactly 1 MiB of X data plus explicitly verified
PTY CR/LF translation. A remains at its completion barrier until exact emulator
bytes, publication, released consumption and final Credit completion are proven.
The test-only read-only ledger snapshot uses the exact production connection
source under its existing connection→endpoint locks; it checks current binding,
usable contact, zero raw/pending bytes and no pending Credit ticket. No shipping
API or instrumentation is added. The link map records that the client archive's
connection object is not extracted twice. All locks are released before UI or
transport actions.

Ordinary libvterm CPR precedes subsequent user input; both sessions receive their
own focused input and native geometry. Exited 7 stays hidden behind the final
flush barrier. After the last sequence is consumed and the real emulator updates
state, explicit flush publishes exact status; retained bottom history contains
the final marker. Local finish joins both presentation workers, permits the same
Exited binding's explicit Close, and B answers a fresh Input afterward. Core loss
shows loss of authority/contact, retains local scroll/selection and never creates
an exact shell exit code. The frontend signals/reaps only its direct core child.
Forced core death provides no descendant cleanup or SIGKILL terminal-restoration
promise.

The lifecycle callback waits for bounded UI restoration acknowledgment. The UI
continues servicing events, finishes presentation outside callbacks, destroys
views and explicitly suspends Turbo Vision before acknowledgment; only then does
it join core cleanup. The outer fixture verifies exact termios after a successful
cooked `outer-check` line, matching the retained desktop fixture. Immediate
PENDIN observations remain in ignored diagnostics; no termios bit is masked.

### Approved bounded deviation

The initial unthrottled burst exposed rejection of valid next sequence 196 with
117,060 raw bytes outstanding solely because 128 small physical chunks were
queued. The [designated ruling](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5851342800)
authorized adjacent queued-tail coalescing. Every wire sequence still enters the
exact ledger; borrowed vectors remain immutable, owned chunks/capacity stay at
most 32,768 bytes, raw credit window remains 256 KiB and the 128-slot bound stays.
Greedy adjacent packing bounds allocation slack separately from raw credit;
capacity never grows beyond the chunk bound. The matching connection/fixture
changes are the only retained production deviation from Task10's nominal file
list. Go semantics, pins, wire and the default local desktop are unchanged.

`core_connection_output-fragments` demonstrates RED→GREEN for 1,024 one-byte
frames while an earlier chunk stays borrowed, ordered bytes, immutable ownership,
partial-only Credit, exact aggregate Credit, latest constituent sequence, rejected
early flush and retained Exited Close. The previous 129-byte negative fixture is
preserved in history; `core_connection_failures` now violates the actual 256 KiB
raw window instead. Existing sequence/seal/cancellation tests remain.

### Retained regression mapping

All old tests remain enabled. This matrix identifies concrete Go/adapter proofs;
it authorizes neither removal nor default cutover.

| Retained local regression | Go/core or adapter proof |
| --- | --- |
| `lifecycle`: natural reap without closing window, exact wait status | `TestStartupExitedRecord`, `TestWaitInterruptReleaseAndMapping`, `TestEOFBeforeWait`; `ipc_terminal` deferred exact exit 7 and retained final marker |
| `shell_fallback`: invalid shell falls back | `TestShellFallback`, `TestSpawnFailureRollback`; real `/bin/sh` native process/client tests |
| `controller_shutdown`: join workers and own-child cleanup | `TestOutputAbortJoinsMonitorAndReader`, `TestCommandNativeCancelJoinFDReuse`; `ipc_terminal` owned presentation joins + restoration before core join |
| `lifecycle_cycles`: repeated resource cleanup | `TestSerializedReaper`, `TestOutputRetiredMonitorJoin`; repeated actual-core MiB cycles with every frontend/core thread/FD/child count and separate worker completions |
| `lifecycle_signal`: signal shutdown, no stale PID action | `TestUncertainWaitStopsSignals`, `TestSignalFailureRetainsOwner`, `TestSerializedReaper`; `core_process` and `core_connection_stopped/uncertain` own only direct core child |
| `lifecycle_resize`: actual geometry and resize ordering | `TestCommandNativeResizeSizeSignalAndFlags`, `TestDarwinResizeWaitInterleavings`, `TestCommandResizeErrorContinues`; `terminal_transport` silent resize and `ipc_terminal` actual view geometry |
| `lifecycle_foreground`: controlling terminal/job control | `TestInteractiveJobControl`, `TestDarwinPTYAndTermios`; real-shell independent core client |
| `lifecycle_blocked`: saturated writes cancel | `TestPartialInputTimeout`, `TestCloseBypassesBlockedInput`, `TestCommandShutdownBypassesBlockedInput`, `TestPollWriteAndFailure`; `core_connection_blocked/partial` |
| `lifecycle_held-slave`: child exit despite open slave | `TestOutputNativeHeldSlaveNaturalExit`, `TestHeldContinuousSlaveDrainNative`, `TestWaitBeforeTail`, `TestReadHeldAcrossExitSeal`; adapter final lastSequence/flush/status |
| `lifecycle_qualification_probes`: reject unjoined worker/FD/unowned thread | Retained probes remain; exact IPC worker nonjoinability after finish, full per-cycle FD/child accounting, Go join tests. Kernel thread plateau is a separate finite observation, without applying the local Apple100ms allowance |
| `scrollback_lock`: actual state-held scrollbar callback | Retained forced lock-order regression plus `ipc_terminal` real windows/views/rendering; snapshot locks released before UI/transport calls |
| `terminal_transport`: owning chunks, DSR, resize, End/Lost, local retained events | Retained concrete controller/libvterm seam tests; `ipc_session`, `ipc_session_reserve`, `core_connection_output-fragments`, real `ipc_terminal` |
| `source_guard`: exact pins/atomic composition/rejected indexes | All 24 real-Git source guard cases retained, including both composed patches and hidden flags |
| `desktop_pty`: focus/input, movement/resize, scroll, close/quit, restoration | Unchanged default desktop retains complete local qualification; opt-in `ipc_terminal` proves IPC view/input/geometry/lifecycle/loss/restoration, without claiming PR3 native UX or runtime cutover |

### Reproducible commands and limits

Use qualified macOS26.6.2 arm64, AppleClang21, SDK27, CMake3.31.10 and Go1.27.0.
`CMAKE`/`CTEST` denote those qualified executables; `GO` is an absolute qualified
Go executable. The source override must be the current exact composed Task9
pinned checkout, not an older lifecycle-only dependency. Existing source guard
validation applies to it. Full default and opt-in commands:

```sh
"$CMAKE" -S . -B .probe/task10-off -DCMAKE_BUILD_TYPE=Debug \
  -DAGENTVISION_BUILD_CORE=OFF -DFETCHCONTENT_SOURCE_DIR_TVTERM="$PWD/.probe/task9-tvterm"
"$CMAKE" --build .probe/task10-off -j 6
"$CTEST" --test-dir .probe/task10-off --output-on-failure
"$CMAKE" -S . -B .probe/task10-on -DCMAKE_BUILD_TYPE=Debug \
  -DAGENTVISION_BUILD_CORE=ON -DAGENTVISION_GO_EXECUTABLE="$GO" \
  -DFETCHCONTENT_SOURCE_DIR_TVTERM="$PWD/.probe/task9-tvterm"
"$CMAKE" --build .probe/task10-on --clean-first -j 6
GODEBUG=execwait=2 GOGC=1 "$CTEST" --test-dir .probe/task10-on --output-on-failure
```

The native core is an ordinary CGO-disabled `-trimpath -buildvcs=false` build;
`core_go` separately runs the whole Go module with `-race` and CGO enabled.
Execwait/GC settings are qualification settings, not ordinary CTest defaults.
The pinned libvterm configure compatibility deprecation remains explicitly
retained; warning-free compilation is a separate claim.

Final exact-source results and all per-cycle observations are pending below;
failed fixture/compiler/stale-binary attempts remain in durable ignored evidence
and will not be represented as successful changed-source coverage.
