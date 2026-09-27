# Bounded Darwin recovery implementation and pre-native review

Authority: [operator approval and assignment](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5858626978)
at design head `79c30fd8dc2bd13036b1eef96903c03254b22808`.
This implements RD-D1 through RD-D5 of the [approved design](darwin-master-open-recovery-design.md).
**Current boundary:** authorized qualification at code `40ab6e62685e5aac4f5c08c618ae00b7aea1ff40` is complete after
[independent correction-cycle clearance](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5859335377).
Final independent review of recovery and PR #9 client interactions remains pending.
Historical failures, correction boundaries and original evidence limits are preserved below.
T10-R1 remains open. All PRs remain Draft/unmerged; local C++ stays the default,
PR #10 acceptance is unchanged and PR #11 stays frozen.

## Implementation boundaries

The pristine import checkpoint is `e3e1c93d26bd53c9e0fd31c01a9163bd8053b29c`:
65 complete module-distribution files, byte-identical to the module archive with
verified `h1:bJrF4RRfyJnbTJqzRLHzcGaZK1NeM5kTC9jGgovnR1s=` and archive SHA256
`754e25253e76a5583b80d57d3add3afe68fc4d9f2a490968a9d1eda8c8fd8815`.
Its origin metadata identifies v1.1.24 commit
`edfbf75025b0ba4ee17c19f52d9b600fad80a787`. The downstream seam and source/build
guards are separately published at `646f728`.

`master_open_darwin.go` admits only the direct typed negative-six failure with
FD=-1, before ownership. Production admits three total calls, with 5/10 ms waits
and a 50 ms retry-admission window. A successful or otherwise owned descriptor
ends recovery; cancellation returns that descriptor to Spawn. An admitted Open
or child start remains non-cancellable; no detached acquisition or outer retry
exists. Prior acquisition errors remain joined when cancellation races success.
Reports are fixed-size in-process records, not IPC or UI telemetry.

The retained borrowed-master API performs the original Darwin name/grant/unlock/
slave-open operations once, with the original syscall and ioctl ABI. Spawn adopts
returned master, slave and child before interpreting errors or cancellation.
Pre-child rollback retains resources on cleanup uncertainty. Immediate post-child
slave close and every later rollback share one first Close attempt/result. Even
ErrClosed remains an error if it was that first attempt; master cleanup and sole
child reaping remain independently reachable.

The complete effective dependency set is checked against pristine file hashes,
changed-file hashes and the upstream-relative patch. Reverse then forward patch
application in a disposable copy must reproduce both source sets. New, removed,
changed and symlinked files fail closed. The CMake guard runs on every opted-in
build before the binary can build; replacement source/go.mod, both manifests,
patch and guard are rebuild dependencies. No shared module-cache mutation,
mutable build-time replacement download, toolchain/pin upgrade or license choice
is introduced.

## Deterministic evidence

Durable ignored implementation evidence lives under
`<worktree>/.superpowers/sdd/2026-09-27-darwin-recovery-implementation/`.
It includes the ledger, preserved RED/GREEN output, targeted prior-behavior
witness sources and final source/test-binary hash inventories.

- Master single-attempt behavioral RED fails recovery count/delay, exhaustion,
  overslept admission and cancellation cases; the actual production helper is
  GREEN under the anchored injected race selection.
- Spawn behavioral RED catches double slave close, lost pre-child owner and
  first-error erasure. The initial Manager fixture also failed a size argument;
  that output is retained but is not claimed as cleanup-behavior RED. A corrected,
  targeted prior direct-close/ErrClosed-erasure witness fails with the uncertain
  reservation lost, then passes against the retained implementation.
- A synchronized recovered-success/cancellation case separately demonstrated
  prior acquisition-error loss before its fix.
- The downstream injected phase selection covers name/grant/unlock/slave errors,
  borrowed-master retention and returned-slave ownership. Its first RED is a
  missing-API compile failure, distinct from behavioral RED.
- The integrity fixture rejects new/hidden/changed/removed/symlinked source.
  The isolated compile-only CMake fixture proves rebuild edges; the previous
  CMake contract fails the nested replacement go.mod edge.

From `core`, using the qualified Go 1.27.0 Darwin arm64 executable in `$GO`:

```sh
GOTOOLCHAIN=local GOENV=off GOWORK=off CGO_ENABLED=1 "$GO" test -mod=readonly -race ./internal/session -run '^TestDarwinMaster(OpenInjected|SpawnInjected)$' -count=1 -timeout=120s
```

From `core/third_party/creack-pty`:

