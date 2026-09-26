# Go core architecture — investigation checkpoint

Status: proposed architecture; operator review required before retained implementation.

The accepted V0 desktop is the baseline. This milestone designs a split in which
C++ owns Turbo Vision presentation and libvterm emulation, while a frontend-spawned
Go child owns sessions, PTYs, process lifecycle, and the authoritative state.

The preferred transport is an inherited Unix socketpair with a small versioned,
bounded binary protocol. Alternatives to evaluate are framed stdin/stdout and a
filesystem Unix socket. Raw terminal bytes stay binary; terminal emulation remains
local to the frontend. Persistent backends, reconnect, remote access, daemon
discovery, dynamic terminals, and retained migration are outside this milestone.

Validation will inspect accepted C++ ownership, current Go PTY dependencies and
macOS semantics, then use explicitly disposable synthetic probes for FD inheritance,
framing, PTY input/resize, exact child status, output/exit ordering, and cleanup
after IPC loss. Evidence must distinguish observed bytes from unread PTY data and
direct-child cleanup from descendant supervision.

The completed spec will define ownership, topology, transport, protocol, states,
lifecycle, ordering, rendering integration, concurrency, bounded backpressure,
failure behavior, packaging, tests, migration decomposition, security limits,
license findings, and unresolved risks. Independent GPT-6 Astra architecture
review follows the investigation. This checkpoint contains intent only; no probe
or product behavior is yet claimed verified.

The PR stays Draft. Publication and a review-request comment end the milestone;
operator approval of the completed written design is required for further work.
