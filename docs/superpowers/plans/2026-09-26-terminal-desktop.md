# Terminal desktop implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline, task by task;
> use one independent whole-branch reviewer. Steps use checkbox syntax.

**Goal:** One retained AgentVision C++ desktop with two overlapping real terminals
and deterministic direct-child, worker and descriptor ownership.

**Architecture:** Reuse the accepted pinned tvterm-core/Turbo Vision/libvterm
bundle. Keep the existing event loops and rendering; add joinable ownership,
cancellable Unix PTY I/O and exact direct-child status through a small downstream
patch. A small application owns initial placement and explicit exit/close UI.

**Tech Stack:** C++14, CMake 3.31, Unix PTYs, existing ncurses dependencies.

**Spec:** `docs/v0/terminal-desktop.md`, `docs/v0/feasibility.md`, and the
[operator decisions D1–D5](https://github.com/weshofmann/agent-vision/pull/2#issuecomment-5844056291).
The current user request supplies the retained-slice authorization and adds normal
outer-terminal restoration to its verification requirements.

## Global constraints

- Exact tvterm/tvision/libvterm pins from the spec; bundled dependencies OFF for
  system substitution; applicable notices retained, no AgentVision license choice.
- One application, two initially visible overlapping windows, upstream Ctrl-B.
- Direct child/controller/owned FDs/workers only; no all-descendant guarantee.
- Retained exact exit status/output, arbitrary keys never close exited windows;
  explicit live close and quit confirmation.
- Sanitized public artifacts; no absolute workspace/home paths or host identifiers.
- No future harness architecture, broad terminal rewrite or additional milestone.

## Review focus

- Child exits before UI observes EOF: reap/status must not depend on window close.
- Shutdown with blocked PTY reads/writes: joins and FD closure must complete.
- Lost wakeup or continuous output: queued input/resize and final output survive.
- Foreground job holds PTY: normal hangup semantics, direct child still reaped.
- Cancel close/quit or type into exited window: keep contents/focus, no fallthrough.

### Task 1: Pinned integration and lifecycle ownership

**Files:** `CMakeLists.txt`, `cmake/apply_patch.py`,
`patches/tvterm-lifecycle.patch`, `tests/lifecycle.cpp`, `THIRD_PARTY_NOTICES.md`.
**Interfaces:** Consumes pinned upstream APIs; produces `tvterm-core` with
`TerminalController::finish() -> int` (idempotent UI-thread stop/join/close/reap),
`childPid() -> pid_t`, `childWaitStatus() -> int` (-1 pending, raw wait status),
and synchronous existing `shutDown()`. `PtyMaster` owns its master FD/direct PID.

- [ ] Write/run real-PTY regressions showing natural exit is not reaped until
  close and controller shutdown returns before worker/FD completion.
- [ ] Capture exact status, make reader/writer joinable, remove the self-owner
  cycle, make queue waits predicate-based and termination atomic.
- [ ] Use nonblocking Unix master I/O with cancellable bounded polling; close
  only after joins. Wait/reap only the known direct PID; normal SIGHUP then KILL
  if needed. Stop reading after a direct child's exit even if another process
  retains the slave. Preserve pending output through the final state update.
- [ ] Verify natural exit 7, signal exit, immediate/live close, blocked output,
  repeated two-controller cycles, FD/thread baseline and foreground job.
- [ ] Run `cmake --build build --parallel 4` and `ctest --test-dir build
  --output-on-failure`; expected all lifecycle assertions pass; publish checkpoint.

### Task 2: Two-window application and explicit exit UI

**Files:** `src/main.cpp`, `src/app.cpp`, `src/app.h`, `src/window.cpp`,
`src/window.h`, `src/commands.h`, `tests/desktop_pty.py`, `README.md`.
**Interfaces:** Consumes Task 1 controller ownership/status APIs and upstream
`BasicTerminalWindow`, constants/event updates, `TApplication` and `TMenuPopup`.
Produces `agentvision` and real outer-PTY acceptance checks.

- [ ] Write a real-PTY test for two initial shells, focus/independent cwd/state,
  overlap/keyboard resize, retained exit 7/output and arbitrary-key swallowing.
  Observe failure before the application behavior exists.
- [ ] Implement the upstream-shaped application/menu and two explicit initial
  bounds. Finalize disconnected controllers on the UI thread before rendering;
  never join while holding terminal-state locks. Show exact exit/signal status.
- [ ] Confirm explicit live close/quit, qualify cancellation and survivor input,
  child viewport size, z-order and one foreground job; inspect owned PID/FD/thread
  completion through lifecycle regressions and process metadata limited to owned PIDs.
- [ ] Run build/CTest and real-PTY script; expected all checks pass. Try physical
  mouse movement/resizing in an available actual terminal and report its precise
  evidence boundary. Publish a coherent checkpoint.

### Task 3: Sanitized evidence and independent implementation review

**Files:** `docs/v0/terminal-desktop.md`, `docs/v0/terminal-desktop-evidence.json`.
**Interfaces:** Task 2 results become purpose-built sanitized claims, not raw
terminal/environment dumps.

- [ ] Fresh final build/tests/real-PTY checks; inspect diff and generated evidence
  for public hygiene and upstream-relative patch scope. Preserve only needed facts.
- [ ] Commit/push, update Draft PR with head/tested SHAs and commands/gaps, post
  **Review requested**, freeze target and obtain independent read-only review.
- [ ] Address authorized bounded findings with regression coverage and renewed
  PR review requests as required by AGENTS.md. Stop at operator review; no merge.