```sh
GOTOOLCHAIN=local GOENV=off GOWORK=off CGO_ENABLED=1 "$GO" test -mod=readonly -race . -run '^TestDarwinBorrowedMasterInjected$' -count=1 -timeout=120s
```

From the worktree:

```sh
python3 cmake/verify_creack_source.py .
python3 tests/test_creack_source_guard.py
python3 tests/core_build_config.py "$CMAKE" "$GO"
```

These selections and fixture builds execute no native PTY, child, core or IPC.
The native-sixteen test has an explicit test-only failure defer to close its
endpoint and join the existing cleanup owner, retaining the existing two-second
start/cleanup and 200 ms goroutine assertions. It logs actual returned owner
reports and descriptor state on failure as well as success. This is a new
validation source delta for review, not a claim that the native test source is
unchanged. At the original pre-native checkpoint, native execution and whole-module tests
were held for independent Astra/high implementation review. The subsequent
results and current correction boundary are recorded below.

## One comparison recipe for independent review

Compile the session race test binary at the published code SHA into a fresh,
durable ignored evidence directory. Record exact source/replacement identities,
Go version, command and test-binary SHA256 before execution. Compilation itself
is allowed before the native gate; execution below requires review clearance.

From `core`, with `$EVIDENCE` an absolute ignored evidence directory:

```sh
GOTOOLCHAIN=local GOENV=off GOWORK=off CGO_ENABLED=1 "$GO" test -mod=readonly -race -c -o "$EVIDENCE/master-comparison.test" ./internal/session
AGENTVISION_DARWIN_COMPARISON=1 python3 - "$EVIDENCE" <<'PY'
import pathlib, subprocess, sys
out = pathlib.Path(sys.argv[1])
with (out / 'comparison.txt').open('wb') as log:
    try:
        run = subprocess.run([str(out / 'master-comparison.test'),
                              '-test.run=^TestDarwinMasterOpenComparison$',
                              '-test.count=1', '-test.timeout=120s', '-test.v'],
                             stdout=log, stderr=subprocess.STDOUT, timeout=130)
    except subprocess.TimeoutExpired:
        log.write(b'OUTER TIMEOUT: owned cleanup uncertain; native progression blocked\n')
        raise
sys.exit(run.returncode)
PY
```

This one opt-in test uses the actual production master helper with limit=1 for
control and limit=3 for candidate, then the same native borrowed-master path and
Resources rollback as Spawn. Each arm runs 16 serial acquisitions then one round
of 16 concurrent acquisitions. Bounds are 64 logical acquisitions, 128 raw Opens,
16 live owned pairs, zero shells/core/IPC. It records each attempt's typed errno
bits/signed value, report/error and separate first slave/master Close results,
including not-attempted versus nil. Every concurrent worker joins and every
owned close is attempted before current-round disposition. Known control
negative-six is an observation; candidate failure/exhaustion, another unexpected
error or cleanup uncertainty hard-stops progression. No extra comparison or
unchanged retry-until-green is authorized. Control-first bias and finite workload
limits remain explicit. No recovered candidate means efficacy is not demonstrated,
not permission to add attempts. Broad Go/CTest invocations disable the opt-in
comparison so the one-run experiment is never accidentally repeated.

