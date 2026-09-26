# Narrow Unix lifecycle patch

`tvterm-lifecycle.patch` is relative to upstream tvterm
`210eb23564da06c358d2623388939bb02f7f3419`. It changes only the PTY/controller
headers and implementations. Turbo Vision, libvterm, emulator, window/frame/view
code, key encoding and cell rendering remain unchanged. The retained application
uses these existing interfaces. Windows is outside this slice and rejected by
the application build; no Windows lifecycle claim is made.

Observed baseline regressions: natural EOF left the direct child unreaped, and
`shutDown()` returned with owned workers/FDs still present. Source tracing found
reaping only in writer disconnect, detached self-owning threads, an unsynchronized
termination read, unconditional waits/lost notifications, and blocking I/O that
could prevent shutdown. This establishes concrete gaps; it does not retroactively
prove which schedule caused the historical feasibility zombie.

The patch makes Unix workers joinable and termination atomic. An isolated queue
mutex protects enqueue/dequeue and writer wake/stop predicates; condition waits
use that same mutex and release it before emulator/state locking. Timeout fields
remain under the emulator mutex. Reader notifications retain a wake flag until
consumed, preventing lost wakeups without a state-to-emulator lock path. `finish()` is idempotent, UI-thread only and
must run **outside** terminal-state/render callbacks. It requests cancellation,
joins both workers, closes the master and captures/reaps the direct child; it
retains emulator/state for an exited window. Existing `shutDown()` now finishes
before deleting the controller. The PTY descriptor remains stable until joins.

Unix master I/O is nonblocking with 25 ms cancellable polls. The reader polls the
known direct PID with `waitpid(WNOHANG)`, including under continuous output, and
captures raw status separately from connectivity. After exit it drains at most
64 KiB of already available output, so a descendant retaining/writing the slave
cannot keep the owned reader alive indefinitely. Final emulator state is flushed
before publishing disconnection. This is not unlimited-output qualification.

Explicit close shuts the PTY, sends SIGHUP only to an unreaped direct PID, waits
up to 250 ms, then uses SIGKILL/waitpid if needed. Already-reaped PIDs are never
signalled. Status is -1 while pending and -2 on unavailable wait status; this
must never be presented as a successful exact-status result. Detached descendants
are outside the guarantee; no process-tree discovery or supervisor is added.

The parent prepares the environment and validates/falls back `$SHELL` to `/bin/sh`
before fork. The child resets terminal signal dispositions and uses only
async-signal-safe operations until `execve` or `_exit(127)`. Master FDs are
close-on-exec so later shells do not retain another terminal's master.

`cmake/apply_patch.py` applies once or recognizes the exact already-applied patch,
validates the complete effective tracked diff against HEAD (including staged
edits), recursively checks dependency gitlinks/cleanliness, rejects before applying
anything, and never resets a working tree. Configured submodule-ignore settings
cannot hide tracked dependency edits. Root and recursive dependencies with
`assume-unchanged` or `skip-worktree` tracked entries are conservatively refused,
even if those files are clean: Git may hide their effective bytes from the diff.
The guard reads NUL-delimited `git ls-files -v` tags without clearing flags or
rewriting the index; see [Git's documented tags](https://git-scm.com/docs/git-ls-files).
Eighteen real-Git fixture cases include both flags at root/dependency/nested levels
and verify rejected files, HEAD, staged entries and flags remain unchanged. Use a
fresh build directory after changing patch versions. Focused tests use real PTYs,
wait statuses, kernel thread counts and FD counts, not mocked lifecycle calls.

R1 regression: `scrollback_lock` coordinates the actual state-held
`TerminalView::draw`/scrollbar broadcast with the production publication method.
The test compilation exposes controller internals only to force scheduling; the
product has no test hooks. An inert PTY descriptor avoids unrelated child I/O.
The reviewed mutex patch fails with an explicit three-second watchdog; the fixed
queue separation completes and delivers the scrollbar event to the real emulator.
Actual desktop verification separately covers finite scrolling output, subsequent
input/menu movement/resize and confirmed close/quit with child/worker/FD cleanup.
