# Go core PR1 implementation and qualification

Status: implementation started; qualification incomplete. This PR executes Tasks
1–6 of the [approved migration plan](../superpowers/plans/2026-09-26-go-core-migration-plan.md),
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
precedes operator PR1 acceptance. Tests below will record actual results as work
is completed; this initial checkpoint makes no runtime-success claim.

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
- Tasks 3–6: pending.
- V0 clean baseline at `233485afa6da31950cf9ca05a9735a0f57b61489`: fresh
  Debug configure/build and all 12 CTests passed (23.54 seconds), including
  synthetic desktop PTY and terminal restoration checks. Qualified host:
  macOS 26.6.2 arm64, Apple Clang 21, CMake 3.31.10, Go 1.27.0.
  Commands: `cmake -S . -B build-v0 -DCMAKE_BUILD_TYPE=Debug`,
  `cmake --build build-v0 --parallel 4`, and
  `ctest --test-dir build-v0 --output-on-failure`.
- Final exact-SHA verification and independent review: pending.

Only synthetic reviewed summaries and fixtures are public. Raw environment,
private captures, host paths and runtime process identifiers are excluded.
Timing policies are tunable observations; no realtime, descendant-isolation or
unqualified-platform guarantee is implied.
