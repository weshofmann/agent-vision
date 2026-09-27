# Bounded Darwin recovery implementation and pre-native review

Authority: [operator approval and assignment](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5858626978)
at design head `79c30fd8dc2bd13036b1eef96903c03254b22808`.
This implements RD-D1 through RD-D5 of the [approved design](darwin-master-open-recovery-design.md).
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
unchanged. Native execution and whole-module tests are held for independent
Astra/high implementation review.

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
