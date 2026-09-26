# First retained terminal desktop slice

The [operator-approved PR #2 design](https://github.com/weshofmann/agent-vision/pull/2#issuecomment-5844056291)
is now a small runnable AgentVision executable. The current tested code checkpoint
is `b5551db421b64dbce85e31bd27e2cc993ea611b2`, including the operator-authorized
R1/R2 remediation below. PR #4 remains Draft; independent follow-up re-review and operator
manual mouse qualification are required before acceptance.

## Components and ownership

`AgentVisionApp` creates exactly two initially visible overlapping windows and
uses Turbo Vision's desktop, focus, frames, menu and cell geometry. Each
`TerminalWindow` uses the existing tvterm window/view adapter and its own
`TerminalController`, libvterm emulator, shell PID and PTY. Resize events update
both emulator viewport and child `TIOCSWINSZ`; keyboard events go to the focused
view. The Ctrl-B prefix/menu and Input Grab mechanism are inherited from upstream;
there is no final keymap redesign or new-window command in this slice.

The narrow Unix patch makes the existing controller's two workers joinable and
cancels blocked PTY I/O. The UI finishes disconnected controllers before rendering
and outside state locks, retains their output/status, and deletes them only on
explicit close. Live close/quit asks for confirmation. `finish()` joins workers,
closes the master and reaps/captures the direct child; repeated finish is harmless.
Normal application shutdown restores the outer terminal through Turbo Vision.
See [patch rationale and limits](../../patches/README.md).

Exact accepted pins, unchanged:

| Component | Revision | Reused role |
| --- | --- | --- |
| [tvterm](https://github.com/magiblot/tvterm/tree/210eb23564da06c358d2623388939bb02f7f3419) | `210eb23564da06c358d2623388939bb02f7f3419` | Terminal adapter, controller loops, PTY creation; four-file lifecycle patch |
| [Turbo Vision](https://github.com/magiblot/tvision/tree/640263136daa67b96a90c9bf6eb8816216ff76a3) | `640263136daa67b96a90c9bf6eb8816216ff76a3` | Desktop, windows, events, rendering; unchanged |
| [magiblot libvterm fork](https://github.com/magiblot/libvterm/tree/62b27d1db0c49eed55936a1bfa35102be91afe42) | `62b27d1db0c49eed55936a1bfa35102be91afe42` | VT state and extended callbacks; unchanged |

CMake verifies Git HEADs and the exact downstream diff. Full upstream notices
match the pins byte for byte and are copied into build output. Development use
of these pins was approved; no AgentVision license or legal redistribution
clearance is claimed. [Notices](../../THIRD_PARTY_NOTICES.md).

## Reproduction and observed evidence

Follow [build/setup commands](../../README.md). A fresh default FetchContent Debug
build with CMake 3.31.10 and Apple Clang 21 on macOS arm64 passed all twelve CTest
cases. It did not use a prepatched source override. A second configure recognized
the already-applied exact patch. The sanitized [evidence summary](terminal-desktop-evidence.json)
records source hashes, exact revisions, assertions and resource counts.

```sh
.probe/tools/cmake/data/bin/cmake -S . -B build-final -DCMAKE_BUILD_TYPE=Debug
.probe/tools/cmake/data/bin/cmake --build build-final --parallel 4
.probe/tools/cmake/data/bin/ctest --test-dir build-final --output-on-failure
PYTHONPATH="$PWD/.probe/tools" python3 tests/desktop_pty.py build-final/agentvision \
  --output .probe/desktop
```

Observed through the **actual application** in a synthetic 120×40 outer PTY:

- Two distinct direct shell PIDs/PTYS, independent variables and working
  directories, independent input/output, Ctrl-B focus changes and overlapping
  window z-order. Keyboard movement and resizing changed each window independently;
  child `stty size` matched inner rows/columns (A: 23×81; B: 24×83).
- B exit 7 retained its final marker and exact status. Arbitrary keys did not
  close it or prefix input into A. Explicit exited close kept A usable. A exit 0
  also retained its status until explicit close.
- Live close and quit both required confirmation; No preserved input/session
  usability, Yes completed cleanup. Quit with both sessions live was separately
  qualified. Known direct PIDs were absent after exit/close/quit.
- Kernel app thread/FD counts were 5/14 with two live sessions, 3/13 with B's
  exited view retained, and 1/12 with both exited views retained. Live close also
  released two workers and one master FD. These are counts for the owned app PID,
  not a process or descriptor-name dump.
- Foreground `sleep` received Ctrl-C through the real input path and returned
  control to the shell. All four normal shutdown scenarios restored exact outer
  termios, cooked line input, alternate-screen exit and application status 0.
  Startup also handled inherited SIGCHLD-ignore disposition.

Nine **core real-PTY regressions**, distinct from UI observations, additionally
assert exact exit 7 and signal 9, `waitpid` ECHILD before window destruction,
idempotent finish, six repeated two-controller cycles, FD/thread baseline,
early queued input/resize, saturated raw-PTY write cancellation, a representative
foreground HUP trap, and direct completion despite a bounded descendant holding
the slave. The natural-reaping and synchronous-shutdown assertions first failed
against unchanged upstream. That establishes defects without claiming the exact
schedule that caused the historical intermittent zombie.

Private raw/decoded synthetic captures were inspected locally and remain in
ignored build/probe folders; no raw logs/screenshots, dynamic PIDs/PTYS or machine
identifiers are published. The tracked summary contains only whitelisted results,
small resource/geometry counts, reproducible platform/tool versions and hashes.

## Gaps and review decisions

Physical mouse movement/resizing in native Terminal is **unqualified and required
by the operator before acceptance**: the
computer-use tool refused `com.apple.Terminal` for safety reasons. No alternate
UI-control bypass was attempted. Turbo Vision frame behavior is reused; source
reuse is not physical mouse evidence. Operator manual mouse qualification is pending; a short checklist will be provided
after independent R1/R2 closure.

The ownership guarantee is for known direct children, controllers, workers and
master descriptors. PTY close supplies normal foreground hangup semantics;
detached descendants are outside the guarantee. No descendant enumeration or
supervisor was added. The reader drains at most 64 KiB already available after
reaping; unlimited output and a continuously writing descendant are not qualified.

Full-screen/SIGWINCH, outer resize, abnormal-signal restoration, Unicode/load,
clipboard interoperability, suspension and wider platform support are not qualified
here. The inherited menu exposes selection/paste/suspend, but this slice's evidence
covers the specified shell/window/lifecycle paths. No daemon, provider integration,
remote access, persistence, backend/IPC or future framework was introduced.

Independent review must assess the lifecycle patch, locking/joins, status/retention,
confirmation routing, reproducibility and evidence limits. Stop for operator
implementation review; keep Draft, do not merge or start a new milestone. The
parent runtime offers no effective model/effort selector/introspection; use the
available session without claiming a switch. Independent review requests advertised
GPT-6 Astra/high for the subtle concurrency; report effective settings only if
observable. No project `.kin/config` is present.

## Operator-authorized R1/R2 remediation

[Operator review](https://github.com/weshofmann/agent-vision/pull/4#issuecomment-5844558989)
confirmed R1's opposing state/emulator lock order and R2's index-relative source
guard gap; authorized only their bounded repair/regressions.

R1 (`1a965ee`) isolates queue/wakeup synchronization from emulator serialization.
Enqueue/dequeue, writer wake and stop predicates use the same queue mutex; the
writer releases it before acquiring the emulator mutex/state. Reader wake flags
remain set until consumed, and timeout fields stay under emulator serialization.
No scrollbar updates were disabled. The four-file upstream patch remains narrow.

A deterministic fixture coordinates actual TerminalView draw/scrollbar broadcast
with the production worker publication method under emulator serialization. Test
compilation exposes controller internals for scheduling only; no product hook or
child I/O is involved. The reviewed patch fails explicitly at the three-second
watchdog, with the exact lock cycle confirmed in a private stack sample. The fix
completes and delivers the real scrollbar event to the real emulator. This is
controlled dynamic evidence, distinct from the original inconclusive marker probe.
The actual outer-PTY app also renders 80 finite scrolling rows, accepts subsequent
input/menu move/resize, reports the resized child size, and completes confirmed
close/survivor quit, owned resource cleanup and exact normal outer restoration.

R2 (`476af40`) checks the effective tracked source against HEAD, including staged
and unstaged changes, and recursively validates dependency gitlinks/cleanliness.
All validation precedes application; rejection preserves files, HEAD and staged
entries. Configured submodule-ignore settings cannot hide dependency edits. The initial ten
disposable real-Git fixtures cover clean/exact/already-staged accepted patch,
unrelated staged/unstaged edits, staged/unstaged dependency edits, wrong dependency
HEAD, nested tracked edits and ignored-dependency configuration. Seven original
fixture cases failed against the reviewed helper; all ten pass.

[Independent re-review](https://github.com/weshofmann/agent-vision/pull/4#issuecomment-5844740894)
closed R1 at `4b1a667` but retained R2: index flags still hid changed tracked bytes.
Follow-up `b5551db` conservatively refuses `assume-unchanged`/`skip-worktree`
entries in root and recursive dependencies before diff/application, without
clearing flags or writing the index. Even clean flagged files are refused; use a
fully materialized checkout without those flags. Eight new real-Git cases cover
both flags at root/dependency/nested levels and clean flagged refusal. All eight
failed against the prior guard; all eighteen cases now pass. Rejection snapshots
also compare index flags. Independent R2 closure is pending at publication.

The complete macOS suite is twelve CTest entries: nine lifecycle tests, one
deterministic lock regression, one eighteen-case source-guard fixture suite, and one
actual-desktop test with four outer-PTY scenarios. A fresh default FetchContent
Debug build passes 12/12. The exact upstream revisions/notices remain unchanged;
no source override, license choice, scope expansion or merge is included.
