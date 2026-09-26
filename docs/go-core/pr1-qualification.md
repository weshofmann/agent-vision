# Go core PR1 implementation and qualification

Status: Tasks 1–6 implemented and task reviews closed; whole-PR review and
operator acceptance pending. This PR executes Tasks 1–6 of the [approved migration plan](../superpowers/plans/2026-09-26-go-core-migration-plan.md),
following [operator plan approval](https://github.com/weshofmann/agent-vision/pull/5#issuecomment-5847740056).
Base: merged architecture/plan on main `6fdce934d0baf12d5471e2f617324ac76ac54939`.

## Scope and acceptance

Implement the versioned binary protocol/golden corpus, Darwin PTY/process owner,
Starting reservations and rollback, ordered input/resize, output credit/seal,
inherited-stream server and synthetic client, and opt-in build/notice integration.
The accepted C++ V0 desktop remains the default. No C++ adapter/cutover, dynamic
terminals, listener/daemon, reconnect, persistence or platform expansion.

Task checkpoints and exact tested SHAs are recorded on the Draft PR. Required
final evidence: race suite, two 30-second protocol fuzz runs, 500-cycle native
reaper stress, startup/cancellation/credit/seal barriers, synthetic client and
unchanged V0 CTests. Independent Astra/high protocol/lifecycle/concurrency review
precedes operator PR1 acceptance. The record distinguishes source checkpoints,
later fixes and final-head verification; the Draft PR records the current gate.

## Qualification record

- Task 1 at `d5588c4a1cf42681fc527432554a999bb14620dd`: uncached module
  tests, race tests, vet and module verification passed. Final-source 30-second
  fuzz runs passed: ReadFrame 7,831,267 executions and Decode 7,551,847
  executions, with no crash/panic/failing corpus. Independent Sol/high review
  passed both spec and quality gates, including separate literal construction
  of all 44 fixtures. Complete MIT notice was brought forward with its pin;
  Task 6 retains packaging responsibility.
- Task 2 runtime at `8985c3c023f38d46ccd1f9d33c5548bc30b0b59a`: full
  race/GC suite and vet passed on matching final source; 500 alternating
  natural-exit/direct-kill cycles passed (5.159 seconds), restoring FD and
  goroutine baselines. Native checks cover V0 termios, shell/tty/job control,
  resize, cancellation/rollback and normalized inherited SIGCHLD. The native
  exec inventory covers descriptors 3–1023; no forced PID-reuse or additional
  platform qualification is claimed. Controller post-commit module tests passed.
  Independent Astra/high review passed runtime spec and found one Minor native
  fixture lost wakeup (R1), corrected at
  `8bcd4e8244557c3601d4ff1d79d6e9bde514533e`. Original PTY and two controlled
  single-resize schedules passed ten repeats, alongside full race/vet and
  controller post-commit module checks. Independent scoped review closed R1
  with no new breakage. Initial test red was missing-API compilation;
  supplemental behavioral regressions do not retroactively establish test-first
  sequencing. Final whole-PR exact-SHA stress remains required.
- Task 3 at `8bb764f0a2b803dcea2a07025540995c9737579d`: exact-commit
  full race/GC suite, twenty startup-barrier race repeats, native failed-start
  FD/direct-child baselines and vet passed. Controller uncached module tests
  passed. Independent Astra/high spec and quality review passed with no findings.
  Starting owns late-returned resources; cancellation and Created share one
  admission boundary. Cleanup attempts are serialized and retryable; uncertainty
  returns a correlated error and preserves ownership. Command/reader/seal/server
  behavior remains assigned to Tasks 4–6.
- Task 4 implementation at `24734b5b92a6fed5d92c2f0cfd59f9d7dc01ca2e`:
  exact-code uncached full race suite passed (protocol 1.513 seconds, session
  5.126 seconds), twenty targeted race repeats passed (22.533 seconds), vet
  passed, and controller uncached module tests passed. Native checks cover
  raw-slave EAGAIN with exact 1,022-byte prefix, worker join before descriptor
  close/reuse, size/SIGWINCH with unchanged nonblocking flags, and second-session
  progress during a blocked write. Command cancellation on natural exit leaves
  lifetime context available for bounded output drain. Review head
  `08e828a9f07f0472f4b604532f5cb08c40fb6293` adds only a comment clarification;
  independent Astra/high spec and quality review passed with no findings.
  Live resize uses captured-FD TIOCSWINSZ with pinned pty.Winsize to preserve
  descriptor ownership; no current File.Fd reset defect is claimed. Linux stub
  compilation is not platform qualification. Reader/seal/ledger/server remain
  Tasks 5–6.
- Task 5 at `786fd2dc64131bbd04a8a1309d13804253fb478f`: matching-source
  full race suite passed (protocol 1.219 seconds, server 1.452 seconds, session
  4.588 seconds), vet passed, and controller post-commit module tests passed.
  Coverage includes held reads through seal, all drain reasons, fake-clock drain
  timing, short-policy credit timeout, credit-before-write-commit, native held
  slave, cleanup retry ownership and joined monitors. Independent Astra/high
  semantics review passed and requested Minor R1 for retained scheduler metadata.
  The focused correction at `a8e5b4483c421c0d700f25cb8e7d9dfadfcbf953`
  passed seven scheduler tests, scoped race (21.343 seconds), vet and controller
  post-commit server tests. Old-source regressions fail the retained capacity
  bounds; fixed queues compact while preserving FIFO and in-flight charges.
  Independent scoped re-review closed R1 with no new breakage. Owned retained
  slice metadata is bounded to 480 KiB on the qualified layout, with temporary
  copies, in-flight storage and allocator/GC overhead separate; this is not a
  total-process-memory guarantee. Actual socket/correlation/Shutdown delivery,
  CLI and build integration remain Task 6. No new full-module race claim is
  made for the metadata-only correction; final exact-SHA suites remain required.
- Task 6 implementation at `ab1b153d91c8dfc1702180f17f48d651ccc85dd2`:
  whole-module race and vet passed; 30-second ReadFrame/Decode fuzz passed with
  4,499,941/4,448,242 executions. The inherited connected Unix stream uses one
  decoder and writer; a bounded nonblocking final socket write and correlation
  retirement share the admission mutex, with readiness waits outside it.
  This bounded P-D3 adaptation follows the [independent design review](https://github.com/weshofmann/agent-vision/pull/8#issuecomment-5849205523).
  Accepted response obligations survive registry removal and precede Shutdown,
  including deferred Credit generated at output write commit. Native and controlled
  tests cover 81/48 lanes, writer stalls, short/EINTR/EAGAIN/error paths,
  first-byte deadlines, rejected response bounds, startup barriers and cleanup.
- Task 6 fixture checkpoint `7f03d349e1a16e1a4be02db400d314be1d1773de`:
  exact-head OFF 12/12 CTests passed (24.91 seconds); ON 15/15 passed
  (52.83 seconds), including core race with execwait/GC settings (26.03 seconds),
  independent synthetic client and build configuration. Three desktop repeats
  and 500 native race cycles passed (6.154 seconds). Go/codec source is identical
  to `ab1b153`; earlier fuzz/vet evidence keeps its original SHA.
  CLI validates inherited connected AF_UNIX/SOCK_STREAM, closes the original
  duplicate, rejects arbitrary options and uses sanitized diagnostics. The client
  independently verifies literal framing, raw NUL/0xff, resize, tail/exit7,
  SIGTERM, Close/Shutdown, endpoint loss and direct-child/FD evidence. Go worker
  joins are separate from kernel-thread observations. Build remains default OFF,
  pins absolute Go 1.27.0/Darwin arm64, tracks source rebuilds and copies notices.
- Independent Task 6 Astra/high review identified Important R1 in the synthetic
  client's overlapping Credit requests and ignored terminal errors. The bounded
  client/test correction at `734f4b52b3a64e0793d946cb434c0dc2be416206`
  passed nine independent literal fixtures, exact 1 MiB real-core output with
  354 matched Credit completions, and registered client CTest (0.54 seconds).
  Old-source regressions fail meaningfully. The client now coalesces one pending
  Credit per session, validates all known terminal correlations and preserves
  accepted Credit through Closed. [Independent scoped review closed R1](https://github.com/weshofmann/agent-vision/pull/8#issuecomment-5849668724)
  with no new findings. No backend/C++/build/wire change was needed.
  The heavy stream uses a separate owned core lifecycle with successful Shutdown,
  pending-response resolution, EOF, exit0 and observed direct-child absence.
  Kernel-thread baseline evidence remains scoped to the original light workload;
  replayed growth from 8 to 14 threads is not classified as a Go worker leak or
  proven runtime initialization. Go worker joins are separately tested.
- Qualification limits requiring explicit review: one initial native sixteen-start
  test exceeded its two-second resource-return watchdog on uncommitted source;
  its exact source/log snapshot was not retained. Later five focused race repeats
  and full suites passed; the cause remains unproven and is not claimed fixed.
  An unchanged V0 desktop probe also failed a contiguous raw ANSI size predicate.
  Reproduction showed correct expected/current 27×87 geometry with cursor
  positioning splitting the raw response. The bounded fixture correction uses a
  fresh rendered response token and rejects wrong-size, echo-only and old-token
  matches. C++ source/patches and behavior are unchanged. The original failed raw
  capture was overwritten; a separate reproduced trace is retained with provenance.
  Durable ignored evidence is hashed and readable; private paths/runtime identifiers
  remain outside public artifacts.
- V0 clean baseline at `233485afa6da31950cf9ca05a9735a0f57b61489`: fresh
  Debug configure/build and all 12 CTests passed (23.54 seconds), including
  synthetic desktop PTY and terminal restoration checks. Qualified host:
  macOS 26.6.2 arm64, Apple Clang 21, CMake 3.31.10, Go 1.27.0.
  Commands: `cmake -S . -B build-v0 -DCMAKE_BUILD_TYPE=Debug`,
  `cmake --build build-v0 --parallel 4`, and
  `ctest --test-dir build-v0 --output-on-failure`.
- All task gates are closed. The Draft PR records the authoritative final
  head/tested SHA and P-D4 final-head race/500-cycle results. Source checkpoints
  above retain their actual command provenance; later documentation commits are
  not implicitly runtime-tested. Fresh whole-PR Astra/high review and operator
  acceptance remain separate gates.

Only synthetic reviewed summaries and fixtures are public. Raw environment,
private captures, host paths and runtime process identifiers are excluded.
Timing policies are tunable observations; no realtime, descendant-isolation or
unqualified-platform guarantee is implied.

Reproduce the independent literal client checks with
`python3 tests/core_client_test.py`; run the native client with
`python3 tests/core_client.py build-core/agentvision-core` after the opt-in build.
Use the explicitly qualified Go executable and local module/cache policy for
these final P-D4 commands:

```sh
GODEBUG=execwait=2 GOGC=1 go -C core test -race ./... -count=1 -timeout=120s
GODEBUG=execwait=2 GOGC=1 go -C core test -race ./internal/session -run '^TestSerializedReaper$' -count=10 -timeout=120s
```
