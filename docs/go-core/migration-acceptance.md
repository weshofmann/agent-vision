# Go core migration acceptance

## PR2 scope and gate

Status: Tasks 7–9 have passed their independent task gates. Task 10 implementation
and native presentation qualification are submitted for review; its complete Go
qualification remains unresolved. Task 10 began at
`875fb1277fc53849d078921d819ca48f34b98e4c`. The [Task 9 gate closure](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5851235133)
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

Final source tested: `1a72981322c24009433e98709d3f1c76ab7d2e3c`.
Default OFF: **36/36 passed, 53.07s**. Opt-in ON: **39/40 passed, 109.66s**;
`core_go` failed because `TestNativeSixteenStartsEndpointBaselines` reported
`native start failed: errno -6` at its spawn-error branch (0.41s). This is an
unresolved native-start watch, distinct from the historical two-second timeout.
The original error did not identify the failing PTY/spawn/rollback stage; absence
of a cleanup error does not recover unreported counts or establish cleanup proof.
One approved copied-source isolated race diagnostic passed all sixteen starts
and cleanup (FD4, goroutines2). That pass is nondiagnostic: copied instrumentation
and isolated ordering can change scheduling. There was no retry loop, timeout
increase or retained Go change. Full Go qualification is not waived and Task10
has not passed its independent gate.

The full native `ipc_terminal` test passed in **16.12s**: two independent
long-lived ordinary Go cores each processed sixteen 1MiB payloads through actual
production presentation, exact publication/consumption/Credit completion before
producer exit, final lastSequence/flush and retained emulator/history content,
explicit A Close while B continued, Shutdown/loss and actual termios/cooked-input
restoration. Final quiet observations for cycles0–15 are:

| Sample | Normal core | Loss core |
| --- | --- | --- |
| Kernel threads | 14,14,14,14,15,15,15,15,15,16,16,16,16,16,16,16 | 14,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15 |
| Frontend threads / FDs / direct children | 8 / 14 / 1 each cycle | 8 / 14 / 1 each cycle |
| Core FDs / direct session children | 11 / 1 each cycle | 11 / 1 each cycle |
| Owned presentation workers | Two joined per A, then two B at teardown | Two joined per A, then two B at teardown |
| UI restoration acknowledgement | 22ms; core exit0 graceful | 18ms; direct core SIGKILL9, EOF contact loss |

These are finite bands with late plateaus, without a claim about kernel-thread
identity/cause or universal cleanup. Every pre-load/started/consumed/after-cycle/
fixed250ms quiet sample remains in durable ignored evidence; earlier runs and
failed stages remain separate. The local Apple100ms Task9 allowance is not used
for Go growth. Native core binary remains CGO-disabled; Go-race results are
separate. No descendant cleanup promise after core crash or restoration guarantee
for SIGKILL of the frontend is made.

Both complete builds compile without warnings; the inherited pinned configure
deprecation remains. Durable evidence preserves source-relative compiler inputs,
exact configs/flags, binaries, logs, synthetic raw terminal captures and hash/copy
verification. Original ordinary `go test` deleted its temporary race executable;
its exact source/command/log and original build cache are retained, without a
claim that the exact failed race executable was recovered. Later documentation
changes do not alter tested compiler inputs. Failed compiler/fixture attempts,
including native7–9 stale-binary runs, remain explicitly invalid for changed-source
coverage. The final independent Task10 and whole-PR reviews remain outstanding.


### Task10 round1 review fixes (gate remains blocked)

[Independent review T10-R1–R3](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5851652877)
requires further native qualification. The original full Go qualification remains
39/40; a newly authorized copied full-module race diagnostic reproduced errno-6
inside `pty.Open`, before termios/resize/nonblock/shell launch. In that diagnostic,
thirteen resources were acquired before the fatal error and existing cleanup
returned to FD4/goroutines2. This is diagnostic evidence, not passing shipping-Go
qualification or a cause/host-only conclusion; exact inner operation remains
unknown. All race executables/completed build work were retained before Go-driver
execution. The first copied full-module attempt lacked golden fixtures and is
explicitly invalid; its evidence remains separate from the valid replacement.

