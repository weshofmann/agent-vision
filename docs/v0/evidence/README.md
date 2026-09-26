# Observed probe results

Behavior tested at **`64f02bede3343e70ad09edd9512ce7048efb58f4`** on
2026-09-26. Upstream code is unchanged at the three revisions in
[provenance.json](provenance.json). The main merge and original archive checkpoint did not change probe code.
Later review fixes change evidence classification and driver failure cleanup;
this original archive remains byte-identical and bound to its original SHA. This repository had no pre-existing product test suite.

## Commands actually run

```sh
PROBE_PYTHON=/Users/devel/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3 \
  sh experiments/v0/build_probe.sh
PYTHONPATH="$PWD/.probe/tools" \
  /Users/devel/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3 \
  experiments/v0/interactive_probe.py .probe/build/tvterm .probe/final-both-moved
/Users/devel/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3 \
  experiments/v0/verify_probe.py .probe/final-both-moved
```

The pinned build setup script completed successfully. The final driver and
behavior verifier both exited 0. [results.json](results.json) records **core
feasibility passed; V0 not fully tested**. No claim of production qualification.

- Shell A PID 83605, shell B PID 83623, distinct child PTYs.
- B inner size 25 rows × 83 columns, moved 15 columns/right and 5 rows/down.
- A remains 37×118 until independently resized to 20×68, then moved
  2 columns/right and 1 row/down. Front/back clipping changes with focus.
- B exit produces Disconnected; closing it leaves A able to print SURVIVOR A.
- This final run found only A in the owned-shell process sample after closing B.
- App quit status 0; alternate-screen enter/leave sequences emitted.
- Outer cooked input recovered with the exact sentinel `outer-check\n`.
- Immediately after quit only `c_lflag` differed: PENDIN (536870912) was set.
  After the input read **all** termios fields matched the original. Do not
  claim immediate byte-for-byte equality or native-terminal visual restoration.

## Intermittent cleanup failure

An earlier uncommitted-driver run observed B still a zombie after B's window
closed: `82439 82425 Z`, with A live as `82426 82425 Ss+`. The raw ordered steps
and filtered process-state observation are in
[exploratory-zombie-steps.json](exploratory-zombie-steps.json), distinct from final
SHA-bound evidence. The final run found B absent at the sample; this variation leaves lifecycle
qualification unresolved. Root cause is **not established**. The driver samples this condition; the reviewed `verify_probe.py` returns 1
for **any** remaining B process (live, stopped or zombie). Absence at a sample
does not prove which process reaped B. The original archived `results.json`
contains a legacy `closed_b_reaped` field; that claim was too broad, and the
reviewed verifier replaces it with `closed_b_absent_at_sample` plus states. One passing run cannot dismiss it.

Reproduce using the documented build/driver/verifier commands. Compare the
`owned_shell_processes_after_b_close` event with B's PID from `03-shell-b.txt`.
Do not run an unbounded stress loop to force a failure. Further tracing belongs
to the proposed first implementation slice, after design approval.

## Archive and interpretation

Selected UTF-8 screen reconstructions are under [screens](screens/); full
synthetic input steps, supervisor termios evidence and compressed raw ANSI output
are retained. Copy hashes and raw gzip round-trip were verified against originals
in the assigned worktree's ignored `.probe/`. The manifest records original
absolute paths and SHA-256 values; no durable evidence relies on `/private/tmp`.
Raw build/configure logs and toolchain inventory are retained alongside them.

The reconstruction showed doubled OSC 0 titles. The raw ANSI output contains
those titles as well; pyte rendering alone does not explain that anomaly. No
upstream fix was attempted. Physical mouse/keyboard, full-screen apps, outer
resize, signals, live-child close and descendants were not qualified.

## Review fixes and final regression

[Review-fix evidence](review-fixes/) is a separate archive tested at
**`bb917b31d22d9afc6203618a04dba11fa7c52898`**. The unmodified binary hash matches
the original archive. Reconfigured explicitly with both system-dependency options
OFF and rebuilt successfully. Five focused checks pass, covering absent/Z/S/R/T
classification and evidence-write, vanished-group and bounded-reap failures.
The real-PTY driver and revised verifier exited 0: A PID 89178, B PID 89186,
B absent at the post-close sample, core feasibility passed, V0 not fully tested.
Original and revised raw interaction evidence remain distinct; no earlier
intermittent failure is dismissed. Copy hashes and gzip round-trip were verified.

Verification accounting: `git diff --check origin/main HEAD --
":!docs/v0/evidence/**"` passes for code and report/workflow documentation.
The full-range check without that exclusion exits 2 because captured configure
logs and fixed-width screen rows contain trailing spaces. Those evidence bytes
are intentionally preserved, not whitespace-normalized. The earlier plain
`git diff --check` checked only the unstaged diff; it was not a full PR check.
