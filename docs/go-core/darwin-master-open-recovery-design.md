# Darwin master-open recovery design and validation plan

**Status:** proposed policy addendum; design review only. No implementation or
execution is authorized by this document. T10-R1 remains open, the retained
cutover gate remains closed, and the local C++ desktop stays the default.

**Authority:** [operator assignment](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5857880417).
Baseline PR #9 is `7b5cf63f03d02e239695d56289d8274ed17b2bfe`; retained code/last
default-OFF qualification is `c2bd9f08c990010ff9d0fd1886c026574a7a774c`.
The [architecture](../superpowers/specs/2026-09-26-go-core-architecture-design.md)
and [migration plan](../superpowers/plans/2026-09-26-go-core-migration-plan.md)
remain controlling except for the explicitly proposed allocation boundary below.
PR #10 is accepted for operator integration; PR #11's accepted preparation at
`4f8cdb7298de493ad7378ebf2739424affd1fa49` remains frozen.

## Evidence and recommendation

Master-only and [full pinned-path evidence](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5853484983)
observed typed `syscall.Errno`, bits `0xfffffffffffffffa` (signed `-6`), at the
master acquisition call before File ownership. The full-path run stopped after
48 attempts; all 47 acquired pairs had individually recorded nil slave/master
closes. [Independent audit](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5853493435)
supports that accounting, not host-wide cleanup or a kernel-cause proof.
Historical R14 stage 2 enclosed all of `pty.Open()`; its inner failure is unknown.
The Apple private-error lead is a hypothesis without exact running-kernel mapping.

Recommend at most **three master Open calls**, retrying only that same-call typed
negative-six failure before acquisition. This is a proposed mitigation of an
observed mode, not proof of its historical cause or native efficacy.

| Alternative | Benefit | Cost / limitation | Decision |
| --- | --- | --- | --- |
| Narrow bounded retry | May recover transient acquisition failure; touches no successful allocation or later stage | Can exhaust; fixed delay may be ineffective; needs an exposed boundary | Recommend subject to design approval and validation |
| Process-local serialization | Limits this core's concurrent acquisitions | Cannot control other host processes; adds queue/cancellation contention; causal benefit unproved | Do not add a gate |
| Explicit failure, unchanged | No new retry/dependency policy; preserves failure visibility | Observed acquisition mode remains user-visible and T10 unresolved | Retain as exhausted/permanent-error behavior and fallback if mitigation fails review |

## Exact dependency and ownership boundary

