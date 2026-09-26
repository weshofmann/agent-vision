# AgentVision

A small C++ text desktop with two real local shell sessions in overlapping,
movable, resizable Turbo Vision windows. This is the first retained V0 slice;
it is not a process sandbox or an agent harness.

## Build and run

Qualified on macOS arm64 using Apple Clang, the macOS SDK, ncurses, pthreads,
libutil, Perl, Python 3 and CMake 3.31.10. Other Unix systems are unqualified;
Windows is rejected by this slice. Network access is needed for the pinned Git
sources and optional local build/test tooling. No system package install is needed
on the qualified machine.

From this feature worktree:

```sh
mkdir -p .probe/tools
python3 -m pip install --target .probe/tools cmake==3.31.10 -r tests/requirements.txt
.probe/tools/cmake/data/bin/cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
.probe/tools/cmake/data/bin/cmake --build build --parallel 4
.probe/tools/cmake/data/bin/ctest --test-dir build --output-on-failure
./build/agentvision
```

Use an interactive outer terminal of at least 40 columns by 14 rows. The two
sessions inherit your environment and working directory and execute `$SHELL`
(falling back to `/bin/sh` if it is not an absolute executable). Shell startup
files run normally. Ctrl-B opens the upstream-shaped menu: Tab/Shift-Tab changes
terminal, R starts keyboard movement/resizing, W closes, F maximizes/restores,
and Q quits. During movement, arrows move, Shift-arrows resize, Ctrl accelerates,
Enter accepts and Escape cancels. Mouse frames are inherited from Turbo Vision.
Input Grab is under More; Alt-End releases it. This is not a final keymap design.
There is no new-window command in this two-window slice.

Exited windows keep their output and exact exit/signal status until explicit
close. Typing into an exited window cannot close it or reach the other shell.
Closing a live window or quitting with live windows requires confirmation;
cancellation leaves the session usable. Normal shutdown joins owned workers,
closes owned PTY masters and reaps the two direct children. PTY closure supplies
normal foreground hangup semantics. Detached descendants are outside that
ownership guarantee; this is not an all-descendant process supervisor.

## Dependencies and verification

CMake fetches the exact accepted tvterm, tvision and magiblot libvterm revisions,
checks their Git HEADs and applies the [narrow lifecycle patch](patches/README.md).
System substitutes and dependency upgrades are not used. Use a fresh build
folder when changing the patch. The build copies full upstream notices into
`build/licenses`; see [third-party notices](THIRD_PARTY_NOTICES.md). No top-level
AgentVision license or redistribution clearance has been selected.

Nine real-PTY lifecycle tests check direct-child status/reaping, blocked I/O,
foreground hangup and worker/FD completion. A deterministic scrollbar callback
test checks lock ordering and event delivery; a real-Git fixture test checks the
HEAD-based source guard and unchanged rejection. On macOS another CTest launches the
actual application through a synthetic 120×40 outer PTY and checks independent
shells, focus, movement/resize, ordinary finite scrolling followed by input/UI,
retention, close/quit confirmations, kernel worker
and FD counts, and normal outer-terminal restoration. Run it separately with:

```sh
PYTHONPATH="$PWD/.probe/tools" python3 tests/desktop_pty.py build/agentvision \
  --output .probe/desktop
```

Raw captures are local/private under ignored build or `.probe` folders. They may
contain dynamic PIDs/PTY names; do not publish them. Only purpose-built reviewed
summaries belong in public evidence. Read the [slice report](docs/v0/terminal-desktop.md)
for observed results and remaining qualification gaps. Full-screen/SIGWINCH,
outer-terminal resize, abnormal-signal restoration, Unicode/load and broader
platform support are outside this slice.

## Independent Go core (PR1 opt-in)

The default desktop still owns its local C++ sessions. `AGENTVISION_BUILD_CORE`
is OFF by default. Enabling it builds a sibling `agentvision-core` for independent
protocol qualification; the desktop does not launch or connect to that binary.
The core is qualified only on Darwin arm64 with exactly Go 1.27.0.

```sh
.probe/tools/cmake/data/bin/cmake -S . -B build-core \
  -DAGENTVISION_BUILD_CORE=ON \
  -DAGENTVISION_GO_EXECUTABLE="$(mise where go@1.27.0)/bin/go" \
  -DCMAKE_BUILD_TYPE=Debug
.probe/tools/cmake/data/bin/cmake --build build-core --parallel 4
.probe/tools/cmake/data/bin/ctest --test-dir build-core --output-on-failure
python3 tests/core_client.py build-core/agentvision-core
```

Supply an absolute Go executable explicitly; CMake rejects a missing, relative,
wrong-version or unsupported-target toolchain. Modules use the checked-in checksums
and readonly mode; source changes rebuild the sibling binary with `-trimpath`.
The opt-in CTests add the Go race suite, independent literal-wire client and build
configuration/rebuild checks. Go race testing also needs the qualified Apple Clang.
Full Go and creack/pty notices are copied beside the binary in `licenses/`.

The only startup mode is `agentvision-core --ipc-fd=3 --mode=frontend-spawned`,
with a connected inherited Unix stream. There is no listener, arbitrary command
option, daemon, persistence or reconnect. The frontend owns the core child; Go
owns its PTYs and direct shell children. Shutdown or contact loss cancels and
joins local work and attempts bounded owned cleanup. Cleanup uncertainty is an
error, never a successful Shutdown Ack. A slow writer or missing output credit
fails contact after the named two-second policy. Sessions are ordinary local
processes, with inherited environment/cwd and no sandbox or descendant-isolation
claim. The C++ adapter and desktop cutover remain separate review boundaries.
