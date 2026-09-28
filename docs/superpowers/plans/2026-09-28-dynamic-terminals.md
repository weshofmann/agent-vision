# Dynamic Local Terminals Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a user create and retire real local terminals without blocking the Turbo Vision desktop or changing the Go-core wire protocol.

**Architecture:** The C++ application owns a bounded set of pending Create request IDs and stable presentation labels. The shared `CoreConnection` and Go core continue to own session and process state. `idle()` matches `Created`/`RequestError` to pending requests, adopts each accepted endpoint once on the UI thread, and closes an orphaned late `Created` session when connection authority still permits. The existing two-window startup remains unchanged.

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
- An `Error` or local admission failure must release pending UI state and report capacity/launch failure without freezing existing terminals.
- Fast repeated New actions must not exceed the 16 active/Starting budget or accumulate unbounded retired views.
- A Close request's acknowledgement must precede view retirement; close-last must leave Ctrl-B/New available.
- Natural exit retains its exact status/output until explicit dismissal; a later New must not reuse the old view identity.

## Task 1: Pending creation and stable view identity

**Files:** Modify `src/app.h`, `src/app.cpp`, `src/commands.h`, `src/window.h`, `src/window.cpp`; test through a focused C++ state test or the existing fake-core desktop seam.

**Interfaces:** A `New Terminal` command reserves a monotonic local view ID and a bounded pending entry keyed by nonzero `RequestId`; `idle()` consumes matching `Created`/`RequestError`. The window label is a string derived from that local ID (`A`, `B`, then `C`…`Z`, then decimal) and never from a `SessionId` or PID.

- [ ] Write failing tests for distinct labels, pending cap, duplicate/unknown/late Created refusal, and Error cleanup.
- [ ] Verify the tests fail for the missing New path.
- [ ] Add menu command and nonblocking `createSession()` path. Keep startup's bounded `await()` only for initial A/B; never call it from New.
- [ ] Adopt accepted endpoints on the UI thread through the existing tvterm `SessionTransport`; Close an unadopted accepted session if authority still exists. Clear pending on loss/shutdown.
- [ ] Run focused tests, inspect UI event/lock ordering, then commit a coherent checkpoint.

## Task 2: Bounded view retirement and failure behavior

**Files:** Modify `src/app.cpp`, `src/window.cpp`, `src/window.h`; focused fake-core/PTY tests in `tests/`.

**Interfaces:** A broadcast counts both live and retained terminal views for the local 16-view cap. Existing correlated Close and deferred `TWindow::close()` remain the retirement path. A failed Create displays a concise capacity or core-error reason, leaving other terminals usable.

- [ ] Write failing cases for cap, repeated create/close, confirmed versus canceled close, last-view dismissal, and create-after-last.
- [ ] Verify the relevant failures before implementation.
- [ ] Enforce the view/pending cap and terminal Close correlation without changing the existing session manager.
- [ ] Run focused C++/fixture tests and the default desktop smoke; commit the behavior checkpoint.

## Task 3: Real-core interaction and G2 qualification

**Files:** Extend `tests/desktop_pty.py` or a narrowly scoped sibling test, plus concise documentation of verified commands/results.

**Interfaces:** Run an opt-in real-core desktop with four simultaneous distinct shells, independent resize/state, natural exit/retained status, close cancel/confirm, close-last/create-again, and finite repeated create/close. Observe outer restoration and exact owned-child cleanup.

- [ ] Add a real-PTY test that would fail before New exists, with short synthetic shell commands and bounded waits.
- [ ] Build the current sibling frontend/core pair; run focused tests, relevant CTest/Go checks, and the real-PTY G2 qualification. Preserve failed evidence and no uncontrolled retry.
- [ ] Review diff/status, publish exact tested/head SHAs and commands on the Draft PR, obtain independent lifecycle/concurrency review, and address in-scope findings before stopping at the G2 technical boundary.

## Design choices

The local 16-view ceiling prevents retained exited windows from growing without bound even if Go has retired a session; explicit dismissal reclaims the UI slot. A pending Create consumes a UI slot until success/error/loss. This favors a predictable finite desktop over hidden accumulation. The New path is asynchronous because the current startup `await()` deliberately blocks while establishing the initial pair; using it for a menu action would freeze event handling during spawn. A single stable local label survives close/recreate cycles without implying that a reused session ID or shell PID represents the same window.
