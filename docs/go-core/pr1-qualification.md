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

- Tasks 1–6: pending.
- V0 clean baseline: pending in this new worktree.
- Final exact-SHA verification and independent review: pending.

Only synthetic reviewed summaries and fixtures are public. Raw environment,
private captures, host paths and runtime process identifiers are excluded.
Timing policies are tunable observations; no realtime, descendant-isolation or
unqualified-platform guarantee is implied.
