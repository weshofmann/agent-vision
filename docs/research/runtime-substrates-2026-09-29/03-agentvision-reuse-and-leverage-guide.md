# AgentVision reuse and leverage guide

**Research date:** 2026-09-29

**Status:** strategic analysis only; no migration or implementation is authorized by this document

## 1. Recommendation

Keep AgentVision’s product and object model independent of any one multiplexer, then run two disposable provider experiments:

1. **tmux control mode first** — best-supported foreign-client seam, raw pane bytes, snapshots, mature lifecycle.
2. **Zellij documented control surface second** — strongest modern workspace/automation alternative, but rendered-state rather than raw-stream semantics.

Continue using the current direct Go implementation as the reference provider while those experiments answer fidelity and boundary questions. Treat WezTerm as a source of mux/domain and emulator architecture ideas until a stable foreign-frontend contract is proven. Treat abduco as a minimal PTY-lifetime reference, not the default stack.

At the product layer, use Herdr as the terminal-supervision baseline and Paperclip as the durable-control-plane baseline. The target is not to recreate both products. The target is to identify the smallest combined model that lets the operator understand and control concurrent work better than either baseline alone.

## 2. The core separation

The architecture should have three independently replaceable layers:

```text
┌──────────────────────────────────────────────────────────────┐
│ Durable work model                                           │
│ task, dependency, assignment, run, evidence, review, decision│
└──────────────────────────────┬───────────────────────────────┘
                               │ explicit references
┌──────────────────────────────▼───────────────────────────────┐
│ AgentVision workbench                                        │
│ windows, focus, commands, attention, navigation, projections  │
└──────────────────────────────┬───────────────────────────────┘
                               │ TerminalProvider contract
┌──────────────────────────────▼───────────────────────────────┐
│ Runtime providers                                            │
│ direct PTY | tmux | possible Zellij | remote/provider adapter│
└──────────────────────────────────────────────────────────────┘
```

This prevents four forms of accidental coupling:

- A tmux session does not become a project merely because tmux is the first backend.
- A Herdr-style pane occupant does not become the durable agent identity.
- A Paperclip-style task state does not pretend to describe a live process or terminal.
- A provider’s layout engine does not dictate AgentVision’s overlapping-window model.

## 3. What can be reused directly

### 3.1 tmux

Potential direct reuse through a provider adapter:

- server-owned PTYs and child lifecycle;
- sessions, windows, panes, stable runtime IDs, and exit notifications;
- live per-pane application byte streams through control mode;
- screen/history snapshots through capture commands;
- detach/reattach and multi-client behavior;
- resize propagation;
- command, hook, format, and option machinery;
- remote placement by running the server over SSH.

Do not expose all tmux vocabulary as AgentVision’s public model. The adapter should translate only capabilities the workbench actually needs.

### 3.2 Zellij

Potential direct reuse through documented commands/subscriptions:

- background session creation and attachment;
- JSON pane/tab inventory;
- targeted pane input and lifecycle operations;
- layout creation, split/floating state, geometry, and exit status;
- rendered screen/scrollback snapshots and subscriptions;
- read-only/multi-client behavior;
- authenticated remote web/terminal attach;
- optional WASM extensions when behavior naturally belongs inside Zellij.

The gating question is whether rendered updates preserve enough information for AgentVision’s cell model and interaction requirements without relying on internal protocol code.

### 3.3 WezTerm

Potential reuse is initially conceptual or optional integration:

- domain abstraction for local, Unix, SSH, TLS, and custom execution contexts;
- split between mux objects and GUI objects;
- rich terminal-emulator behavior and screen ownership;
- reconnection and remote bootstrap ideas;
- Lua event/configuration patterns;
- cross-platform process and terminal implementation lessons.

A production dependency on internal mux crates or codec should require a separate maintenance and API-stability decision.

### 3.4 abduco

Potential reuse:

- compact reference for detached PTY ownership;
- session socket lifecycle;
- simple multi-client/read-only attachment;
- exit-status retention;
- resize-authority policy.

Its lack of canonical screen/history means it saves less engineering than its code size initially suggests.

### 3.5 Herdr

Potential reuse by learning, interoperability, or explicit integration rather than copying code:

- client/server ownership model;
- separate session/workspace/tab/pane/agent concepts;
- multi-machine aggregation without pretending to have one runtime namespace;
- worktree provenance and safe close/remove distinctions;
- attention rollups and per-client “seen” state;
- state authority/evidence/explain model;
- agent start/prompt/wait/read automation semantics;
- socket schema and event subscription patterns;
- plugin entrypoints and machine routing.

