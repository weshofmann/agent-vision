# Go core migration acceptance

## Current parent acceptance and cutover status

The operator [accepted PR #9 and closed T10-R1](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5859566997)
for the bounded mitigation and qualified Darwin arm64 checkpoint at accepted
head `9e4c5fb954401be43e3289ae1a4d12d90844338e`, runtime-qualified code
`40ab6e62685e5aac4f5c08c618ae00b7aea1ff40`. Parent integration is main
`9758954a7a6d0ff53848f460c6f7e5428375320b` (same tree as the accepted head).
The [continuation](https://github.com/weshofmann/agent-vision/pull/11#issuecomment-5859569996)
authorizes Tasks 11–12 on the existing cutover branch; PR #11 remains Draft.
Task 11 passed its [fresh independent scoped gate](https://github.com/weshofmann/agent-vision/pull/11#issuecomment-5860047264)
at `af8a2ec55c5c723ac05da5c530c2b68b6a758d35`. The cutover shipping frontend
uses only IPC; retained local lifecycle code/patches/tests await a separate
Task 13 authorization after manual operator equivalence acceptance.

T10-R1 closure does not establish native retry efficacy or the historical kernel
cause. The native comparison observed no retries. The accepted finite heavier
runs reported quiet core thread plateaus 15/15/17, with recorded FD/direct-child
baselines recovered; they do not establish lazy initialization or a universal
resource bound. Historical failures and missing historical executed-binary
identities below remain disclosed. Earlier open/pending status statements are
historical observations, superseded by the exact operator disposition above.

## Task 12 sibling package checkpoint

The candidate defaults the core ON and rejects OFF, preserving exactly Go
1.27.0 darwin/arm64, readonly module builds, trimpath, source-integrity guards
and source/module/patch rebuild edges. CMake installs the shipping frontend and
core together in `bin`, with six complete notices and this repository's notice
summary in `share/agentvision/licenses`. No project license or redistribution
clearance is selected.

The package regression first failed because CMake install omitted the frontend.
The default/OFF regression first failed because OFF was accepted. After the
rules were added, the actual installed and relocated package (path with spaces)
ran two distinct Go-owned sessions from an unrelated cwd with a runnable PATH
impostor. Missing/nonexecutable siblings and literal incompatible/malformed
HelloAck peers produced one startup diagnostic with no PATH execution or local
fallback; rejected direct owned peers were absent after frontend exit. A stopped
real core restored the alternate screen, cursor and terminal configuration
before core escalation, then was reaped with a forced-cleanup diagnostic.
Cooked input and exact post-input termios readback were restored in every case.
These observations qualify same-host relocation, not universal portability.

Two initial stronger stopped-core oracle attempts failed and remain retained.
Investigation found only Darwin's `PENDIN` transient pending-input state differed
from the prelaunch baseline. The oracle excludes only that state bit before
escalation, while comparing every configuration/control field and requiring
cursor visibility plus alternate-screen restoration while the stopped core is
still alive. Deterministic negative tests reject actual flag, speed and control
character changes; the cooked-input roundtrip still requires exact unmasked
termios equality. The [pinned Apple tty source](https://github.com/apple-oss-distributions/xnu/blob/xnu-12377.1.9/bsd/kern/tty.c)
sets pending-input state when canonical processing resumes and clears it during
input handling; this is a mechanism reference, not identification of the running
kernel. The instrumented investigation script was retained but lacked its own
contemporaneous pre-run hash; subsequent wrappers archive executed scripts.

Targeted verification used the existing Debug build with successful configure
and build gates: `core_build_config` passed required-default/OFF/tool/target,
source-integrity and source/module/patch rebuild checks; the amended covering
CTest selection `core_process|package_siblings|desktop_termios_oracle|desktop_pty`
passed 4/4 (36.97 s, including 17 desktop scenarios). After the peer audit was
strengthened to require the intended literal reply actually be sent,
`package_siblings` passed again (6.69 s). An earlier 4/5 covering result included
one redundant direct seam registration failure: that submode requires its
isolated copied executable, already exercised by `core_process`. The redundant
registration was removed; the failed attempt remains retained. These are
covering checks, not a fresh whole qualification.

Current fresh whole qualification, independent packaging/whole-PR review and
operator acceptance will be recorded separately. All manual checks below remain
unchecked; automated checks do not qualify native UX or authorize Task 13.

- [ ] Focus/z-order/Ctrl-B menu and keyboard/mouse move, resize, maximize.
- [ ] Independent A/B input routing and stty rows/columns after resize.
- [ ] Finite scrollback/selection followed by input and UI interaction.
- [ ] Unicode/raw escape/DSR response behavior.
- [ ] Ctrl-C, Ctrl-Z and fg under Go shell ownership.
- [ ] Close/quit cancellation and confirmation; natural exit/status retention.
- [ ] Stopped/crashed core behavior and outer terminal modes/cursor restoration.
- [ ] Operator equivalent-evidence/manual acceptance before lifecycle removal.

## Historical PR2 scope and gate

Status: Tasks 7–9 have passed their independent task gates. Task 10 implementation
and completed native qualification are submitted for final independent review;
T10-R1 disposition and operator acceptance remain unresolved. Task 10 began at
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
It now uses `tests/retained_go_test.py` to run the actual qualified Go child with
`-work -count=1` and a fresh durable ignored GOTMPDIR under
`.probe/go-test-evidence/`. WORK, executed test binaries/hashes, exact command,
selected environment, source/replacement archive/digests and outputs survive
success and failure. Go120s/CTest150s caps and the default desktop are unchanged.
This future retention does not repair historical ordinary-Go-test binary gaps.
For direct whole-module qualification and current held correction gates, follow
[the Darwin recovery retention recipe](darwin-recovery-implementation.md#correction-cycle-1-fixture-ownership-causal-cancellation-and-retention).
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

### WPR-F1 bounded fixture repair checkpoint (failed focused verification)

The [supervisor's fixture-only authorization](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5852247553)
followed the approved single-event diagnostic, which established that a merged
`ready` + DSR event produces the real `ESC[0n` reply while the old exact-five-byte
readiness gate stays false. That diagnostic was executed after the earlier
rejections; it did not identify the callback in the original failed qualification.

This test-only checkpoint replaces callback-count acceptance with ordered stream
and actual adapter-admission observations. The peer keeps all 48 ordinary
requests unacknowledged until Shutdown; it sends the second query only after
cumulative real Credit reaches 13 bytes (8 initial query bytes plus 5 readiness
bytes). The callback blocks that readiness consumption until the test has staged
4,086 bytes behind the pending 10-byte real reply. A fragmented schedule releases
each readiness byte after Credit for the preceding prefix. An early merged-query
control and unrelated-EOF control exercise rejection by the overflow oracle.
Production code, controller patches, pins and wire policies are unchanged.

The one declared focused batch completed **24/25 passed, 24.70s; FAILED**.
Normal reserve and forced fragmented readiness passed: exactly 47 ordered user
admissions, slot48's actual 10-byte DSR/CPR reply, 4,086 queued filler bytes and the
subsequent real four-byte DSR response returning `Overflow`. Both restored the
FD baseline, joined presentation workers and completed contactNone/graceful peer
exit0. The early-query negative rejected actual DSR generation before filling.
The separate no-core merged-event characterization also passed.

`ipc_session_reserve_loss` failed the assertion requiring peer exit0 without
escalation; the failure did not report which completion field violated it.
The production loss path calls `requestStop()` for nongraceful completion, so the
negative's no-escalation expectation is suspect. This is source-based inference,
not diagnosis of that actual child's exit. Exact source, binaries, commands and
result logs were preserved before further action. The controller stopped this
cycle: **no correction, retry, second focused batch or complete OFF qualification**
was performed. The existing failed OFF and single-event archives remain unchanged.
WPR-F1 and T10-R1 both remain open; this checkpoint claims neither fixture closure
nor Task10/PR2 acceptance. No Go core or native-start test was executed.

### WPR-F1-R1 authorized EOF cleanup predicate correction

The [supervisor's WPR-F1-R1 authorization](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5852401363)
confirmed the unconditional positive-cleanup assertion was incompatible with the
cooperative EOF negative. The corrected fixture uses distinct predicates: positive,
fragmented and early schedules require graceful exit0, contactNone, systemError0
and no TERM/KILL; EOF requires nongraceful EOFReached, systemError0, no KILL and
either reaped exit0 (with or without a recorded TERM attempt) or SIGTERM with an
owner-recorded TERM. Other signals, nonzero exits, uncertain completion, wrong
contact and system errors are rejected. A 27-row matrix exercises these same
live predicates; no process or production hook was introduced.

Completion, phase bytes/admissions, worker joins, state accessibility and measured
FD baseline/result are now printed before the post-join assertions. The EOF
negative independently requires exact query/readiness and consumed13, the actual
10-byte queued reply plus 4,086 queued filler bytes, genuine Lost, no timeout or
early query and no subsequent query/reply/Overflow. Positive reserve enforcement
and the causal Credit13 barrier are unchanged; the fake peer is unchanged.

One predeclared reserve/matrix batch passed **6/6, 1.95s**; the separate affected
shared-peer batch passed **20/20, 22.01s**. In this new measured EOF run, completion
was Signaled/SIGTERM15, systemError0, termSent1, killSent0, EOFReached/nongraceful;
workers joined, retained state was accessible and descriptors restored5/5. Normal,
fragmented and early schedules retained strict exit0/noTERM/noKILL/contactNone/
graceful completion and descriptors5/5. These new fields do not recover or identify
the missing completion fields from the prior failed run. No retry was performed.
The single fresh complete OFF qualification follows this committed checkpoint;
its result and independent scoped review remain pending. T10-R1 remains open;
no Go/ON/native-start test or PR2 acceptance is claimed.

The single fresh default-OFF qualification at code checkpoint
`c2bd9f08c990010ff9d0fd1886c026574a7a774c` passed **42/42, 54.71s**.
Configure/build succeeded with unchanged pinned inputs; C++ compilation emitted
no warnings and the inherited libvterm configure deprecation remains. The default
`desktop_pty` passed **9.30s**, including interaction, cleanup, cooked input and
exact termios restoration. No Go/ON/native-start/heavy qualification ran.

Full-OFF normal, fragmented and early completions again met strict exit0,
systemError0, noTERM/KILL, contactNone/graceful and descriptors5/5. Its EOF
completion was Signaled15, systemError0, termSent1, killSent0, EOFReached and
nongraceful, with consumed13, only the real10-byte queued reply plus4086 queued
filler, genuine Lost and no second query/reply/Overflow; workers joined and
retained state remained accessible. These current results preserve both the
original failed OFF qualification and the failed cleanup-oracle checkpoint as
historical failures, without diagnosing their unreported completion fields.
Later documentation-only commits do not change the tested code. WPR-F1 and
WPR-F1-R1 await scoped independent review/operator disposition; T10-R1 remains a
separate blocker and PR2 acceptance is still held. PR9 stays Draft and unmerged.


### T10-R1 overnight investigation and unchanged qualification gate

The [overnight envelope](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5853204271)
permits bounded independent diagnostics, documentation and dependent preparation.
It explicitly records **WPR-F1/WPR-F1-R1 closed** and **T10-R1 open/blocking**;
this supersedes the historical pending-disposition statements above without
reopening the reserve fixtures. Default-OFF evidence remains the 42/42 run at
code `c2bd9f08c990010ff9d0fd1886c026574a7a774c`; no new OFF/ON qualification
or real-core/heavier-workload resource qualification was run this session.

The completed [master-only result](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5853191996)
was used without rerunning: 32 acquisitions, one concurrent master-Open signed
errno -6, 31 successful owned closes. Additional design 1 exercised the complete
pinned Darwin PTY-open path using an explicitly named, isolated diagnostic copy.
It preserved the existing Open flags, original ioctl/Syscall helper, slave
OpenFile and sole File-owner cleanup. Instrumentation can alter scheduling;
this is not the unchanged shipping executable.

[Independent preflight](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5853456008)
closed diagnostic D1-R1 after exactly one reporting-only RED and GREEN test.
The actual extracted helper initially lost nonnumeric error messages; the two
expected message cases failed while exact negative-six and nil cases passed.
The reviewed correction quotes the inner nonnumeric error while omitting the
outer PathError path/message. All four GREEN cases passed. No PTY/ioctl/core
operation occurred in these reporting tests, and no product code changed.

The [single design 1 execution](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5853484983)
reproduced typed errno `0xfffffffffffffffa`, unsigned 18446744073709551610,
signed -6 at master `syscall.Open`, concurrent round 2/attempt 6. It stopped at
48 acquisitions after joined normal cleanup: 47 successful pairs, 94 individually
recorded nil slave/master File.Close results, 141 same-call ioctl tuples with
raw errno 0. The failed master Open returned before file ownership. Selected
test deliberately FAIL after 0.84s; process exit1, supervised 1.297992125s, no timeout,
forced termination, stderr or race report. The single offline build succeeded.

Approved source bundle SHA256:
`1291eb8f808ae1282616908ec1fe4b22e6e7f6fcdfe4f6427580936f8972c492`.
Preserved race executable SHA256:
`2418303dfdde928f699e738cd54b3c1edcd9626a5541bfc267bd6f5516d593b7`.
Metadata reports qualified Go1.27.0, CGO1, race, Darwin/arm64. Source/binary
identities, exact commands and distinct raw results remain durably preserved.
[Independent result audit](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5853493435)
reconciled all records and found no unresolved probe-owned cleanup. This is
neither a host FD baseline nor proof of kernel-internal cleanup.

Historical R14 stage2 marks the entire `pty.Open()` call, not its name ioctl;
its inner operation remains unknown. A proposed name-ioctl experiment based on
the mistaken boundary was stopped during source preparation, before any build
or run. The second additional diagnostic allowance remains unused. No further
native experiment is queued without a new discriminating question.

The matching master boundary does not establish a running-kernel branch,
underlying cause, retained correction, retry/serialization policy or restored
shipping qualification. **T10-R1 remains open**; broad OFF/ON reruns and retained
Tasks 11–12 cutover remain gated. The heavier frontend/core kernel-thread growth
question remains pending; no lazy-initialization explanation is assumed.
The default local C++ desktop and lifecycle coverage remain intact. Task 13
removal, manual equivalence acceptance and PR acceptance are not authorized by
these observations.

### Current recovery qualification and final review boundary

After [independent correction-cycle clearance](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5859335377),
code `40ab6e62685e5aac4f5c08c618ae00b7aea1ff40` passed one retained whole-module
Go race invocation (25.292 s), fresh default-OFF 42/42 CTests (54.385 s), and fresh
opt-in-ON clean-first build plus 48/48 CTests (116.713 s). Three additional fresh
normal/normal/direct-core-loss workloads passed: 51 creations, 48 heavy A cycles,
48 zeroed Credit completions, 195 resource records and 51 joined worker records.
The automated restoration/cooked-input oracles passed; manual acceptance is not
claimed. Quiet core thread plateaus varied across fresh processes (15/15/17),
with quiet frontend/core FDs 14/11 and direct children 1/1 while B remained alive.
These finite observations establish no universal thread bound or growth cause.

[Detailed results, resource stages and evidence limits](darwin-recovery-implementation.md#completed-qualification-at-40ab6e6-final-review-pending)
retain the original failed whole-module run, original comparison/nested SHA
attribution and missing historical executed-binary identities. Later documentation
changes preserve the qualified source bytes. Final independent Astra/high review
covers the complete recovery and PR #9 client interactions before operator
disposition; **T10-R1 remains open** and PR2 acceptance/Tasks 11–12 cutover stay
gated. Local C++ remains default, PR #10 unchanged, PR #11 frozen, all PRs
Draft/unmerged. No further runtime or code correction is authorized here.
