# Prior art and future runtime research

**Status:** research record / future-direction hypothesis.
**Not a committed roadmap or implementation authorization.**
Consolidated from [Issue #7](https://github.com/weshofmann/agent-vision/issues/7), with the [operator attachment decision](https://github.com/weshofmann/agent-vision/issues/7#issuecomment-5852954574) recorded separately in [multi-node and attachment direction](multi-node-and-attachment-direction.md). That decision refines the research's multi-client language: initially one interactive presentation per session; other clients may observe metadata.

## Present behavior and evidence boundary

At accepted main `3bc0b00d691739f3b472f3e0509f2db4c0ecf850`, the default desktop owns local C++ sessions. The opt-in Go core is frontend-spawned and connection-scoped: frontend shutdown or IPC loss tears down its sessions and core. C++ owns emulation and local scrollback; Go owns PTYs and direct shell lifecycle. See the [README](../../README.md#independent-go-core-pr1-opt-in), [architecture design](../superpowers/specs/2026-09-26-go-core-architecture-design.md) and [migration plan](../superpowers/plans/2026-09-26-go-core-migration-plan.md).

[Draft PR #9](https://github.com/weshofmann/agent-vision/pull/9) is a separate opt-in C++ adapter review boundary, not accepted desktop cutover. None of this research changes its scope, desktop defaults, current wire semantics or qualification requirements. Detach/reattach is a future requirement, not current behavior. List/Attach, durable identities, network transport and agent adapters below are hypothetical APIs/features.

## Attribution and upstream verification

The landscape, rankings, diagrams, examples and reuse choices below preserve **Issue #7's research assessments**, not a verified ecosystem capability inventory. Unless explicitly verified here, named upstream feature claims (including Herdr terminal-engine choices, Paperclip mechanisms, agent-specific ACP support, Hermes/OpenCode/OpenHands boundaries, Zellij extensions, A2A, Nomad drivers, Temporal, NATS, SQLite and go-plugin suitability) remain **unverified attributed research leads**. Names do not establish pinned versions, compatibility, licensing clearance, or AgentVision integration. Reinspect primary sources and run a bounded probe when a specific dependency becomes relevant.

Two narrow primary-source observations were checked on 2026-09-27:

- The [tmux upstream wiki](https://github.com/tmux/tmux/wiki) describes detaching running programs and reattaching from a different terminal. This supports detach/reattach as prior art; it does not settle AgentVision's screen model or resize policy.
- The [ACP introduction](https://agentclientprotocol.com/get-started/introduction) describes a protocol between coding agents and editors/IDEs, including local subprocess communication. AgentVision adapter suitability is an inference requiring design and compatibility checks; this does not establish that a particular agent supports ACP. The introduction also calls full remote support work in progress.

The technology choices and timing labels below are research preferences. The node/attachment requirements in the companion document are operator decisions. No later milestone is authorized by either document.

## Why this exists

AgentVision began as a deliberately small Turbo Vision terminal desktop: multiple real PTY-backed shells in movable, resizable, overlapping text windows.

The emerging longer-term hypothesis is broader:

> AgentVision may evolve into a neutral local/distributed runtime for human-supervised processes and agents, with replaceable frontends and optional higher-level orchestration layered above it.

The important qualifier is **neutral**. AgentVision should provide mechanisms such as sessions, processes, agents, runs, messages, capabilities, execution placement, and events without baking in a specific business, org-chart, crew, graph, or workflow metaphor.

This note captures useful architecture patterns from Herdr, Paperclip, tmux/Zellij, Hermes, OpenCode/OpenHands, ACP/A2A, Nomad/Kubernetes, Temporal, OpenTelemetry, and adjacent systems. The goal is to identify mechanisms and standards to verify and reuse where they fit.

## Hypothetical architectural picture

```text
                         AgentVision

                +--------------------------+
                |   Replaceable Frontends  |
                |                          |
                | Turbo Vision   GUI       |
                | Web            CLI       |
                +------------+-------------+
                             |
                    stable client protocol
                             |
                +------------v-------------+
                |     AgentVision Node     |
                |          Go              |
                |                          |
                | sessions / PTYs          |
                | processes                |
                | runtime state            |
                | events                   |
                | agent adapters           |
                | plugin host              |
                | local persistence        |
                +------------+-------------+
                             |
                   optional federation
                             |
        +--------------------+---------------------+
        |                    |                     |
        v                    v                     v
    AV node              AV node               AV node
   Mac Studio           Linux/GPU             cloud VM
        |                    |                     |
        +--------------------+---------------------+
                             |
                  eventual control plane
                             |
            placement / leases / runs / policy
                             |
              +--------------+---------------+
              v              v               v
            local          Nomad          Kubernetes
          execution       provider         provider
```

Higher-level systems can then sit above the substrate:

```text
planner -> implementer -> reviewer
                 |
                 v
          AgentVision runs/jobs
```

A Paperclip-like company model, a LangGraph-style workflow graph, or any other organizational policy should be able to exist as a client/layer without becoming AgentVision's core ontology.

## Prior-art landscape (attributed research leads)

| Prior art | What appears useful to AgentVision |
| --- | --- |
| **Herdr** | Closest architectural cousin. The useful pattern is a background runtime/server owning real terminals and state while detachable clients render/control them. Multi-machine aggregation can remain federated rather than immediately becoming a consensus cluster. |
| **tmux** | Mature reference for server-owned PTYs, attach/detach, session lifecycle, resize, and client independence. |
| **Zellij** | Useful plugin/extensibility reference, especially the idea that a stable extension API can support surprisingly rich UI functionality. |
| **Paperclip** | Excellent higher-level mechanisms: adapters, wakeups/heartbeats, structured run records, work checkout/leases, external runtimes reporting into a control plane. Its company/CEO/employee ontology should not become AgentVision's ontology. |
| **Hermes Agent** | Good example of one agent core exposed through several surfaces and of separating provider/tool/runtime concerns. Also relevant as a thing AgentVision should be able to host rather than absorb. |
| **OpenCode** | Strong precedent for a hard client/backend boundary and a single programmatic API/SDK rather than privileged TUI-only access. |
| **OpenHands** | Useful precedent for modular frontend/backend execution boundaries and remote/sandbox execution being separate from presentation. |
| **ACP** | Candidate standard adapter protocol for structured agent/client interaction instead of scraping CLI/TUI output whenever supported. |
| **A2A** | Candidate edge protocol for external agent interoperability, capability discovery, tasks, artifacts, and streaming. Not a replacement for the low-level AgentVision node/frontend protocol. |
| **Nomad** | Particularly attractive conceptual model for future placement: nodes advertise capabilities/resources, desired work produces evaluations, and allocations bind work to nodes. Drivers abstract execution mechanisms. |
| **Kubernetes** | Likely useful eventual execution/deployment provider, especially for containerized or isolated workloads. Avoid making Kubernetes pod/container concepts AgentVision's core domain model. |
| **Temporal** | Potential future dependency for genuinely durable long-running workflows, retries, timers, human approval, and recovery. It belongs above the session runtime, not inside PTY handling. |
| **OpenTelemetry** | Natural answer for correlation/tracing once commands span frontend -> node -> scheduler -> remote node -> adapter -> agent. |
| **NATS / JetStream** | Possible future event/fanout/replay backbone if distributed subscribers eventually justify it. Not needed for early local IPC. |
| **LangGraph / CrewAI / similar orchestration frameworks** | Useful workloads/clients for AgentVision to host. Less desirable as core ontology because they prescribe graph/crew/workflow concepts. Their persistence/checkpoint ideas can still be studied independently. |

## Ranked opportunities

Ranking preserves Issue #7's assessment of likely architectural value, fit and adoption cost/risk. All examples are hypothetical. Timing is a research label, not an approved sequence. Rank 1's detach example and rank 3's List/Attach are future behavior, absent from the present connection-scoped core.

| Rank | Idea / technology | Why it matters | Example in AgentVision | Timing |
| ---: | --- | --- | --- | --- |
| **1** | **AgentVision node is authoritative; frontend is a client** | This is the foundation for alternate frontends, detach/reconnect, remote use, and clustering. | Turbo Vision can exit while Go still owns session/process identity and lifecycle. | Future direction; current core remains connection-scoped |
| **2** | **Separate durable/shared state, live runtime objects, and presentation state** | Prevents PTYs/FDs/UI coordinates from contaminating domain objects and keeps persistence/testability sane. | Session metadata is distinct from live PTY handles and from Turbo Vision window position/scroll selection. | Design invariant now |
| **3** | **One stable public node API used by every frontend/automation surface** | Avoids privileged frontend code paths and makes modular frontends real. | Turbo Vision, CLI, GUI, and automation all issue the same Create/Input/Resize/Close/List/Attach semantics. | After current local-core cutover |
| **4** | **Resolve canonical terminal-state ownership before promising reconnect** | Raw PTY bytes alone are insufficient for arbitrary late attach without replay/snapshots. | New client can attach to a running session and receive a bounded snapshot/replay that reconstructs the current screen. | Before persistence/reattach |
| **5** | **Federate independent AgentVision nodes before building a consensus cluster** | Captures most multi-machine UX value at far lower complexity. | One Turbo Vision desktop shows sessions from Local, a Mac Studio, a Linux GPU box, and a VPS; each machine remains authoritative for its own processes. | First distributed milestone |
| **6** | **Neutral agent-adapter layer** | Lets AgentVision host Codex, Claude, Hermes, raw shells, etc. without encoding one vendor or workflow. | `RawTerminalAdapter`, `CodexAdapter`, `ClaudeAdapter`, `ACPAdapter`, `HermesAdapter`. | After generic session API is stable |
| **7** | **Keep terminal and agent concepts layered, not synonymous** | A terminal remains useful without any agent semantics; agent awareness becomes additive. | A pane/window may just be bash, or it may contain a recognized Codex process with additional status metadata. | Design invariant |
| **8** | **Semantic attention state above raw process state** | Gives the spatial UI a meaningful supervision advantage. | Window frame/status can indicate `working`, `blocked/needs-input`, `done`, `idle`, `unknown`. | Early agent-aware milestone |
| **9** | **Separate process persistence, terminal-history persistence, and agent-session persistence** | Avoids an overloaded and misleading `persistent=true`. | Process can survive frontend detach; terminal history can survive node restart; an ACP-native agent session can be resumed independently. | Persistence design |
| **10** | **Structured run records and work claims/leases** | Prevents duplicate workers and gives durable coordination evidence. | A session claims `github:repo/pr/42`; another worker gets a conflict instead of editing the same work simultaneously. | Coordination layer |
| **11** | **Nomad-like capability and placement vocabulary** | Gives clustering a neutral resource model without hard-coding Kubernetes. | Requirement: Linux + amd64 + CUDA + >=32 GiB; preference: repo cache already present. | Before scheduler |
| **12** | **Execution-provider/driver abstraction** | Separates "what should run" from "how/where it runs." | Providers: local PTY, remote AgentVision node, SSH, Docker, VM, Nomad, Kubernetes. | Distributed execution |
| **13** | **Use ACP where supported** | Avoids fragile terminal scraping and gives structured sessions/tool/status data. | An agent verified to support ACP could expose structured session updates while the raw terminal remains displayed. | Agent adapters |
| **14** | **Start plugins as out-of-process commands using the public API** | Language-neutral, crash-isolated, simple deployment, easy to replace later. | Manifest names executable + capabilities; plugin talks to AgentVision using the same supported API as other clients. | Extension v1 |
| **15** | **Append-only structured event/run journal before "agent memory"** | Objective lifecycle facts are much easier to reason about than magical long-term memory. | `SessionCreated`, `RunStarted`, `AgentBlocked`, `InputProvided`, `RunFinished`, `ClaimAcquired`. | Persistence/observability |
| **16** | **OpenTelemetry correlation IDs when distribution begins** | Makes cross-node debugging tractable. | Follow one action from Turbo Vision to local node, placement, remote node, adapter, and agent process. | Distributed milestone |
| **17** | **Temporal only if durable orchestration actually earns it** | Can save enormous effort on retries/timers/recovery, but would be wasteful for local shells. | Multi-day workflow pauses for human approval, survives restarts, then resumes on another worker. | Much later / optional |
| **18** | **A2A at the ecosystem edge** | Avoids inventing a new external agent interoperability protocol. | Expose an AgentVision-managed worker to another orchestration product through A2A task/artifact semantics. | Optional external interoperability |
| **19** | **NATS/JetStream only after event fanout/replay becomes a real pain** | Powerful but easy to overbuild early. | Multiple remote frontends, audit consumers, schedulers, and dashboards consume durable node/run events. | Much later / evidence-driven |
| **20** | **Evaluate Ghostty/libghostty-vt only if libvterm produces concrete limitations** | Herdr's terminal-engine choices are worth studying, but AgentVision already has a working tvterm/libvterm slice. | Switch only if Unicode/reflow/performance/feature evidence shows a real gap. | Technology watch, not current work |

## Especially useful architectural patterns

### 1. Mechanism, not organizational metaphor

Prefer neutral primitives:

```text
Node
Session
Process
Terminal
Agent
Run
Capability
Requirement
Allocation
WorkClaim
Message
Artifact
Event
```

Avoid making these core concepts:

```text
Company
CEO
Manager
Employee
Department
Crew
Graph
Business goal
```

A frontend or orchestration layer can map neutral AgentVision objects into those metaphors later.

### 2. Agent adapters rather than provider-specific core logic

A future adapter boundary could look roughly like:

```text
AgentVision runtime
       |
       +-- Raw terminal/process
       +-- ACP
       +-- Codex-specific fallback
       +-- Claude-specific fallback
       +-- Hermes
       +-- A2A bridge
```

The adapter should translate vendor/runtime behavior into neutral events and capabilities without making the provider the source of truth for generic session/process lifecycle.

### 3. Federated multi-machine phase before true clustering

A cheaper and likely very valuable intermediate architecture:

```text
                   Frontend
                      |
        +-------------+-------------+
        |             |             |
     AV node        AV node       AV node
      local          studio        gpu-box
        |             |             |
    sessions      sessions      sessions
```

Each node owns its local truth. The frontend aggregates them.

This gives remote/multi-machine supervision without immediately solving consensus, leader election, distributed locks, replicated stores, or HA schedulers.

### 4. Nomad-like placement later

If AgentVision eventually needs a scheduler, a useful conceptual model is:

```text
desired work
    |
    v
evaluation
    |
filter infeasible nodes
    |
rank candidates
    |
allocation
    |
AgentVision node
    |
execution provider/driver
```

Example:

```yaml
requirements:
  os: linux
  arch: amd64
  capabilities:
    - docker
    - cuda
  memory_gib: 32

preferences:
  workspace_cached: agent-vision
  locality: near-user
```

Kubernetes or Nomad can then be one execution provider rather than AgentVision's identity.

### 5. Persistence must be decomposed

Do not treat persistence as one feature. Distinguish at least:

| Persistence type | Example |
| --- | --- |
| **Frontend detach persistence** | Turbo Vision exits; shell remains alive on the node. |
| **Node-process persistence** | Runtime survives client disconnects/reconnects. |
| **Terminal presentation/history persistence** | New client can reconstruct useful screen/scrollback state. |
| **Agent-native session persistence** | Codex/Claude/Hermes/ACP session can resume even after process replacement. |
| **Run/event persistence** | Durable historical evidence of what happened and why. |

These have different storage, privacy, lifecycle, and correctness requirements.

## Relationship to the bounded Go migration

The original research referred to architecture PR #5. Its [design](../superpowers/specs/2026-09-26-go-core-architecture-design.md) and the current migration remain bounded separately from this research. They align with several high-value findings:

- Go is the authority for PTY/session/process lifecycle in the opt-in core.
- C++ retains Turbo Vision/libvterm presentation.
- IDs are opaque rather than process IDs.
- framing and backpressure are explicit.
- terminal presentation and process lifecycle are separated.
- it intentionally does not add daemon discovery, persistence, reconnect, remote access, orchestration, agent adapters, or dynamic terminals.
- it already identifies late-attach terminal-state reconstruction as a deferred design problem.

Those are strengths.

The research should instead establish future-facing invariants:

```text
1. Runtime identity must not depend on a particular frontend.
2. Session/process domain objects must not contain Turbo Vision concepts.
3. Backend APIs should avoid assuming frontend and session are on the same machine.
4. Agent semantics layer on top of generic sessions/processes.
5. Cluster placement layers on top of node-local execution.
6. Kubernetes/Nomad/SSH/etc. remain execution mechanisms rather than core ontology.
7. Persistence distinguishes process, presentation, agent-session, and event state.
8. Existing interoperability standards should be used at the appropriate edges
   rather than duplicated inside the core protocol.
```

These are constraints for later architecture reviews, not authorization to implement those later systems now.

## Possible evolution stages (hypothesis, not roadmap)

```text
V0 terminal desktop
  |
  v
Go-owned local PTY/process/session core
  |
  v
stable AgentVision Node API
  |
  +-- dynamic sessions
  +-- node-local registry
  +-- structured events
  |
  v
persistent/detachable node
  |
  +-- reconnect
  +-- explicit persistence classes
  +-- screen snapshot/replay decision
  |
  v
agent awareness
  |
  +-- neutral Agent abstraction
  +-- semantic attention state
  +-- ACP + provider adapters
  +-- plugins
  |
  v
multi-machine federation
  |
  +-- one authoritative AV node per machine
  +-- secure transport/auth
  +-- frontend aggregates nodes
  +-- node capability inventory
  |
  v
work coordination
  |
  +-- Runs
  +-- WorkClaims/leases
  +-- artifacts
  +-- wakeups
  +-- optional agent messaging
  |
  v
scheduling
  |
  +-- requirements/preferences
  +-- evaluations
  +-- allocations
  +-- execution providers
  |
  v
optional distributed control plane
  |
  +-- HA/replication only if needed
  +-- Nomad/Kubernetes integration
  +-- Temporal if durable workflow semantics justify it
  +-- NATS if event fanout/replay justifies it
  +-- A2A for external interoperability
```

The research hypothesis is that **much user-visible multi-machine value can arrive during federation**, before a replicated control plane is necessary. The order shown is illustrative; it does not require agent awareness before federation.

## Reuse posture

AgentVision should be aggressively pragmatic about reuse.

| Area | Default posture |
| --- | --- |
| Turbo Vision desktop | Continue building on modern Turbo Vision. |
| Embedded terminal UI | Continue adapting tvterm rather than cloning it. |
| Terminal emulation | Keep libvterm until evidence justifies another engine. |
| PTY/process primitives | Reuse mature OS/Go components; keep AgentVision's ownership/lifecycle semantics explicit. |
| Agent/client protocol | Prefer ACP when supported. |
| External agent interoperability | Prefer A2A if/when required. |
| Observability | Prefer OpenTelemetry rather than inventing trace propagation. |
| Local metadata | SQLite/WAL is a strong candidate once persistence is actually in scope. |
| Extension v1 | Prefer external executables/manifests using the public API. |
| Rich Go plugins later | Evaluate HashiCorp go-plugin rather than inventing process/plugin RPC from scratch. |
| Placement concepts | Borrow Nomad's node/job/evaluation/allocation/driver vocabulary where it fits. |
| Container orchestration | Integrate Kubernetes as a provider rather than exposing Pod concepts everywhere. |
| Durable workflows | Evaluate Temporal before implementing durable retries/timers/workflow recovery ourselves. |
| Event backbone | Evaluate NATS/JetStream only when simple streams stop being enough. |

Reuse still requires dependency/license review, source pinning, compatibility verification, and avoiding accidental architectural coupling to another product's policy model.

## Open research questions

| Question | Why it matters |
| --- | --- |
| Where should canonical terminal state live once late attach is required? | Determines replay/snapshot semantics and multi-client correctness. |
| How much process/session state must survive a node restart? | Separates simple detach from true persistence. |
| What is the minimum stable AgentVision Node API? | This becomes the contract for Turbo Vision, CLI, GUI, plugins, and remote clients. |
| If simultaneous presentation is added later, how should clients arbitrate size/input ownership? | Initially exclusive interactive attachment defers multi-client arbitration; the node still owns canonical PTY size. |
| Should the first remote phase be SSH-tunneled node connections, TLS listeners, or another transport? | Security/discovery requirements differ significantly. |
| Which agent runtimes already expose ACP or another structured protocol sufficiently well to avoid terminal scraping? | Determines adapter complexity. |
| How should WorkClaims interact with Git branches/worktrees/PRs? | This is directly relevant to AgentVision's own worker workflow. |
| What capability/resource schema is sufficient before introducing a scheduler? | Avoids prematurely recreating Kubernetes/Nomad. |
| When, if ever, is replicated cluster state actually necessary? | Federation may be enough for a long time. |
| What terminal/history data is safe to persist by default? | Terminal streams can contain credentials and private data. |

## Recommended next use of this research

Do not implement these ideas en masse.

Use this issue as a checklist during future architecture reviews. When a milestone reaches one of these boundaries, inspect the relevant upstream systems directly and write a bounded design/probe for that specific question.

This repository document is the durable consolidation. It remains **research / hypotheses / technology watch**, not a roadmap commitment. Read the [attachment direction](multi-node-and-attachment-direction.md) for the narrower agreed requirements and unresolved terminal-state question.

## Summary

The most useful combined mental model currently appears to be:

- **Herdr/tmux** for node-owned interactive sessions and detachable/multi-machine clients.
- **Paperclip** for adapter/wakeup/run/work-claim coordination mechanisms, minus the business ontology.
- **Nomad** for eventual capability-based placement, evaluations, allocations, and execution-driver concepts.
- **ACP/A2A** for interoperability at agent-facing boundaries.
- **OpenTelemetry** for eventual distributed causal observability.
- **Temporal/NATS/Kubernetes** as optional later infrastructure only when concrete requirements justify them.

The desired result is not "an agent framework with a terminal UI."

It is closer to:

> **A neutral runtime and spatial supervision substrate for interactive processes and agents, local first, federated next, and clusterable later, with modular frontends and policy kept above the core.**