The pinned [public Open API](https://github.com/creack/pty/blob/edfbf75025b0ba4ee17c19f52d9b600fad80a787/doc.go)
exposes no master hook. Its [Darwin path](https://github.com/creack/pty/blob/edfbf75025b0ba4ee17c19f52d9b600fad80a787/pty_darwin.go)
opens `/dev/ptmx`, wraps the descriptor, performs name/grant/unlock operations,
and opens the slave. Later errors use best-effort master close without reporting
that close error. Wrapping `pty.Open()` cannot safely classify or account for
those later failures, so it is rejected.

Propose one narrow downstream extension to creack/pty **v1.1.24**, commit
`edfbf75025b0ba4ee17c19f52d9b600fad80a787`:

- Darwin-only `pty.OpenSlaveFromMaster(master *os.File) (*os.File, error)` borrows
  the supplied master. It performs the existing name/grant/unlock/slave-open
  sequence once, never closes/reopens the master, never retries, and returns any
  acquired slave to the caller even with an error. Operation-wrapped errors keep
  their underlying type. No raw syscall/ABI substitution or new ioctl constants.
- Existing `pty.Open()` retains its public contract and delegates its post-master
  work to that common sequence. Other platforms, Setsize and lifecycle stay intact.
- `core/internal/session/master_open_darwin.go` defines
  `acquireDarwinMaster(ctx context.Context, ops masterOpenOps) (*os.File, MasterOpenReport, error)`.
  Production `ops.open` calls the unchanged standard-library
  `syscall.Open("/dev/ptmx", O_RDWR|O_CLOEXEC, 0)`; clock/wait/open seams are per-call
  injected dependencies for tests, never mutable global hooks. No shell is started here.
- `DarwinSpawner.Spawn` adopts any nonnil master into `Resources` and installs
  rollback **before** interpreting the acquisition error or opening the slave.
  It adopts any returned slave, then uses existing termios/size/nonblocking and
  direct-child startup/reaper policy once. There is no outer Spawn/Create retry.
- On pre-child rollback, return nil resources only if cleanup succeeds. Otherwise
  preserve the resource pointer and joined error so `Manager.start` retains the
  uncertain reservation. `Resources.closeOnce` prevents a second ambiguous Close;
  ownership is not erased merely because a failure response was sent.
- Include `core/internal/session/types.go` in this narrow integration:
  `Resources.CloseParentSlave() error` records one slave File.Close attempt and
  memoizes its first result under a per-resource once guard. Replace Spawn's
  direct post-child `s.Close()` with this method; Rollback uses the same recorded
  result without calling that File.Close again or normalizing its first error
  away as ErrClosed. Master close and sole-child cleanup remain independently
  attempted. A slave-close error survives even a successful child reap and later
  Rollback calls; `Manager.start` retains the uncertain reservation and shutdown
  reports the existing cleanup error, never a successful Shutdown Ack. This
  addresses a source-supported gap, not a reproduced native Close failure.

The integration choice is an explicit local module replacement:
`github.com/creack/pty => ./third_party/creack-pty` in `core/go.mod`, with a complete
pinned source snapshot under `core/third_party/creack-pty/`. Retain the v1.1.24
require/go.sum and full MIT notice; **go.sum does not verify the local replacement**.
Track pristine per-file hashes, the minimal downstream patch
`patches/creack-pty-darwin-master-boundary.patch`, and provenance/changed-file
manifest; add source guards verifying pristine-plus-patch equals retained source.
Do not modify a shared module cache or fetch mutable upstream code during builds.
Update THIRD_PARTY_NOTICES and CMake build dependencies for replacement go.mod,
source manifest and patch; retain readonly/trimpath builds and Go 1.27.0 Darwin arm64.
This explicit dependency delta needs operator approval; no replacement is made now.

## Finite policy and result semantics

Proposed private constants: `masterOpenMaxAttempts = 3` (including initial call),
retry waits **5 ms then 10 ms**, and `masterOpenRetryWindow = 50 ms` measured with
monotonic time from the first call. Maximum deliberate waiting is 15 ms. These
values are conservative proposed limits, not empirically tuned recovery claims.

Before every call and after every cancellable wait, check the parent context.
Before a retry, require remaining attempts and time within the 50 ms window;
wait for the specified delay or cancellation, then recheck both. A late timer
cannot authorize another call. The initial call is made only with a live context.

Classification applies only to the direct master-call result: a concrete
`syscall.Errno` equal to `^syscall.Errno(5)` on the qualified 64-bit target, with
an error return and no acquired descriptor. Positive errno 6, wrapped/string
lookalikes, EINTR, other errors and invalid return combinations are not retryable.
Never apply `errors.As` to an aggregate of downstream/cleanup errors to decide
retry. Once any master succeeds, recovery is permanently over for that startup.

The 50 ms limit bounds **retry admission**, not a native call already in progress.
No goroutine is detached to manufacture cancellability. A blocking Open or later
ioctl can outlive that window; existing cleanup watchdogs remain unchanged and
must report incomplete ownership if startup has not returned. A success after
the window is still adopted if the parent context remains live; no extra retry
is admitted. Do not claim a hard 50 ms syscall or whole-startup deadline: current
reservations use cancellation, while CleanupTimeout and native test watchdogs
are distinct bounds (see `policy.Default`, `Manager.start` and native tests).

After a successful Open, wrap/adopt its descriptor exactly once before checking
cancellation. If cancellation won during Open, return that File **with** the
context error; Spawn adopts and rolls it back, with no slave setup or child spawn.
Check cancellation before/after slave setup and immediately before StartProcess.
A child start already admitted while the context was live is not itself
cancellable: cancellation racing that native call retains its returned child
for the existing sole reaper/rollback, never a second StartProcess. No shell is
launched after cancellation has been observed; late resources cannot be abandoned.

An exhausted acquisition returns `MasterOpenError` containing operation,
attempt count, elapsed time and last underlying error; `Unwrap()` preserves that
typed error. Cancellation joins the context error with the last acquisition
error, if any. No retry-exhaustion success or fabricated process status.
`MasterOpenReport` is fixed-size (attempts, elapsed, at most three typed errno
observations, recovered/exhausted/cancelled outcome), returned with the result
and stored in `Resources` when present. Native qualification records these
fields after cleanup; injected tests inspect them directly. No unbounded history,
new IPC fields, frontend logging UI, background logger or blocking diagnostic
I/O is added to acquisition. Existing generic spawn error on the wire remains.

## Proposed implementation and verification sequence — not execution authority

1. **After operator design approval**, publish the exact bounded implementation
   assignment/recipe. Add deterministic regressions to
   `core/internal/session/master_open_darwin_test.go`, calling the production
   helper through its injected operations; record meaningful RED before coding.
   Add the downstream API/phase tests and source guards, then the minimal helper
   and `process_darwin.go` integration. No disconnected retry imitation.
2. Required injected matrix: immediate success (one call/no wait); negative-six
   then success (two calls/5 ms); two negative-six then success (three/15 ms);
   three negative-six (typed exhaustion, no fourth); elapsed window expired
   after a slow failure or overslept timer (no next call); positive 6, other errno,
   wrapped/string lookalikes (one call). Cancellation before first, during delay,
   between wait/call, and racing a successful Open must prove call counts,
   exactly-once acquisition/adoption/close, retained close error and zero spawn.
   Use per-call fake clock/wait plus synchronized race barriers, not timing luck.
3. In the actual Spawn path, inject failures at name/grant/unlock/slave open,
   termios, size, nonblocking, StartProcess and post-start cancellation. Verify
   **no reacquisition**, each owned close attempted once, cleanup errors joined
   and owner reachable, exactly zero or one child starts, and sole-child reaping.
   Include a later negative-six plus failing master close: it must not trigger
   recovery. Preserve existing startup/native assertions and deadlines.
   Add a first **post-child parent-slave Close error** followed by successful
   child reap and repeated Rollback: one slave File.Close call, one master close,
   one child start/sole reaper, no reacquisition; the original close error remains
   observable, the uncertain reservation stays reachable and Shutdown produces
   cleanup failure rather than Ack. Test the actual Resources/Spawn/Manager path,
   not only a standalone close helper. No new cleanup retry is introduced.
4. Publish code/source/patch identities and obtain independent Astra/high
   implementation review before native verification. Name the injected matrix
   tests `TestDarwinMasterOpenInjected` and `TestDarwinMasterSpawnInjected`.
   From `core`, the focused proposed command is:
   `"$GO" test -mod=readonly -race ./internal/session -run '^TestDarwinMaster(OpenInjected|SpawnInjected)$' -count=1 -timeout=120s`;
   this selection must not include the later native comparison test.
   downstream tests run separately from its local module with the same toolchain.
   Document checks/source guards are also required; none of these run now.
5. Predeclare **one** reviewed native comparison recipe at the production helper/
   full PTY boundary: control max-attempts=1 and candidate policy above, each
   16 serial plus one 16-concurrent round. At most 64 logical acquisitions,
   **128 raw Opens**, 16 live probe-owned pairs, no shell/core/IPC. Control comes
   first; ordering/warmup bias is a limitation, not hidden randomization. Observe
   each operation/error/attempt/elapsed/close before assertions; join workers and
   close each owned slave/master once. Known control negative-six is an expected
   comparison observation; candidate exhaustion or any other unexpected error
   stops launches after current-round join/cleanup. Incomplete cleanup stops all
   further native work. Preserve exact binary/source/command/results under the
   existing 120 s test/130 s outer caps; no retries of the experiment or extending
   the batch when neither arm reproduces. These are future proposed bounds, not
   a renewal of the overnight allowance or permission to build a probe now.
6. After reviewed implementation and satisfactory bounded evidence, run the
   unchanged `TestNativeSixteenStartsEndpointBaselines` once in server context,
   whole-module `-race ./... -timeout=120s`, then fresh exact-published-SHA OFF/ON
   configure/build/CTest and actual-core presentation qualification using the
   [acceptance commands](migration-acceptance.md#reproducible-commands-and-limits).
   Preserve failed outcomes; never repeat unchanged failures until green.
   Keep all original T10/R15 evidence. Injected passes prove mechanics, native
   nonreproduction is finite evidence; neither establishes kernel cause.
7. Re-measure repeated heavier frontend/core sessions at that same code SHA.
   Predeclare three fresh frontend/core invocations (normal, normal, loss),
   separate from runs already included in whole qualification. Each retains the
   existing harness: one core, persistent B plus 16 successive heavy A sessions
   = **17 creations**, at most two live. Total **51 creations / 48 heavy A cycles**,
   with the unchanged bounded >=1 MiB workload and quiet post-cycle samples.
   Use `tests/ipc_terminal_pty.py` in separate `--cases normal`, `--cases normal`,
   and `--cases loss` invocations with unique ignored evidence directories;
   duplicate normal cases in one invocation conflict with its per-mode directory
   guard. Preserve its outer deadline and CMake's 190 s timeout (no extension).
   Stop on unexpected failure, preserve it, and seek review before another run.
   Record every FD/child/owned
   worker/kernel-thread sample and consumption/publication/Credit completion;
   compare plateaus and per-session trend, preserving late samples. A finite
   plateau is not a universal bound or proof of lazy runtime initialization.
   Growth correlated with sessions or another owned-resource defect stays open
   for review; no runtime redesign is inferred. Normal security approvals and
   one native workload at a time remain mandatory.

## Decision and acceptance boundary

Operator review must approve the retry constants, boundary/API and explicit local
replacement, honest cancellation/admission limits, and finite validation sequence
before implementation. If rejected or native evidence is insufficient, retain
explicit failure and propose the next decision; do not silently add serialization,
more retries, dependency upgrades or an alternative ABI. Fresh qualifying evidence
and independent closure recommendation are prerequisites for T10 technical closure;
operator acceptance and the cutover gate remain separate. PRs stay Draft/unmerged;
no PR #11 update, desktop cutover, Task 13 or deferred feature belongs to this work.
