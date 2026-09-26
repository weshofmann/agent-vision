# Go core migration acceptance

## PR2 scope and gate

Status: initial intent checkpoint; implementation and qualification pending.
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
| 7 | C++ shared-corpus AVCP codec and core-child owner | Every golden frame, bounded malformed parsing, native FD3/exec/reap/escalation | Pending |
| 8 | Shared connection and bounded per-session endpoints | Fragmentation, partial I/O, control/reply reserves, isolated cancellation, consumed-credit and staged final status | Pending |
| 9 | tvterm transport seam and IPC adapter | Owned payloads, real DSR/CPR, silent resize, lock callbacks, final flush, local selection and default local lifecycle | Pending |
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
