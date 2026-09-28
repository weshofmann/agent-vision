# Dynamic Local Terminals Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a user create and retire real local terminals without blocking the Turbo Vision desktop or changing the Go-core wire protocol.

**Architecture:** The C++ application owns a bounded set of pending Create request IDs and stable presentation labels. The shared `CoreConnection` and Go core continue to own session and process state. A bounded core-event service called from `getEvent()` and `idle()` matches `Created`/`RequestError`, prepares accepted terminal controllers on the UI thread, and inserts windows only outside nested modal/drag operations when Turbo Vision also permits focus movement; it closes unadopted accepted sessions while connection authority remains. The existing two-window startup remains unchanged.

**Tech Stack:** C++14, Turbo Vision/tvterm, existing AVCP v1 `CoreConnection`, Go-core session manager, CTest and the existing outer-PTY `desktop_pty.py` harness.

**Spec:** [Delegated local-preview goal on PR #11](https://github.com/weshofmann/agent-vision/pull/11#issuecomment-5875765694), G2; approved Go-core architecture and migration plan in `docs/go-core/`.

## Global constraints

- PR #11 at `e132d858769fa8c8bdbf8b2a299aa544ec2eef26` is this feature branch's published parent; preserve its two initial A/B windows and default real-core desktop.
- No wire fields, Go lifecycle-policy changes, session-limit increase, dependencies, Task 13 removal, or deferred capabilities.
- Go remains authoritative for shell/process lifecycle; C++ owns only its direct core child and presentation views.
- `CoreConnection` has a 16-session/Starting cap and bounded pending tickets; the UI must not bypass either.
- No blocking IPC or core join while a terminal/emulator/state lock is held; local endpoint cancellation must not close the shared connection.
- Keep the PR Draft, publish a small initial checkpoint before implementation, and stop at independent technical review.

## Review focus

- A delayed `Created` arriving after local shutdown must not create a window; a still-authoritative orphan must receive Close.
- A delayed `Created` during continuous key/mouse input or a popup, confirmation, or drag must be serviced without relying on Turbo Vision's idle-only event path; defer window insertion without changing that UI operation's focus/modal ownership, while the prepared controller consumes output.
- An `Error` or local admission failure must release pending UI state and report capacity/launch failure without freezing existing terminals.
- Fast repeated New actions must not exceed the 16 active/Starting budget or accumulate unbounded retired views.
- A Close request's acknowledgement must precede view retirement; close-last must leave Ctrl-B/New available.
- Natural exit retains its exact status/output until explicit dismissal; a later New must not reuse the old view identity. Pending-only Quit cancellation must preserve creation; confirmed Quit must close admission and tear down late results.

## Task 1: Pending creation and stable view identity

**Files:** Modify `src/app.h`, `src/app.cpp`, `src/commands.h`, `src/window.h`, `src/window.cpp`; test through a focused C++ state test or the existing fake-core desktop seam.

**Interfaces:** A `New Terminal` command reserves a monotonic local view ID and a bounded pending entry keyed by nonzero `RequestId`; `serviceCoreEvents()` is called from `getEvent()` and `idle()` with a finite per-call budget and a reentrancy guard. The window label is a string derived from that local ID (`A`, `B`, then `C`…`Z`, then decimal) and never from a `SessionId` or PID. Only local controller construction and window insertion occur under the existing `cleanupMutex`/`StartupAdoption` exclusion gate; `createSession`, `closeSession`, join, loss handling and message boxes remain outside it.

- [ ] Write failing tests for distinct labels, pending cap, duplicate/unknown/late Created refusal, Error cleanup, and delayed Created/Error during sustained inert input and nested modal UI.
- [ ] Verify the tests fail for the missing New path.
- [ ] Add menu command and nonblocking `createSession()` path. Keep startup's bounded `await()` only for initial A/B; never call it from New. Count visible/retained views plus pending/prepared creations under the local 16-view cap.
- [ ] Service at most a fixed small number of core events per `getEvent()`/`idle()` call, including during continuous input. Prepare a controller on the UI thread so accepted output is consumed while a modal/drag operation is active. Insert its window only when `TopView() == this`, the current desktop view is not `sfDragging`, and `canMoveFocus()` permits; `canMoveFocus()` alone is not a modal guard in pinned Turbo Vision. Retain the controller until insertion or explicit cleanup.
- [ ] Adopt accepted endpoints once under the cleanup gate. A duplicate for an already adopted session is ignored, not Closed; an unknown or late unadopted `Created` is Closed if authority remains; presentation-construction failure Closes that accepted session. On loss/shutdown, finish any prepared controller locally and clear pending state; a rejected Close relies on connection teardown, not an indefinite retry.
- [ ] Run focused tests, inspect UI event/lock ordering, then commit a coherent checkpoint.

## Task 2: Bounded view retirement and failure behavior

**Files:** Modify `src/app.cpp`, `src/window.cpp`, `src/window.h`; focused fake-core/PTY tests in `tests/`.

**Interfaces:** A broadcast counts both live and retained terminal views for the local 16-view cap. Existing correlated Close and deferred `TWindow::close()` remain the retirement path. A failed Create displays a concise capacity reason only when the local/Go limit is established; other zero `createSession()` results report admission unavailable without pretending the cause is capacity. Confirmed Quit closes New admission before shutdown, and its confirmation count includes pending/prepared Create requests; cancellation leaves them intact.

- [ ] Write failing cases for cap, repeated create/close, confirmed versus canceled close, last-view dismissal, create-after-last, and pending-only Quit cancel/confirm with a late Created reply.
- [ ] Verify the relevant failures before implementation.
- [ ] Enforce the view/pending cap and terminal Close correlation without changing the existing session manager. Confirmed Quit and local cleanup reject all later adoption.
- [ ] Run focused C++/fixture tests and the default desktop smoke; commit the behavior checkpoint.

## Task 3: Real-core interaction and G2 qualification

**Files:** Extend `tests/desktop_pty.py` or a narrowly scoped sibling test, plus concise documentation of verified commands/results.

**Interfaces:** Run an opt-in real-core desktop with four simultaneous distinct shells, independent resize/state, natural exit/retained status, close cancel/confirm, close-last/create-again, and finite repeated create/close. Observe outer restoration and exact owned-child cleanup.

- [ ] Add a real-PTY test that would fail before New exists, with short synthetic shell commands and bounded waits.
- [ ] Build the current sibling frontend/core pair; run focused tests, relevant CTest/Go checks, and the real-PTY G2 qualification. Preserve failed evidence and no uncontrolled retry.
- [ ] Review diff/status, publish exact tested/head SHAs and commands on the Draft PR, obtain independent lifecycle/concurrency review, and address in-scope findings before stopping at the G2 technical boundary.

## Design choices

The local 16-view ceiling prevents retained exited windows from growing without bound even if Go has retired a session; explicit dismissal reclaims the UI slot. A pending Create consumes a UI slot until success/error/loss. This favors a predictable finite desktop over hidden accumulation. The New path is asynchronous because the current startup `await()` deliberately blocks while establishing the initial pair; using it for a menu action would freeze event handling during spawn. A single stable local label survives close/recreate cycles without implying that a reused session ID or shell PID represents the same window.