After a satisfactory comparison, the authorized coordinator follows the approved
native-sixteen, explicit nested-dependency relevant tests, whole-module race,
fresh exact-SHA OFF/ON build/CTest and actual-core acceptance recipe in
[migration acceptance](migration-acceptance.md#reproducible-commands-and-limits).
Set `AGENTVISION_DARWIN_COMPARISON=0` for whole-module runs. Perform the three
separate normal/normal/loss heavier-resource invocations with unique evidence
and unchanged caps: 51 creations, 48 heavy A cycles, at most two live per run.
Preserve all per-cycle resource/consumption/publication/Credit observations.
Unexpected failure or incomplete cleanup blocks further native execution for
review. Evidence from previous SHAs cannot substitute for this qualification.

The final independent review and operator request must separately dispose of
T10-R1 and the integration gate. None of the injected/build checks closes either.

## Correction cycle 1: fixture ownership, causal cancellation and retention

The coordinator's one approved comparison at `f4730d5` passed with 64 logical
acquisitions, 64 raw Opens, at most three live pairs, all 128 first closes
attempted/nil, no errors and no retry. This does not demonstrate recovery efficacy.
Standalone native-sixteen passed; nested-module race passed with its existing
Darwin I/O skips. The one whole-module race invocation then **failed**, with
job-control cleanup, count-triggered cancellation and termios/size fixture failures.
Its raw output hash is
`20ed6eea6e41f1015318effe08a5d2db1caadff3f576d35ec6393f8ede7dba08`.
No later native phase or unchanged retry followed that failure.

The [failure record](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5859018097)
and [independent triage](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5859041800)
remain authoritative. Original broad/nested test executables were deleted by
ordinary Go test cleanup; their runtime binary hashes were not observed. Retained
injected executables are not relabeled as those binaries, and rebuilding cannot
repair that historical evidence gap. Skipped termios/size per-case baselines
remain unverified at that original head; the former unconditional parent log is
not proof they passed.

- **DRI-R1:** deliberate slave close now calls `CloseParentSlave` and checks the
  first result. A small test-only master-close helper shares its first actual
  result with the existing per-resource close hook. On nil it clears only the
  fixture's master reference and invalidates its captured FD; on error it retains
  both and returns the same first failure during rollback without another Close.
  No production once guard is manually completed. Startup failure cleanup is
  registered before fatal assertions, first close results are logged, and the
  summary reflects only actually reached per-case baselines. The job-control
  hangup/status assertion is retained; drain cannot read a reused saved FD.
- **DRI-R2:** the Err-call-count context is removed. A real cancellable context
  uses the existing native start seam to call `startDarwinChild` once, cancel only
  after it returns an actual child, then return that same child/cleanup/error.
  It asserts one start, exact adopted child identity, cancellation, bounded
  rollback, available owned-child status and restored FD baseline. Cleanup is
  registered before fatal assertions. The amended native fixture is compiled,
  not executed before re-review.
- **DRI-R3:** direct and CMake whole-module qualification use the small shared
  `tests/retained_go_test.py` helper. The real qualified Go executable is its
  actual child command, with `-mod=readonly -race -work -count=1`; each invocation
  creates a fresh durable ignored GOTMPDIR before Go starts. It preserves the
  printed WORK path, work-directory test executables, command, selected non-secret
  environment, source archive/digests, Go executable/version, output and binary
  hashes on success and nonzero exit. CMake's Go timeout120s/CTest timeout150s
  and package selection remain unchanged. Fake-Go direct/CTest regressions prove
  the retention contract without native qualification execution.

The source mapping checks every non-test Go file, the complete 66-file effective
nested source set, both dependency manifests, module files, upstream-relative
patch, unchanged comparison source and qualified Go executable against the
original f4730d5 identities. The original comparison/nested observations can
carry forward only with that explicit original-SHA mapping and their evidence
limits. They are not new-head runs. The comparison must not be repeated; no extra
standalone native-sixteen run is required. The next runtime gate, after a clear
re-review, is one retained-executable whole-module race invocation including the
corrected fixtures and native-sixteen in normal suite context.

From the worktree, prepare `$EVIDENCE` in a fresh durable ignored project directory
and invoke the reviewed direct qualification command once:

```sh
GOTOOLCHAIN=local GOENV=off GOWORK=off GOFLAGS= CGO_ENABLED=1 \
AGENTVISION_DARWIN_COMPARISON=0 python3 tests/retained_go_test.py \
  --go "$GO" --source-root "$PWD" --evidence-root "$EVIDENCE" \
  -- -v ./... -timeout=120s
```

The caller sets the existing qualified cache paths and supervises the existing
outer cap; no helper deadline extends an admitted native call or test watchdog.
CMake `core_go` uses the same helper with evidence under
`<worktree>/.probe/go-test-evidence/run-<unique>/`, CGO enabled and comparison
explicitly disabled. Inspect `started.json`, `result.json`, `output.txt`,
`source-identities.json`, `source.tar.gz` and `tmp/<printed-WORK>/` before declaring
qualification complete. If an outer supervisor interrupts finalization, retain
all available WORK/output and classify that evidence as incomplete; never invent
hashes or cleanup completion. Any new unexpected native failure returns to review.

## Correction cycle 2: independent native-child fixture cleanup

The cycle-1 independent review closed DRI-R1 and DRI-R3 at the source boundary,
but held DRI-R2 because the cancellation fixture depended on Spawn returning the
correct child owner before cleanup was reachable. The native start seam now
registers the actual returned cleanup before forwarding its result to Spawn.
That saved cleanup remains reachable when Resources is nil or its Process is
missing or mismatched. Returned Resources also retain their own rollback cleanup.
Each callback uses the existing sole reaper under a two-second context and reports
errors. The exact-owner, one-start, cancellation, status and FD assertions remain
unchanged; no production recovery or dependency code changes.

The same registration helpers have a no-native injected regression for all three
ownership faults. The prior returned-owner-only arrangement failed each case with
zero known-child cleanup calls; the correction passes, joins the synthetic sole
reapers and preserves both cleanup errors. Evidence and actual executed injected
binaries are retained in the ignored implementation ledger's `cycle-2/` directory.
At that checkpoint, amended native execution remained held for independent
pre-native re-review. This used the final authorized correction allowance;
unresolved findings return to the operator. The original comparison and nested observations retain their original
SHA and binary-identity limits; they are not repeated or relabeled as cycle-2 runs.

## Completed qualification at 40ab6e6; final review pending

The coordinator executed the authorized sequence once after independent clearance,
with comparison disabled and qualified Go 1.27.0, CGO/race, readonly modules and
unchanged watchdogs. All commands, selected environment, exact source inputs,
printed WORK directories, actual execution binaries and hashes remain in durable
ignored evidence at `<worktree>/.probe/darwin-recovery-coordinator/qualification-40ab6e6/`.
This later documentation checkpoint does not change the tested code SHA.

| Qualification at `40ab6e62685e5aac4f5c08c618ae00b7aea1ff40` | Observed result |
| --- | --- |
| Retained direct `go test -mod=readonly -race -work -count=1 -v ./... -timeout=120s` | PASS, 25.292 s supervised; all four startup-failure FD baselines, causal cancellation/exact owner, job-control and native-sixteen assertions reached |
| Fresh default-OFF configure/build and full CTest | PASS, 42/42, 54.385 s |
| Fresh opt-in-ON configure, clean-first build and full CTest | PASS, 48/48, 116.713 s; includes retained `core_go` whole-module race |
| Three additional fresh actual-core workloads: normal, normal, direct-core loss | PASS, 7.689 / 8.172 / 8.251 s; 51 creations and 48 heavy A cycles total |

Direct whole-module output SHA256 is
`eda0af25c68fc5921b0874f063032de3987d676f23b5240a53807d3557a3a134`.
Both direct and CTest Go runs retain their actual four test executables; the CTest
receipt/WORK are at `<worktree>/.probe/go-test-evidence/run-1790539571123001000/`.
OFF/ON execution binaries were archived before ON execution and while OFF inputs
remained untouched. C++ inputs include the exact composed tvterm source, tvision,
the vterm wrapper and a supplementary archive/hash map of all 76 actual nested
libvterm source files. The early wrapper's parent-head label does not identify
the nested libvterm revision; the supplementary record supplies that identity.
Dependency pins remain unchanged.

All 48 Credit completion records report raw/pending/ticket zero. All 51 worker
records join, covering each of 16 A cycles plus final B in each invocation.
The 195 resource records and all per-cycle stages remain retained. Exact MiB,
consumed/emulated/published equality and final sequence equality are executed
fixture assertions/stage evidence, not numerical counter dumps. An auxiliary
summary initially assumed one worker record per invocation; it was corrected to
17 without a native rerun or test change, with the analysis failure preserved.

Quiet frontend threads are eight throughout. Quiet core threads are 14 for four
cycles then 15 for twelve in normal 1; 14 for two then 15 for fourteen in normal 2;
and 15 for two then 17 for fourteen in loss. These runs show finite plateaus after
cycles 4/2/2 and different fresh-process plateaus of 15/15/17. All-stage frontend
threads range 6–10; core threads range 1–15/15/17. At quiet stages, frontend/core
FDs are 14/11 and direct children 1/1 with B alive; at most two core shell children
are live. These observations establish neither lazy initialization, a universal
thread bound nor the cause of historical growth.

Normal completion reports contact/core/value `0/0/0`, graceful 1. Intentional
direct-core loss reports `1/1/9`, graceful 0. Automated UI restoration and cooked
input oracles pass in all three invocations. This is not manual operator UI
acceptance or a guarantee about all host descendants.

The original f4730d5 whole-module failure and broad/nested missing executed-binary
identities remain historical limitations. The single comparison and nested pass
carry forward only under their original SHA and unchanged-source map; neither
was repeated, and no extra standalone native-sixteen run occurred. Recovery
policy efficacy is not inferred from the comparison's absence of failures.
Final independent Astra/high review must assess the complete recovery and PR #9
client interactions before operator disposition. T10-R1 stays open; PR2 acceptance
and Tasks 11–12 cutover remain gated. Local C++ stays default, PR #11 frozen and
all PRs Draft/unmerged. No runtime work or autonomous code correction remains
authorized by this checkpoint.