Because Herdr is Apache-2.0, code reuse is legally possible subject to notice and license compliance, but a clean interop boundary may be more valuable than forking a fast-moving competing runtime.

### 3.6 Paperclip

Potential reuse by adopting semantics or integrating through the API:

- durable Task → Run → Agent relationships;
- explicit separation of parentage, dependency, assignment, and checkout;
- atomic execution ownership;
- wake queues, admission state, reconciliation, and visible next-action invariants;
- review/approval handoffs;
- workspaces as durable run context with explicit cleanup state;
- actor-attributed audit history;
- budgets and usage records;
- connection/account grants;
- adapter boundary between control plane and runtime;
- OpenAPI/CLI as equal operator surfaces.

Paperclip is MIT licensed, but its full company model is much larger than AgentVision needs for an initial useful workbench.

## 4. Terminal provider contract

The minimum provider contract should describe capabilities rather than assume a specific backend’s hierarchy.

### Required operations

```text
Provider
  list_resources(scope) -> RuntimeResource[]
  create(spec) -> RuntimeResource
  attach(resource_id, client_spec) -> Attachment
  close(resource_id, policy)

Attachment
  initial_snapshot() -> TerminalSnapshot
  events(after_revision) -> stream<TerminalEvent>
  send_input(bytes, expected_lease?)
  resize(cols, rows, expected_lease?)
  detach()
```

### Required resource fields

- stable provider-scoped identity;
- provider kind and endpoint/node identity;
- parent/resource relationship if the provider has one;
- title/label and current dimensions;
- lifecycle: starting, running, exited, lost, unknown;
- child exit status when available;
- attachment count and control authority;
- current revision or recovery token;
- provider capability flags.

### Required event classes

- resource created/updated/exited/removed;
- terminal snapshot or snapshot-required;
- ordered screen delta or raw output chunk;
- title/cwd/mode changes when available;
- resize and control-lease changes;
- connection degraded/lost/restored;
- explicit gap/backpressure/resynchronization request.

### Capability flags

Providers should declare behavior such as:

- `raw_output_stream`
- `canonical_screen_snapshot`
- `scrollback_snapshot`
- `independent_client_views`
- `shared_geometry`
- `read_only_attachment`
- `control_lease`
- `layout_objects`
- `floating_layout`
- `process_resurrection`
- `remote_transport`
- `native_windows`

Capability declarations prevent the abstraction from lying. For example, abduco could declare raw streaming but no canonical snapshot; Zellij could declare rendered snapshots/subscriptions but not raw output; tmux could declare both raw output and capture snapshots with shared geometry.

## 5. Minimum durable object model

The durable layer should remain small enough to earn its existence. A useful minimum is:

### Workspace

The durable project/repository context.

- stable ID
- label
- repository/root reference
- optional worktree/branch metadata
- node/endpoint affinity
- lifecycle independent of UI windows and checkout deletion

### Task

The intended unit of work.

- stable ID
- title and description
- status
- parent task ID, if decomposed
- blocker task IDs, distinct from parentage
- assignee ID, if any
- priority
- workspace reference
- current run reference
- review/approval policy reference only if validated as necessary

### Agent identity

The durable configured actor, not the current process.

- stable ID and display name
- provider/adapter kind
- capability description
- configuration reference
- authority/credential scope references

### Run

One execution attempt.

- stable ID
- task and agent IDs
- start/end timestamps and outcome
- admission/ownership state
- runtime attachment reference, if live
- agent-native conversation/session reference, if available
- exact workspace/worktree/branch/revision references
- cost/usage when available
- predecessor/successor for retries or resumes

### Runtime attachment

The link to a live or historical terminal/process resource.

- provider ID
- node/endpoint ID
- provider resource identity
- current connection state
- control lease
- dimensions/capabilities
- never used as the task or agent’s durable identity

### Evidence

An immutable or append-only claim attached to a run/revision.

- kind: test, build, diff, log excerpt, artifact, review finding, screenshot, etc.
- producer and timestamp
- exact subject revision/resource
- result and provenance
- content reference or digest

### Decision

A human or policy transition.

- actor
- action
- subject task/run/review
- timestamp
- rationale/comment
- previous and new state

This model borrows Paperclip’s strongest distinctions while remaining far smaller than an autonomous-company platform.

## 6. Keep state dimensions orthogonal

