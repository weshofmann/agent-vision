# Prior art and future runtime research

Status: **documentation consolidation in progress; research and hypotheses, not an implementation roadmap**.

This document will promote [Issue #7](https://github.com/weshofmann/agent-vision/issues/7) into a durable research reference, preserving its twenty ranked ideas, examples, reuse posture and unresolved questions. The companion attachment direction will record the operator decision separately from research hypotheses.

The current desktop uses local C++ sessions. The opt-in Go core is frontend-spawned and connection-scoped; frontend shutdown or IPC loss tears down its sessions and core. This documentation task does not add detach/reattach, remote transport, persistence, providers, agent integrations, or new wire semantics.

The completed checkpoint will receive independent documentation consistency and privacy review before operator handoff.