T10-R2 now requires successful sixteen-cycle completion, sixteen Credit barriers
and sixteen A completions plus a latched intentional direct-core kill or Shutdown.
Any earlier restoration/contact failure fails qualification while still restoring
and joining. Completion requires exact normal exit0/contactNone or intentional
SIGKILL9/contactEOF. The premature-loss probe fails at cycle0/credits0, restores
termios/cooked input and joins workers; the previous harness falsely returned
success and the focused pre-fix RED preserves that observation.

T10-R3 corrects the earlier visible-caption claim: the archived original loss
capture did **not** display the caption. Its workload/resource/restoration evidence
remains valid. The corrected harness uses the pinned `TEventQueue::waitForEvents`
display/FPS path while waiting boundedly for one outer-key acknowledgment. The
outer decoder sends that key only after actual terminal cells contain
`authority/contact lost`; native consumes it before teardown, so a later screen
erase cannot precede the observed caption. No sleep/title-getter substitute or
Go-session input is used. Removing publication fails the probe and safely restores.

Focused native tests passed2/2 in25.91s: intended normal+loss16MiB runs16.84s and
premature-loss/omitted-publication negatives9.06s. Positive restoration ACKs41ms
normal/14ms loss; premature-loss failure15ms; omitted-publication failure1027ms
uses its one-second display observation deadline within the unchanged two-second
restoration watchdog, and is not a positive100ms restoration claim. All captures,
counts and failed attempts remain ignored durable evidence. DefaultOFF36/36
passing evidence is retained with all613 actual default compiler inputs verified
byte-identical; only opt-in native test registration/oracles changed. Task10 remains
open pending scoped re-review and explicit disposition of T10-R1.


Round1 code checkpoint `851cf2537ecde79934079c7888bf86cf988afea5` is byte-identical
to the compiled native source/fixtures (later acceptance text only differs).
A single subsequently authorized deeper copied pinned-PTY diagnostic used a local
modfile/replace and error-only Open/ioctl labels. Its full-module race run passed,
including all sixteen starts, so it did not identify the failing inner operation.
The original shipping failure and reproduced `pty.Open` phase remain unresolved;
this passing diagnostic is neither a cause nor a shipping-Go qualification pass.
No additional Go execution or retained Go/dependency change followed. Existing
pyte desktop-test dependency is reused for decoded native captions; CMake supplies
the same `.probe/tools` PYTHONPATH for the opt-in native tests.


## Final whole-PR minor corrections

The final bounded fix wave addresses Task7-R1 (known nonregular executable paths
report deterministic `EACCES`; failed regular-file access keeps syscall errno),
T9-R1 (resize-overflow cancellation is local presentation cancellation), and
T10-R4 (the outer-PTY oracle uses incremental `pyte.ByteStream`). Offline proof
compares exact cells/cursor across all 63 single-cut byte boundaries and
byte-at-a-time input, including wide glyph columns, and replays retained positive
and omitted-publication captions. T9-R2 remains deferred: pinned configure
compatibility warnings do not authorize dependency changes.

Code checkpoint `adfc342b6ef57d833becab997a5d2d82195d529e`: focused tests **2/2
passed, 2.49s**; fresh default OFF build compiled without C++ warnings, retaining
the inherited pinned libvterm CMake configure deprecation. Full default suite
**36/37 passed, 55.41s; FAILED**: `ipc_session_reserve` rejected “peer accepted exact
DSR/CPR as slot48 behind saturated users.” The existing local desktop test passed
9.25s with its interaction, cleanup and outer-terminal restoration checks. No
retry or follow-up source change was made. The new offline decoder test accounts
for the added registration. Initial process RED source/log are retained, but its
executable was overwritten by the next focused rebuild before archival; that
original binary is unavailable. GREEN/final binaries were archived before tests.

Both the new default qualification failure and Important T10-R1 remain unresolved;
the shipping opt-in Go qualification remains 39/40 failed with unknown inner
`pty.Open` cause. No Go or native heavy test was rerun in this wave. This is a
scoped review checkpoint, not Task10 completion or operator acceptance.