One `status` field will become incoherent. AgentVision should project several state dimensions:

| Dimension | Example states | Authority |
|---|---|---|
| Task workflow | todo, active, review, blocked, done, cancelled | Durable work service/policy |
| Run lifecycle | queued, admitted, starting, running, stopped, failed, completed, lost | Execution controller/provider |
| Process state | alive, exited, unknown | PTY/mux provider |
| Agent attention | working, waiting-for-input, ready, unknown | Direct agent signal or evidence-based detector |
| Connectivity | connected, stale, reconnecting, unavailable | Client/provider connection |
| Review | not-requested, pending, changes-requested, accepted | Review service/human decision |
| Evidence | absent, stale, passing, failing, inconclusive | Verified artifact/result |
| Human acknowledgement | unseen, seen, acted-on | Per-user/client presentation state |

The UI can synthesize these into a concise attention indicator, but the underlying authorities should remain inspectable. Herdr’s `agent explain` is a useful precedent.

## 7. Architecture options

| Option | Description | Advantages | Costs/risks | Recommendation |
|---|---|---|---|---|
| **A. Direct PTY only** | Continue owning PTYs, terminal state, lifecycle, protocol, and desktop | Maximum control; no external process dependency; clean product fit | Rebuilds mature session, recovery, remote, compatibility, and multi-client behavior | Keep as reference provider, not yet proven best production substrate |
| **B. tmux-backed provider** | AgentVision is a control-mode client and owns its own desktop | Reuses mature lifecycle; supported foreign-client path; raw output plus snapshots | Screen reconciliation, tmux mode mismatch, shared geometry, external dependency | **First experiment and current leading substrate hypothesis** |
| **C. Zellij-backed provider** | Use documented automation and rendered-state subscriptions | Modern layouts, strong automation/plugins/remote web, less VT parsing if sufficient | Rendered stream may be lossy for a complete frontend; internal client protocol is not public | **Second experiment** |
| **D. WezTerm-backed provider** | Integrate via CLI/Lua or internal mux components | Richest emulator/domain model; cross-platform; remote domains | Weak public foreign-client seam; coupling to internal implementation | Research/reference; revisit if API boundary improves |
| **E. Minimal broker + VT library** | abduco-like PTY service plus `libghostty-vt` or `libtsm` | Small conceptual stack; AgentVision owns semantics; potentially cross-provider design | Must build snapshots, history, protocol, recovery, remote, multi-client, security, and operations | Only if experiments show existing muxes fundamentally incompatible |
| **F. Herdr runtime integration** | Treat Herdr as terminal/agent runtime under or beside AgentVision | Immediately gains rich agent runtime, remote machines, worktrees, automation | Product overlap; fast-moving API; AgentVision could become a presentation skin | Consider an interoperability probe, not the default architecture |
| **G. Paperclip control-plane integration** | Show/manage Paperclip tasks/runs while attaching live runtime providers | Gains mature durable workflow, governance, budgets, adapters | Huge product vocabulary and deployment dependency; terminal linkage still required | Useful optional integration/reference after workbench value is proven |
| **H. Full bespoke vertical stack** | AgentVision owns runtime, work model, UI, remote, plugins, orchestration | Coherent vision and unrestricted design | Maximum scope and risk; duplicates multiple mature projects | Reject for current phase |

## 8. Recommended adoption posture

### Adopt now as design constraints

- Provider-scoped runtime identity; never globally assume pane IDs.
- Explicit control/resize authority in multi-client scenarios.
- Snapshot-plus-stream recovery with revisions or gap detection.
- Separation of live detach from cold restart/resurrection.
- Worktree provenance separate from workspace closure and branch deletion.
- State with authority and evidence; uncertainty is first-class.
- Durable task/run identity separate from terminal/process identity.
- Worktrees and PTYs are not security sandboxes.
- Remote nodes retain their own failure and consistency boundaries.

### Validate before adopting

- tmux as production process/session substrate;
- Zellij rendered updates as a complete frontend input;
- any provider’s floating panes as AgentVision’s native desktop layout;
- automatic agent-state classification as authoritative workflow state;
- whether users need a durable task model in the first useful product;
- whether multi-machine aggregation is needed before local supervision is excellent.

### Defer

- broad agent-provider catalog;
- company/org hierarchy;
- budgets and billing;
- general MCP surface;
- cloud control plane;
- arbitrary remote execution;
- marketplace/plugin ecosystem;
- process checkpoint/restore;
- custom terminal emulator unless provider experiments prove it necessary.

