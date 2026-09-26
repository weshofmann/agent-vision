# V0 feasibility and proposed design

**Recommendation: reuse Turbo Vision + tvterm-core + its pinned libvterm fork.**
The unmodified upstream application built on this macOS arm64 machine and ran
two independent interactive shells in overlapping, keyboard-movable and
resizable text windows. This answers the core feasibility question positively.
It does **not** qualify upstream as a finished product: cleanup, child reaping,
terminal protocol coverage and license provenance remain review gates.

Only throwaway probe tooling and findings are retained in this PR. No Agent
Vision product implementation is authorized or included.

## Verified upstream/dependency baseline

Observed on 2026-09-26; full SHA references are intentional. GitHub API status and
Git checkout inspection were used, rather than earlier brainstorming claims.

| Component | Revision/status | License and requirements |
| --- | --- | --- |
| [tvterm](https://github.com/magiblot/tvterm/tree/210eb23564da06c358d2623388939bb02f7f3419) | `210eb23564da06c358d2623388939bb02f7f3419`, master HEAD, committed 2026-08-14; non-archived; no releases; explicitly experimental | [MIT](https://github.com/magiblot/tvterm/blob/210eb23564da06c358d2623388939bb02f7f3419/COPYRIGHT); CMake, C++14, C99 libvterm, Perl, ncurses, Unix libutil/pthread |
| [tvision bundled by tvterm](https://github.com/magiblot/tvision/tree/640263136daa67b96a90c9bf6eb8816216ff76a3) | `640263136daa67b96a90c9bf6eb8816216ff76a3`, 2026-08-14; this is the **tested** UI revision | [COPYRIGHT](https://github.com/magiblot/tvision/blob/640263136daa67b96a90c9bf6eb8816216ff76a3/COPYRIGHT) contains the original Borland disclaimer, MIT for new code/modifications, and embedded third-party notices; do not flatten this to blanket MIT |
| [current tvision](https://github.com/magiblot/tvision/tree/b4831e2ca16652db327fb7cc964f1c8c2d512524) | `b4831e2ca16652db327fb7cc964f1c8c2d512524`, master HEAD 2026-09-18; non-archived; three old prereleases, no stable release | Inspected status/source; **not** substituted into the tested tvterm bundle |
| [actual libvterm fork](https://github.com/magiblot/libvterm/tree/62b27d1db0c49eed55936a1bfa35102be91afe42) | `62b27d1db0c49eed55936a1bfa35102be91afe42`, mirror HEAD 2024-10-21; non-archived; git describe `v0.3.3-8-g62b27d1` | [MIT](https://github.com/magiblot/libvterm/blob/62b27d1db0c49eed55936a1bfa35102be91afe42/LICENSE); fork of neovim/libvterm mirror, not an unmodified system package |

The fork contains scrollback-continuation callback extensions (`sb_pushline4`,
`vterm_screen_callbacks_has_pushline4`) plus Alt/multibyte and Shift patches.
[Submodules](https://github.com/magiblot/tvterm/blob/210eb23564da06c358d2623388939bb02f7f3419/.gitmodules)
and [callback use](https://github.com/magiblot/tvterm/blob/210eb23564da06c358d2623388939bb02f7f3419/source/tvterm-core/vtermemu.cc)
confirm this dependency. Generic libvterm ABI/API compatibility must not be
assumed; [upstream issue #13](https://github.com/magiblot/tvterm/issues/13) reports
a build failure using system dependencies. We did not reproduce that alternate
configuration. Use the recursive pins with both system-dependency options OFF.

Host: macOS 26.6.2 (25G83), Darwin arm64; Apple clang 21.0.0; selected Xcode SDK
MacOSX27.0; Perl 5.34.1; Unix Makefiles. Initial PATH lacked CMake/Ninja/pkg-config.
Disposable CMake 3.31.10 was installed locally using the bundled Python 3.12.
Configure found SDK `libutil.tbd`, `libpthread.tbd` and `libncurses.tbd` (tvision
has an APPLE ncurses fallback). No Homebrew/MacPorts/system installation, source
patch or persistent service was needed. CMake emitted an old-minimum-version
deprecation warning; the build completed without compiler errors.

## Observations and evidence boundaries

The experimental driver sends keys to an **outer controlling PTY**, running
unmodified tvterm. It never writes directly to either child PTY. tvterm creates
real `/bin/sh` children under a clean synthetic environment, with rc loading
disabled. pyte 0.8.2 decodes outer output into text screen snapshots; these are
reconstructions, not screenshots from a native terminal application.

| Behavior | Observation |
| --- | --- |
| Two real sessions | Different shell PIDs and `/dev/ttys` paths; each owns an independent `label` variable and prompt. |
| Focus/input routing | Ctrl-B menu → Tab switches front window. Commands return `FOCUS A` or `FOCUS B` according to focus; A remains 37×118 after only B is resized. |
| Move/resize/overlap | Ctrl-B → R, arrows move; Shift-arrows resize. B moves 15 columns/right and 5 rows/down, with inner size 25×83; A independently shrinks to 20×68 and moves 2 columns/right and 1 row/down. Snapshots show clipping and front/back changes as focus switches. |
| Child resize propagation | `stty size` in the real child changes from 37×118 to 25×83 (B) and 20×68 (A), consistent with inner viewport dimensions. This verifies TIOCSWINSZ effects, not a SIGWINCH handler test. |
| Child exits | Sending `exit 7` to B produces “Disconnected”; next key closes that window. A subsequently prints `SURVIVOR A`. A's `exit 0` also produces “Disconnected”. Numeric child status is **not** exposed by the existing UI. A process-state check found B still a zombie after its window closed; this is an observed intermittent qualification failure: a later process sample found B absent (not proof of who reaped it). Do not infer reliable cleanup from one passing run. |
| Outer restoration | Quit returned 0, alternate-screen enter/leave emitted, and cooked input recovered. Immediate termios differed only by PENDIN; after reading the sentinel input every field matched the original. See [results](evidence/README.md). Visual restoration in a native terminal remains untested. |
| Title anomaly | OSC 0 title appears as `SShheellll  AA`/`BB` in emitted output/snapshots. Source suggests shared fragment accumulation for both ICONNAME and TITLE; no fix or full causal confirmation is included. |

Untested: physical keyboard/mouse dragging in the user's terminal app; outer
window resize; SIGWINCH traps; full-screen editors/alternate child screens;
Unicode/grapheme accuracy; high-output load; paste/clipboard; suspend/resume;
signals/abnormal termination; close-live-child shutdown; descendants; Windows,
Linux and other macOS/toolchain combinations. No performance or sandbox claims.

Probe-development failures must not be mistaken for upstream failures: pyte
misdispatches `CSI ? ... r` to a margins method and raised `TypeError` during
shutdown; the corrected driver ignores private restore sequences for rendering
and answers DSR queries. Directly making tvterm the outer session leader also
caused post-exit `tcgetattr` to fail with ENOTTY on macOS. An outer supervisor
is needed to measure attributes while that controlling session remains alive.
The initial driver's blocking cleanup required killing only its identified
throwaway processes; the reviewed driver closes descriptors even if evidence saving fails, tolerates a
vanished outer process group, and bounds its nonblocking supervisor reap. These
probe safeguards do not qualify upstream child/worker cleanup.

## Reuse and adaptation

[Turbo Vision](https://github.com/magiblot/tvision) supplies desktop composition,
z-order/clipping, focus/event dispatch and movable/resizable `TWindow` widgets.
Its legacy `TTerminal` is not the embedded VT emulator we need.

[tvterm-core](https://github.com/magiblot/tvterm/blob/210eb23564da06c358d2623388939bb02f7f3419/CMakeLists.txt)
already separates the reusable terminal widget from the demo application:
`BasicTerminalWindow` → `TerminalView` → `TerminalController` → `VTermEmulator`
+ `PtyMaster`. Reuse libvterm parsing/key encoding and the widget adapter directly;
use upstream app/menu code as a reference for the small application shell.

Source-level mechanism: the UI sends typed terminal events into each controller's
queue. Reader/writer threads feed libvterm and publish locked terminal state;
the reader wakes Turbo Vision with `TEventQueue::wakeUp`. Focused views forward
keys, while the app reserves Ctrl-B. View resize queues a viewport event; the
controller resizes libvterm and calls Unix `ioctl(TIOCSWINSZ)`. `forkpty` starts
`$SHELL`; these PTYs are interfaces with the same user's permissions.

Adaptation is required for deliberate initial window placement, shell validation/
fallback and arguments, bounded exit/close semantics and observable child status.
Controller internals are private and threads are detached: an application wrapper
alone cannot join them or reliably audit PID ownership. A narrow reviewed
`tvterm-core` change may be needed. Preserve the existing emulator/window system;
do not build a replacement framework to solve those lifecycle gaps.

## Proposed minimal V0 (for review, not approved implementation)

One C++ executable owns one Turbo Vision application and exactly two initial
terminal windows. Each has one controller, one emulator and one child PTY. No
backend language, IPC, daemon, session persistence or remote boundary. Initial
bounds make both windows visible and overlapping; geometry is cell-based.

Keep upstream Ctrl-B menu and keyboard move/resize behavior for the first slice.
Document the reserved prefix and input-grab escape before choosing new shortcuts.
Terminal output updates only its own surface; the UI alone owns window mutation.
Keep resize messages serialized/coalesced per controller, with child dimensions
matching the inner viewport rather than its frame. Exit should retain output,
mark the window exited, and allow closing without accidentally routing a final
key into a different live shell.

Before declaring V0 usable, audit the controller's detached-thread shutdown,
notification ordering, its `terminated` reader access, inherited signal handlers,
FD inheritance and child reaping. Upstream Unix disconnect signals the direct PID
and sleeps before kill/wait; exit status is discarded and descendants are not
explicitly supervised. A clean V0 close/quit contract must wait for worker
termination, close owned FDs and reap each direct child. Terminal restoration
belongs to Turbo Vision, qualified in the actual outer terminal. No credentials
or filesystem/process isolation are implied.

### Acceptance criteria

1. Reproducible pinned build on this host with complete license notices retained.
2. Two different live shell PIDs/PTYs; independent variables/cwd/output; typing
   reaches exactly the focused session, including after z-order changes.
3. Both windows move, resize and overlap by keyboard and mouse; obscured output
   updates correctly when exposed; inner rows/columns match child `stty size`.
4. A foreground child program observes SIGWINCH and redraws after resize; a
   representative full-screen terminal app works in both windows.
5. A shell exit leaves the other usable; exact status is visible; closing live
   shells and quitting leave no owned direct children, zombies, threads or FDs.
   Define the descendant/job shutdown policy before extending this claim.
6. Normal close/quit and tested interrupt paths restore outer termios, input,
   mouse/paste modes and screen; unsupported forced-kill paths documented.

### Alternatives, risks and recommended first slice

- **Recommended:** pin the tested dependency bundle and adapt the existing core/
  windows. Lowest duplicated code; accepts responsibility for small upstream
  lifecycle patches and an older fork ABI.
- Direct Turbo Vision + bare libvterm would require writing PTY scheduling,
  key/state-to-cell glue and lifecycle code already present in tvterm. Consider
  only if bounded core fixes prove untenable; not authorized by this spike.
- Reimplementing either desktop or terminal emulator is unwarranted by the
  successful probe and outside scope.

**First implementation slice after approval:** checked-in pinned dependency/build
integration plus an upstream-shaped two-window launcher and focused lifecycle
qualification. Resolve direct-child reaping/worker termination first, then keep
only explicit initial placement and shell selection as application behavior.
Do not introduce a general session manager. Acceptance criteria 1, 2, 3 and 5
bound this slice; full-screen/SIGWINCH and actual terminal restoration must still
pass before V0 is accepted. If cleanup cannot be qualified with a small change,
stop with its reproducer rather than replace the entire process subsystem.

Remaining decisions: accept the dependency/license baseline; approve a small
maintained core patch if needed; select reserved shortcut policy and exit-output
behavior; define descendant/job shutdown expectations; authorize the first slice.
