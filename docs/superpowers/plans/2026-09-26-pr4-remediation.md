# PR4 R1/R2 remediation plan

> **For agentic workers:** Use superpowers:executing-plans inline, test first;
> independent read-only whole-fix review follows publication.

**Goal:** Close the operator-authorized R1/R2 findings without expanding V0.

**Architecture:** Keep the existing two workers/emulator and four-file patch.
Separate queue/wakeup synchronization from emulator serialization; no queue lock
may be held while acquiring emulator or terminal state. Validate effective tracked
source against HEAD before applying a patch, recursively checking pinned gitlinks.

**Tech Stack:** Existing C++14/CMake/Unix PTYs and Python/Git fixtures.

**Spec:** [operator authorization](https://github.com/weshofmann/agent-vision/pull/4#issuecomment-5844558989).

## Constraints and review focus

- Exact existing pins/notices, two-terminal application, direct-child ownership.
- No UI/emulator rewrite, disabled scrollbar propagation, timing-based repair,
  descendant supervisor, deferred qualification or new milestone.
- Condition mutation and waiting share the isolated queue mutex; no lost wakeups.
- Rendering state may enqueue events without acquiring the emulator mutex.
- Guard includes staged/unstaged effective tracked changes and recursive gitlinks;
  rejection preserves fixture contents/index, and no reset/cleanup is permitted.
- Draft PR, sanitized public evidence and fresh head/tested SHAs throughout.

### Task 1: R1 queue/wakeup isolation

**Files:** `patches/tvterm-lifecycle.patch`, `tests/scrollback_lock.cpp`,
`tests/desktop_pty.py`, `CMakeLists.txt`, `patches/README.md`.

- [ ] Add a deterministic regression coordinating actual state-held
  `TerminalView::draw → TScrollBar::scrollDraw` with the production worker state
  publication method; observe bounded failure against the reviewed patch.
- [ ] Isolate queue/wakeup mutex from emulator mutex; writer releases the queue
  lock before processing/publication. Reader wake/stop mutations use that mutex.
- [ ] Verify event propagation, bounded publication/completion, real application
  finite scrolling output followed by shell input/UI and owned cleanup.
- [ ] Run full existing suite plus the new regression, inspect diff, commit/push.

### Task 2: R2 effective-source guard

**Files:** `cmake/apply_patch.py`, `tests/test_source_guard.py`, `CMakeLists.txt`.

- [ ] Write disposable real-Git clean/exact/staged/unstaged/dependency-edit
  fixtures with unchanged-content/index assertions; observe current guard fail.
- [ ] Compare tracked effective source with HEAD, check recursive gitlink identity
  and clean dependency source before applying; reject without mutating fixtures.
- [ ] Run fixtures and fresh default FetchContent build/full real-PTY suite;
  inspect changes, commit/push coherent checkpoint.

### Task 3: Evidence and independent re-review

**Files:** existing slice report/evidence; PR comments/body.

- [ ] Refresh source-hashed sanitized evidence and exact-head verification.
- [ ] Publish dispositions and fresh **Review requested**, freeze target and
  obtain independent read-only review anchored to that SHA on PR4.
- [ ] After R1/R2 independently close, provide the operator's short manual mouse
  checklist. Manual qualification remains pending; keep Draft, do not merge.