## 9. Three small validation experiments

This is the authoritative **next-three** sequence. The multiplexer survey’s M1
and M2 sections supply the deeper technical checklists for E2 and E3. Its M3
provider-boundary proof is conditional follow-up work only after a substrate
probe passes.

### E1 — supervision workflow comparison

Create four synthetic concurrent work situations:

1. agent actively working;
2. agent blocked on a permission/question;
3. failing test with relevant output several screens back;
4. review with changes requested against a specific revision.

Run the same operator tasks in:

- a tmux/terminal baseline;
- Herdr;
- Paperclip where applicable;
- an AgentVision prototype or paper/clickable interaction model.

Measure:

- time to identify which work needs attention;
- time to locate the authoritative evidence;
- incorrect interventions or context mix-ups;
- navigation/input steps;
- confidence in what state is inferred versus authoritative.

**Kill criterion:** if spatial organization does not measurably improve attention, evidence lookup, or intervention correctness, do not build a large spatial desktop merely for retro character.

### E2 — tmux control-mode fidelity probe

Implement only the provider boundary and one simple view. Exercise the cases listed in the multiplexer survey, including flow-control recovery and multi-client resize.

**Kill criterion:** if correct snapshot/stream reconciliation requires deep dependence on tmux internals, or common agent TUIs cannot be represented faithfully, stop treating tmux as the leading substrate.

### E3 — Zellij rendered-state probe

Use only public CLI/subscription interfaces to drive and observe one real coding-agent TUI plus ordinary shell/full-screen workloads.

**Kill criterion:** if rendered subscriptions omit cursor, modes, or update semantics required for correct interaction—or require the internal client protocol—do not use Zellij as the hidden primary provider. It remains a feature and UX reference.

These experiments should remain standalone. They do not authorize migration, alteration of the existing PR #13 trial candidate, or replacement of the direct provider.

## 10. Product differentiation roadmap

### Stage 1 — useful terminal desktop

Prove the tactile core:

- multiple real interactive terminals;
- movable/resizable/overlapping windows;
- correct focus/input routing;
- resize propagation;
- child exit and cleanup;
- reliable outer-terminal restoration.

### Stage 2 — trustworthy attention

Add a provider-neutral attention projection:

- process alive/exited;
- agent working/waiting/ready/unknown;
- blocked evidence and explanation;
- unseen completion per operator;
- rapid jump to relevant window.

The acceptance test is reduced polling and fewer missed requests, not the number of badges.

### Stage 3 — task/run linkage

Introduce only the minimum durable objects needed to answer:

- What is this process trying to accomplish?
- Which run and worktree does it belong to?
- What evidence supports its claimed result?
- What decision is required from me?

### Stage 4 — evidence and review workspace

Make tests, diffs, logs, review findings, and decisions first-class windows linked to the exact run/revision. This is the most plausible differentiation from Herdr’s runtime-centric model and Paperclip’s web control-plane model.

### Stage 5 — heterogeneous and remote providers

Only after the local model works, aggregate tmux/direct/other providers and remote nodes. The frontend should preserve the same task/run/evidence relationships while honestly surfacing provider capabilities and stale remote state.

## 11. Kill criteria for the broader architecture

Stop or narrow the strategy if any of these are demonstrated:

- The useful experience requires only a terminal sidebar and splits; overlapping spatial layout does not improve real supervision outcomes.
- Herdr plus a small plugin can deliver the desired integrated workflow with less complexity and no meaningful UX loss.
- Paperclip plus terminal links satisfies the target operator and the text desktop becomes ornamental.
- Provider differences leak so pervasively that a common frontend either lies or collapses to the least-capable backend.
- Terminal fidelity consumes the project and prevents progress on the actual supervision workflow.
- Durable task/run state cannot be kept authoritative without recreating a large control plane before the terminal workbench is useful.
- The target users prefer existing GUI/web tools and do not value a text-mode spatial workbench enough to tolerate terminal constraints.

## 12. Final recommendation

The lowest-risk path preserves optionality:

1. Keep the current direct implementation as a reference.
2. Define the provider contract around observable capabilities and recovery semantics.
3. Run the tmux and Zellij probes without migrating product code.
4. Benchmark AgentVision against Herdr for live supervision and Paperclip for durable work comprehension.
5. Add durable objects only when a workflow experiment proves which relationships the operator needs.

This approach leverages mature upstream systems while keeping AgentVision’s actual product bet—spatial, evidence-linked supervision—under its own control.
