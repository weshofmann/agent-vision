# Multi-node and attachment direction

Status: **agreed future requirements; no implementation authorization**.

Source: the [operator decision on Issue #7](https://github.com/weshofmann/agent-vision/issues/7#issuecomment-5852954574). This refines the [prior-art research](prior-art-and-future-runtime.md), whose broader patterns remain hypotheses. It supersedes the implication in the older [future-direction sketch](../future-direction.md#sequence-and-current-boundary) that multiple attached interactive clients must precede multi-machine use.

## Required future behavior

AgentVision must support a frontend aggregating sessions from multiple nodes/hosts. Each session belongs to exactly one authoritative node, which owns its processes and PTY lifecycle. Aggregation does not imply session migration, replicated authority or a consensus cluster.

A session must eventually outlive the lifetime and identity of a particular frontend: disconnecting a frontend should allow the session to continue and later reattach from another frontend or machine. This requirement does not promise survival of node shutdown/restart, durable terminal history or resumption of an agent-native session; those are separate persistence questions.

## Connections, ownership, observation and control

| Concern | Required future distinction |
| --- | --- |
| Node connection | One frontend may connect to many nodes; multiple frontends may eventually connect to one node. |
| Session ownership | Exactly one node is authoritative for each session. |
| Interactive presentation attachment | Initially at most one active interactive terminal presentation per session. |
| Observation | Other connected clients may observe metadata/state/events without owning live terminal presentation. Metadata observation does not imply raw terminal mirroring. |
| Control | The attached presentation may send input and request a session resize; node policy owns the authoritative outcome. |
| Handoff | A later explicit handoff may transfer presentation/control; its protocol and failure semantics need a separate design. |

Simultaneous multi-client interactive presentation is not an initial requirement. Read-only terminal mirroring and shared input may be considered later. Preserve room for them without implementing their arbitration now.

```text
Future session 42
  session/process authority: Node A
  PTY owner: Node A
  canonical PTY geometry: Node A / session
  active interactive presentation: at most one frontend
  other clients: metadata observation
  canonical screen/scrollback owner: UNRESOLVED
```

## Canonical geometry and local viewports

A session has one node-authoritative PTY geometry (rows/columns). Each frontend's viewport geometry is local presentation state. A frontend may request canonical resize; it must not independently resize the PTY as an implicit consequence of every local window/viewport change. The node owns the effective geometry and resize policy.

Initially exclusive attachment limits conflicting interactive resize requests, but does not decide request validation, handoff ordering or disconnected-session resize policy. If simultaneous presentation is later added, manual sizing, latest-active, largest/smallest viewport or a control lease are candidate policies, not decisions. They require explicit arbitration.

## Screen and scrollback ownership remains unresolved

**Canonical terminal screen/scrollback ownership is unresolved.** PTY ownership and canonical rows/columns do not by themselves determine where emulation, screen contents, scrollback, parser state or reconstruction data live.

A new frontend cannot reconstruct an arbitrary live terminal from only the future bytes it receives. Possible designs include node-owned emulation/state, snapshots plus incremental output, or bounded replay. Each needs evidence for reconstruction correctness and the handling of output produced while detached. Bounded replay alone is not a guarantee that sufficient history or parser state exists.

Resolve this before promising robust late attach. The geometry diagram above intentionally assigns no screen/scrollback owner. Retention, privacy, synchronization and failure behavior require a bounded design; terminal data can contain credentials or private information.

## Present implementation boundary

At accepted main `3bc0b00d691739f3b472f3e0509f2db4c0ecf850`, the default desktop uses local C++ sessions. The opt-in Go core is frontend-spawned and connection-scoped; frontend shutdown or IPC loss tears down its sessions and core. C++ retains emulation and local scrollback. Current frontend-local endpoint cancellation in an adapter is not persistent session detachment or a reconnect contract.

See the [README](../../README.md#independent-go-core-pr1-opt-in), [Go-core architecture](../superpowers/specs/2026-09-26-go-core-architecture-design.md) and [migration plan](../superpowers/plans/2026-09-26-go-core-migration-plan.md). [Draft PR #9](https://github.com/weshofmann/agent-vision/pull/9) remains a separate migration review boundary.

This decision does not authorize daemonization, persistence, reconnect, remote listeners/transports, multi-client arbitration, canonical-emulator migration, dynamic terminals, provider/agent integrations or desktop cutover. It changes no current wire semantics or lifecycle guarantees. Future designs must separately resolve identity across restarts, transport/authentication, attachment/control transitions and terminal reconstruction.
