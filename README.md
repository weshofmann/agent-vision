# AgentVision

A small text desktop with two real shell sessions in overlapping, movable,
resizable Turbo Vision windows. C++ owns presentation, terminal emulation and
one frontend-spawned Go core child; Go owns the sessions, PTYs and direct shell
children. This Tasks 11–12 cutover candidate is not a process sandbox or an agent
harness. Operator manual equivalence acceptance is still required before removing
the retained standalone local lifecycle implementation/tests.

## Build, install and run

Qualified on macOS arm64 using Apple Clang, the macOS SDK, ncurses, pthreads,
libutil, Perl, Python 3, CMake 3.31.10 and exactly Go 1.27.0 darwin/arm64.
Other targets are unqualified and rejected by the core build guard. Network
access is needed for pinned Git sources and local build/test tooling.

From the assigned feature worktree:

```sh
mkdir -p .probe/tools
python3 -m pip install --target .probe/tools cmake==3.31.10 -r tests/requirements.txt
.probe/tools/cmake/data/bin/cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DAGENTVISION_GO_EXECUTABLE="$(mise where go@1.27.0)/bin/go"
.probe/tools/cmake/data/bin/cmake --build build --parallel 4
.probe/tools/cmake/data/bin/ctest --test-dir build --output-on-failure
.probe/tools/cmake/data/bin/cmake --install build --prefix "$PWD/.probe/package"
.probe/package/bin/agentvision
```

Supply the qualified absolute Go executable explicitly. The core is ON by
default; `AGENTVISION_BUILD_CORE=OFF` is rejected because the shipping desktop
always uses IPC and has no local-shell fallback. CMake rejects missing/relative
Go paths, wrong versions and unsupported targets. Builds use readonly module
checksums, `-trimpath`, and guarded local replacement source; source, module
manifest and patch inputs trigger sibling rebuilds.

Installation places `agentvision` and `agentvision-core` together in `bin`, with
all six complete retained third-party notices and the notice summary under
`share/agentvision/licenses`. Keep the pair together when relocating the package.
The frontend resolves only its executable-adjacent core, without PATH lookup or
shell evaluation. Installed/relocated execution, including paths with spaces,
is qualified on the same Darwin arm64 host; this does not establish portability
across macOS versions or machines. There is no installed background service.

Use an interactive outer terminal of at least 40 columns by 14 rows. Sessions
inherit the frontend environment and working directory and execute `$SHELL`
(falling back to `/bin/sh` if it is not an absolute executable). Shell startup
files run normally. Ctrl-B opens the upstream-shaped menu: Tab/Shift-Tab changes
terminal, R starts keyboard movement/resizing, W closes, F maximizes/restores,
and Q quits. During movement, arrows move, Shift-arrows resize, Ctrl accelerates,
Enter accepts and Escape cancels. Mouse frames are inherited from Turbo Vision.
Input Grab is under More; Alt-End releases it. There is no new-window command in
this two-window slice.

Exited windows keep their output and authoritative exit/signal status until
explicit close. Contact loss is displayed separately; it cannot invent a shell
exit status. Typing into an exited window cannot close it or reach the other
shell. Closing a live window or quitting with live windows requires confirmation;
cancellation leaves the session usable. Close/quit waits are asynchronous so the
UI remains responsive. Shutdown or IPC loss tears down the connection-scoped
core and its sessions; there is no automatic restart, daemon, persistence,
reconnect, listener, remote access or orchestration.

Normal Go cleanup joins owned work, closes PTY masters and reaps direct shell
children. PTY closure supplies foreground hangup semantics. The frontend owns
and reaps only its direct core child. Forced core cleanup is reported separately
from graceful shutdown and cannot prove shell/descendant cleanup. Detached
descendants are outside the ownership guarantee. Presentation restores the outer
terminal before core escalation; an unresponsive UI can miss the 100 ms target
and the retained safety barrier still waits for actual restoration acknowledgement.
SIGKILL restoration is not guaranteed.

## Dependencies and verification

CMake fetches exact accepted tvterm, tvision and magiblot libvterm revisions,
checks their Git HEADs and applies the [downstream patches](patches/README.md).
System substitutes and dependency upgrades are not used. Use a fresh build
folder when changing patches. The build also copies full notices to
`build/licenses`; see [third-party notices](THIRD_PARTY_NOTICES.md).
No top-level AgentVision license or redistribution clearance has been selected.

CTest retains standalone local lifecycle regressions and adds the Go race/PTY
suite, literal-wire client, source/build guards, IPC/presentation tests and
synthetic outer-PTY desktop acceptance. The installed-package test performs an
actual CMake install, checks complete notices, runs two independent sessions
before and after relocation, and exercises missing/nonexecutable/incompatible/
malformed peers and a stopped real core with restoration/owned-core checks.
Run these harnesses separately with:

```sh
PYTHONPATH="$PWD/.probe/tools" python3 tests/desktop_pty.py build/agentvision \
  --output .probe/desktop
PYTHONPATH="$PWD/.probe/tools" python3 tests/package_siblings.py \
  .probe/tools/cmake/data/bin/cmake build --output .probe/package-acceptance
```

Raw captures are local/private under ignored build or `.probe` folders and may
contain dynamic process/PTY identities. Do not publish them. Only reviewed,
sanitized summaries belong in public evidence. The [migration acceptance
record](docs/go-core/migration-acceptance.md) distinguishes current cutover
results, historical failures and qualification limits. Automated evidence does
not substitute for the unchecked operator interaction checklist or authorize
Task 13 lifecycle removal.
