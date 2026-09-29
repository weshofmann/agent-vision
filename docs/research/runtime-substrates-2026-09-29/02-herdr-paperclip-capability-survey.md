# Herdr and Paperclip capability survey

**Research date:** 2026-09-29

**Products:** Herdr and Paperclip

**Question:** what do they already provide, how do their models differ, and what can AgentVision reuse or learn?

## 1. They operate at different layers

Herdr and Paperclip are sometimes grouped as “agent orchestration tools,” but that obscures the useful distinction:

```text
Paperclip
  durable organization, goals, tasks, assignment, runs,
  scheduling, budgets, approvals, audit, adapters

                         possible integration boundary
                                      │
                                      ▼

Herdr
  live terminals, PTYs, panes, workspaces, agent presence,
  attention state, input/output, clients, remote machines
```

Herdr starts from the runtime experience: the coding agent is a process in a terminal pane. Paperclip starts from durable intent and accountability: the agent is an organizational actor assigned to an issue and invoked through an adapter or runner.

That difference determines what each system can say authoritatively:

- Herdr can authoritatively say which process/pane is alive, what is on its screen, which client is attached, and which inferred or reported attention state it has.
- Paperclip can authoritatively say which task exists, who owns it, which run holds its checkout, what policy governs completion, what it cost, and which approval/review is pending.

Neither authority automatically implies the other.

---

## 2. Herdr

### 2.1 Product position and release baseline

Herdr calls itself a terminal workspace manager and “the runtime your coding agents live on.” It is implemented in Rust and licensed under [Apache-2.0](https://github.com/herdrdev/herdr/blob/master/LICENSE). The latest verified stable release is [v0.9.1](https://github.com/herdrdev/herdr/releases/tag/v0.9.1), published September 16, 2026. A newer September 28 build exists on the [preview channel](https://github.com/herdrdev/herdr/releases) and is explicitly prerelease.

Herdr is not merely a themed multiplexer. Current documentation covers persistent PTYs, agent-aware attention state, multi-machine aggregation, first-class Git worktrees, an extensive socket/CLI API, automation waits, terminal observation, plugins, remote attachment, and Windows support.

### 2.2 Runtime ownership and object model

**Verified facts.** A background Herdr server owns pane processes and terminal state. One or more TUI clients attach to that server. Its documented hierarchy is:

```text
session
  └── workspace
       └── tab
            └── pane
                 └── optional recognized agent occupant
```

- A **session** is a separate server namespace with its own sockets and runtime state.
- A **workspace** is the top-level project/task context and the unit shown in the sidebar.
- A **tab** is a named layout within a workspace.
- A **pane** is a real terminal with a PTY, process, rendered state, history, and identity.
- An **agent** is a recognized live occupant of a pane, not a container that owns the pane forever.

The official [Concepts](https://herdr.dev/docs/concepts/) and [CLI reference](https://herdr.dev/docs/cli-reference/) describe right/down splits, drag resizing, pane swap/move, zoom, tabs, workspace navigation, and popups/overlays for commands and plugins.

Herdr’s primary layout is a tiled split tree. It has temporary overlays and modal popups, but it does not document a persistent arbitrary overlapping-window desktop comparable to the AgentVision concept. That is a fact about the documented layout surface, not a claim that the renderer could never be extended.

### 2.3 Terminal implementation

Herdr’s change history and repository guidance identify vendored `libghostty-vt` as the current terminal parser/state engine. The server maintains terminal state and a live bottom-buffer view used for agent detection, separate from a human client’s current scrolled viewport. Pane reads can return visible or recent rendered output, plain or ANSI-styled. Direct terminal attachment can stream a pane to an external terminal client.

**Interpretation.** Herdr is evidence that reusing a mature terminal-state component does not remove the need for a server protocol, process lifecycle, client arbitration, and product object model. It has implemented all of those around the VT layer.

### 2.4 Multi-client behavior

Multiple clients can attach to one server and independently view different workspaces and tabs. When clients view the same tab, the most recently focusing/selecting/interacting client controls that tab’s pane dimensions. That policy is similar in spirit to a “latest client wins” multiplexer policy and makes geometry ownership explicit.

Direct single-pane attachment supports one writable input/resize owner, which another client may acquire with takeover. Multiple observers can watch without controlling input or resize.

This yields two different collaboration modes:

1. Full clients have their own navigation context but share server resources.
2. Direct pane observers share a terminal stream while only one client has control authority.

### 2.5 Persistence and restoration

Herdr’s [session-state documentation](https://herdr.dev/docs/session-state/) distinguishes four cases:

| Event | Processes | Layout | Screen | Agent conversation |
|---|---|---|---|---|
| Client detach/reattach | Continue live | Preserved | Live terminal state | Same live process |
| Server restart | Stop | Restored | Only if screen history was saved | Only if an official integration recorded a resumable session reference |
| Normal update | Compatible server may continue; restart may stop processes | Restored after restart | Depends on history | Depends on native resume support |
| Experimental live handoff | Best effort to transfer supported PTYs/processes | Preserved | Live if handoff succeeds | Same process if transfer succeeds |

Saved pane-screen history is off by default because terminal output may contain secrets. Native agent restoration launches a new agent process using a saved conversation/session reference; it is not arbitrary process resurrection.

Herdr keeps current session state plus recovery copies and a bounded set of layout snapshots. Those snapshots describe topology and metadata, not the memory state of child processes.

### 2.6 Remote and multi-machine operation

Herdr documents three remote paths:

1. SSH to a host and run Herdr there, like tmux.
2. Run `herdr --remote <ssh-target>` so a local client renders a remote Herdr server over SSH.
3. Save several remote machines and aggregate them with Local in one Herdr client.

In the multi-machine model, each host keeps its own server, sessions, processes, and failure domain. The local sidebar can show agents across connected machines. Only the selected machine’s live terminal surface is streamed. A disconnected machine’s last state remains visible but dimmed until reconnection. [Connecting machines](https://herdr.dev/docs/connecting-machines/) covers compatibility checks, explicit install/restart approval, remote session selection, and reconnection behavior.

The CLI can route a supported command to one saved machine with a `--machine` selector. It does not make all servers one transactional namespace; resource IDs remain scoped to a server, and commands are not silently retried or redirected on failure.

This is a particularly relevant precedent for AgentVision’s previously recorded multi-node direction: aggregate views do not require pretending that remote runtimes share a single process or consistency boundary.

### 2.7 Git and worktrees

Herdr displays branch and ahead/behind information and can list, create, open, group, and remove Git worktrees. A worktree becomes an ordinary workspace with checkout provenance. The separation between UI state and Git state is explicit:

- closing a workspace does not delete the checkout;
- deleting the checkout uses `git worktree remove`;
- dirty removal requires an explicit force path;
- removing a worktree does not delete the branch;
- the original checkout and linked worktrees are grouped for navigation.

See the [CLI worktree reference](https://herdr.dev/docs/cli-reference/#worktrees) and [configuration guide](https://herdr.dev/docs/configuration/).

These semantics are directly reusable as product guidance: workspace presence, checkout existence, branch existence, and active process ownership must remain separate states.

### 2.8 Agent detection and attention state

Herdr recognizes a supported agent as a foreground process in a pane. It exposes the semantic states:

- `working`
- `blocked`
- `idle`
- `done`
- `unknown`

`done` is not a process lifecycle state; it means the agent is idle/ready and its completion has not been seen. Each TUI client tracks viewed completion independently, while server/API seen state follows explicit focus operations. A workspace’s attention state rolls up from its agents, making blocked/done work discoverable in the sidebar.

State evidence comes from two mechanisms:

1. **Screen manifests** inspect a live bottom-buffer shape after recognizing the process.
2. **Lifecycle integrations** allow supported agents to report semantic state directly.

Many integrations report only a native agent session reference for later resume, not lifecycle state. For example, Claude Code and Codex session identity may be integration-reported while working/blocked/idle still depends on screen evidence. Unsupported agents remain usable as ordinary terminal processes but may not receive rich classification.

Herdr deserves credit for exposing uncertainty instead of disguising it:

- `agent explain` reports the manifest source/version, matched rule, screen evidence, fallback, and direct-report authority.
- `blocked` detection is deliberately strict.
- a known agent can fall back to idle or unknown depending on ambiguity.
- Codex can remain `unknown` when the screen does not distinguish an active turn from an ordinary composer.
- `unknown` explicitly does not mean success.

This is valuable prior art for any AgentVision attention model: state should carry authority and evidence, not merely a badge.

### 2.9 Automation and API

Herdr exposes CLI wrappers over newline-delimited JSON on a Unix socket or Windows named pipe. The [socket API](https://herdr.dev/docs/socket-api/) covers:

- session snapshots and event subscriptions;
- workspace and worktree operations;
- tab creation, selection, rename, reorder, and close;
- pane split, move, swap, zoom, layout, resize, process inspection, reads, input, waits, and close;
- agent list/get/read/explain/start/prompt/send/focus/wait;
- integration install/uninstall;
- plugin link/enable/actions/panes/logs;
- notifications, config reload, and server control.

The installed binary can emit a JSON Schema for the protocol. Consumers are advised to use CLI wrappers for simple automation and raw socket subscriptions for long-lived tools.

The [agent automation guide](https://herdr.dev/docs/agent-automation/) supports a workflow such as: split a pane, start an agent, submit a prompt, wait for a settled semantic state, then read the result. The safety limitations are important:

- waits observe state, not a uniquely identified agent turn;
- completion of an already active turn may satisfy a later wait;
- prompt timeout does not prove input was never delivered;
- a stalled prompt should be read before retry to avoid duplication;
- a replacement pane occupant is prevented from satisfying a pinned server-owned wait.

This is a credible interactive automation layer, not a durable task scheduler.

### 2.10 Plugins

Herdr plugins are executable packages declared by `herdr-plugin.toml`. A manifest can declare startup hooks, event hooks, shareable actions, link handlers, keybindings, and terminal pane entrypoints. Plugin panes may appear as splits, tabs, zoomed panes, overlays, or modal popups.

Plugins invoke Herdr through the same CLI/socket surface and run with the user’s authority. There is no sandbox. The marketplace indexes public repositories and is explicitly not a security review. Plugin v1 does not provide native nonterminal widgets, runtime action registration, or managed durable plugin storage; plugins own any files or databases they require.

This is enough to build workflow adjuncts. It is not evidence that workflow data has become part of Herdr’s core consistency model.

### 2.11 Boundaries and AgentVision relevance

Herdr’s documented core API is rich in live runtime objects but does not define first-class durable:

- task/dependency records;
- execution-run records with admission/ownership semantics;
- test or build evidence linked to an exact run/revision;
- PR review findings and resolution transitions;
- approval policies and auditable human decisions.

That is a documented-surface observation, not a proof that plugins cannot add them.

**Conclusion:** Herdr is the strongest direct baseline for AgentVision’s terminal runtime and agent-attention ambitions. AgentVision should assume that persistent terminals, agent badges, worktrees, automation, and multi-machine aggregation are table stakes rather than differentiation.

---

## 3. Paperclip

### 3.1 Product position and release baseline

Paperclip describes itself as the control plane or operating system for companies of AI agents. It is a web application with a React frontend, TypeScript/Express REST server, Drizzle data layer, and PostgreSQL or embedded PGlite. It is licensed under [MIT](https://github.com/paperclipai/paperclip/blob/master/LICENSE). Latest verified stable release: [v2026.916.1](https://github.com/paperclipai/paperclip/releases/tag/v2026.916.1), published September 21, 2026.

Paperclip’s README says it is not an agent framework, workflow builder, prompt manager, single-agent tool, or general code-review tool. Those statements locate the product center; they do not mean adjacent execution, workflow, chat, and review features are absent.

### 3.2 System architecture

The documented request flow is:

```text
scheduler / assignment / mention / manual action / routine
                         │
                         ▼
                      wake
                         │
                         ▼
             adapter or Paperclip Runner
                         │
                         ▼
               external/local agent runtime
                         │
                         ▼
       Paperclip REST API + captured output/session/cost
                         │
                         ▼
             durable run and work records
```

Traditional adapters bridge the control plane to Claude, Codex, Gemini, Cursor, OpenCode, Pi, Hermes, generic processes, HTTP services, and other runtimes. An adapter validates configuration, starts/calls the runtime, passes work context, captures output and usage, extracts resumable session state, and optionally supplies a transcript parser and skill synchronization. See the [architecture overview](https://github.com/paperclipai/paperclip/blob/master/docs/start/architecture.md) and [adapter documentation](https://docs.paperclip.ing/reference/adapters/overview/).

The long-standing “control plane, not execution plane” description now requires qualification. The [September 2026 release](https://github.com/paperclipai/paperclip/releases/tag/v2026.916.0) added a complete **experimental Paperclip Runner**, enabled as an available gate by default on self-hosted installations for agents explicitly configured to use it. Legacy adapters do not silently migrate. Paperclip is expanding into execution management while retaining the adapter boundary.

### 3.3 Organizational and work model

Paperclip’s first-order boundary is a **company**. One instance may host multiple companies, and company scope constrains agents, work, permissions, cost, and audit history.

Its core objects include:

- **Company** — isolation, settings, members, strategic context, and budget scope.
- **Human principal** — owner/admin/operator/viewer membership and grants.
- **Agent** — organizational employee with role, reporting manager, adapter/runner configuration, permissions, run policy, skills, and budget.
- **Goal** — an outcome, optionally in a hierarchy.
- **Project** — a grouping and execution/workspace context, often tied to a repository or directory.
- **Issue/task** — the executable work item with status, priority, assignee, hierarchy, dependencies, comments, documents, attachments, and work products.
- **Run** — one execution attempt or heartbeat associated with an agent and work context.
- **Approval/review** — governance or completion gates.
- **Connection** — credentials/accounts/capabilities granted to responsible principals.
- **Workspace** — an execution checkout/directory with provisioning and cleanup state.

The [product definition](https://github.com/paperclipai/paperclip/blob/master/doc/PRODUCT.md), [key concepts](https://docs.paperclip.ing/guides/welcome/key-concepts/), and [issues API](https://docs.paperclip.ing/reference/api/issues/) expose an important separation:

| Relationship | Meaning |
|---|---|
| Parent/child issue | Decomposition/structure |
| Blocker link | Scheduling dependency; must be acyclic and company-local |
| Assignee | Accountable agent or human |
| Checkout | Which active agent run currently owns execution rights |
| Execution run | The currently live execution path |
| Goal/project link | Why and where the work exists |

Parentage does not automatically mean blocking, and assignment does not by itself mean active execution.

### 3.4 Atomic checkout and concurrency

An agent-owned issue must be checked out to enter active `in_progress` execution. The checkout is atomic. A competing claimant receives `409 Conflict`; an agent can claim only as itself and supplies its current run identity. `checkoutRunId` records the run holding issue ownership, while `executionRunId` identifies the active execution path.

Same-agent continuation can be idempotent. Stale locks can be cleared or adopted under defined terminal/lost-run conditions. Releasing work returns it to a schedulable state and clears the appropriate ownership fields.

This is much stronger than “pane X appears busy.” It gives the system an authoritative answer to “who is allowed to advance this task right now?”

### 3.5 Scheduling, wakeups, and recovery

Paperclip’s agents normally execute in bounded **heartbeats** and become dormant afterward. Wake sources include assignment, comments or mentions, manual invocation, routines, and selected system events. Interval polling is opt-in rather than the default. Run policy includes cooldown and concurrency limits. Paused agents reject wake paths.

Routines turn cron schedules or signed webhooks into tracked work and define overlap policies such as coalesce, skip, or enqueue. Catch-up after downtime is bounded.

The server records queued wakes and reconciles orphaned runs, queued work, stale ownership, and stranded assigned tasks at startup and periodically. Its current execution semantics articulate a useful liveness invariant: each nonterminal agent task should have an inspectable next path, such as an active run, queued wake, reviewer, required human interaction, blocker, human owner, or recovery action. See [execution semantics](https://github.com/paperclipai/paperclip/blob/master/doc/execution-semantics.md).

Human-owned issues remain durable and visible but are not driven by the heartbeat scheduler.

### 3.6 Agent continuity and terminal semantics

Adapters can persist a provider/runtime session identifier and resume a later heartbeat into the same model conversation. That is durable conversational context. It does **not** imply that a PTY or interactive process remains continuously alive between heartbeats.

Some adapters use structured ACP or machine-readable event streams; others parse CLI streams or capture ordinary process output. Paperclip’s authoritative unit is the run and its records, not a terminal screen.

This is the mirror image of Herdr:

- Herdr preserves a live PTY and infers/reports agent state around it.
- Paperclip preserves durable work/run/session metadata and may launch a new runtime process for the next burst.

### 3.7 Execution workspaces

Projects can use a primary shared checkout, reuse a prior execution workspace, or provision an isolated workspace per task. The current isolated Git implementation creates a worktree and branch from a configured base, runs provisioning commands, and gives its directory to the runtime. Workspaces can survive multiple runs and can be shared by related tasks.

The docs still label these surfaces experimental. Cleanup and teardown are explicit states, including `cleanup_failed`. The product warns that teardown can destroy uncommitted work. A Git worktree is directory/branch isolation, not a process, credential, filesystem, or network sandbox. [Workspaces](https://docs.paperclip.ing/guides/projects-workflow/workspaces/) documents these boundaries.

Compared with Herdr:

- Herdr treats a worktree primarily as a navigable live workspace.
- Paperclip treats a worktree primarily as an execution workspace tied to task/run lifecycle and policy.

Both separate worktree deletion from branch deletion, but Paperclip’s cleanup automation raises a larger data-loss and policy surface.

### 3.8 Review, approval, and governance

Paperclip has two related but distinct gate systems:

1. **Execution policy** can require review, approval, both, or neither before an issue becomes done. The server redirects a requested completion into the configured stage, reassigns to the reviewer/approver, and returns changes-requested work to the original executor. Repeated agent-review loops can escalate to a human.
2. **Board/governance approvals** apply to decisions such as strategy, hiring, and budget overrides. Approve, reject, and request-revision are durable actions.

See [execution policy](https://docs.paperclip.ing/guides/power/execution-policy/) and [approvals](https://docs.paperclip.ing/guides/day-to-day/approvals/).

Budgets exist at company and agent monthly scopes and project lifetime scope. Documentation describes warnings before the limit and a hard stop at exhaustion, after which an operator can raise the limit or keep execution paused. Cost and override actions are recorded.

Human company roles and grants are separate from the agent reporting hierarchy. Recent Connections work moves runtime credentials and external accounts into explicit responsibility and grant boundaries. The local secret provider encrypts stored material with a machine-local key.

### 3.9 Trust and isolation

Paperclip’s low-trust review preset combines reduced API scope, reduced secret access, isolated workspaces, and a required sandbox driver. Its documentation correctly states that API permissions do not sandbox a host-local process. A task worktree also does not sandbox a process.

This distinction should be copied directly into AgentVision’s language: a PTY, worktree, pane, remote connection, or read-only UI mode is not a security boundary unless OS/container/VM credentials and resource access enforce one.

### 3.10 Operator surfaces and extensibility

Paperclip’s web UI exposes dashboard, org chart, agents, issues/inbox, comments/conversation, documents, run transcripts, workspaces, costs, approvals, and activity. The activity feed records actors and structural changes. Run records retain invocation details, transcripts, touched issues, provider session identifiers, results, and usage.

The REST API publishes OpenAPI. The CLI covers company, project, goal, issue, agent, run, routine, approval, workspace, budget, activity, auth, and context operations. The codebase includes plugin and SDK packages, skills and team catalogs, and a broad adapter ecosystem. Recent releases add managed Connections, per-person GitHub identities, agent email, experimental chat connectors, and the Paperclip Runner.

Paperclip is therefore no longer a narrow task board. It is a broad control-plane platform whose complexity and company metaphor are intentional product choices.

### 3.11 Boundaries and AgentVision relevance

Paperclip’s primary documented interface is a web control surface over burst-oriented runs and durable work. It does not document a Turbo Vision-style desktop of movable overlapping live PTY windows, nor a terminal multiplexer that keeps arbitrary interactive shells continuously attached. That conclusion is based on reviewed documented surfaces, not an exhaustive proof of absence in every plugin or experimental branch.

**Conclusion:** Paperclip is the strongest prior-art source for AgentVision’s potential durable work model: Task → Run → Agent, atomic ownership, liveness/recovery, review handoffs, budgets, approvals, workspaces, and audit. AgentVision should not reproduce the entire autonomous-company metaphor unless evidence shows users need it.

---

## 4. Direct comparison

| Dimension | Herdr | Paperclip |
|---|---|---|
| Product center | Live terminal workspace for humans supervising coding agents | Durable control plane for organizations of agents |
| Primary UI | Terminal TUI | Web application and CLI/API |
| Authoritative unit | Server/session/pane and recognized live occupant | Company/task/assignment/run/policy |
| Agent identity | Current recognized process in a pane, optionally named and session-linked | Durable employee/principal with adapter, role, policy, permissions, and budget |
| Execution model | Long-lived interactive processes in real PTYs | Burst-oriented heartbeats/runs via adapters or experimental runner |
| Process persistence | Yes across detach while server lives | Adapter/provider-specific; conversation can resume across runs, PTY continuity not assumed |
| Cold restart | Layout restored; arbitrary processes not preserved; supported agent conversation may resume | Durable DB state and run/work recovery; runtime process restarted/reinvoked as needed |
| Terminal state | Server-owned rendered screen and scrollback | Transcript/event/output records; no canonical general PTY screen model documented |
| Work hierarchy | Session/workspace/tab/pane/agent | Company/goal/project/issue/agent/run/workspace |
| Task dependencies | Not a documented core object | First-class blocker edges, distinct from hierarchy |
| Execution ownership | Input/takeover authority and live pane occupant | Atomic issue checkout bound to a run |
| Attention | Working/blocked/idle/done/unknown with evidence and rollups | Issue/run/approval/review/interaction/recovery states |
| Human “seen” state | Per-client completion acknowledgement | Durable inbox/activity/approval workflow |
| Review policy | Not a documented core durable transition system | Configurable review/approval stages and changes-requested loops |
| Worktrees | First-class live workspace management | Experimental task execution workspaces with provisioning/cleanup |
| Remote | SSH attach and multi-machine TUI aggregation | Remote adapters/runners, HTTP control plane, deployment modes, connectors |
| Multi-user | Multiple terminal clients for the same OS-user-owned runtimes | Authenticated human memberships, roles, grants, multi-company boundaries |
| Extension | Executable plugins using CLI/socket; terminal panes/actions/hooks | Adapters, plugins/SDK, skills, connectors, runner/backends |
| Cost/budget | Not a documented core concern | First-class usage and enforced budgets |
| Audit | Live events and server state; plugin logs | Durable activity, comments, runs, approvals, cost records |
| License | Apache-2.0 | MIT |

## 5. What the comparison means for AgentVision

### Verified competitive baseline

AgentVision cannot credibly differentiate on any single item below:

- keeping coding agents alive after client detach;
- showing working/blocked/done-style attention;
- supervising several remote machines in one terminal client;
- managing Git worktrees beside live agents;
- scripting agent start/prompt/wait/read;
- showing tasks, assignments, dependencies, runs, reviews, approvals, and budgets;
- isolating task work into execution worktrees;
- maintaining an activity/audit record.

Herdr or Paperclip already documents each of those capabilities.

### Potential synthesis

The opening is the relationship between live and durable state:

```text
durable task
  └── current/previous run
       ├── assigned agent identity
       ├── provider execution reference
       ├── live process/PTY attachment, if one exists
       ├── exact worktree/branch/revision
       ├── verification evidence
       ├── review findings
       └── pending human decision
```

Herdr is strongest at the lower live-runtime half. Paperclip is strongest at the upper durable-control half. AgentVision can test whether a spatial desktop makes the connection between them faster to understand and safer to operate.

### What not to copy wholesale

- Do not copy Herdr’s screen-classification rules as the sole source of truth for durable task state. A screen can indicate attention but cannot prove task ownership, test success, or review completion.
- Do not copy Paperclip’s entire company/org-chart metaphor into the first product proof. It brings substantial governance and configuration complexity before the live workbench value is established.
- Do not equate worktrees with sandboxes in either model.
- Do not assign a durable task’s identity to a pane. Pane occupants change, panes move, processes restart, and providers differ.
- Do not make one opaque `status` enum carry process state, task state, attention, review, and connectivity. Those have different authorities and failure modes.

## 6. Bottom line

- **Herdr is the closest direct product competitor and runtime prior art.** It sets a high bar for persistent agent terminals and attention-aware supervision.
- **Paperclip is the strongest control-plane prior art.** It provides concrete semantics for durable work, execution ownership, scheduling, recovery, review, governance, cost, and audit.
- **They are more complementary than interchangeable.** A Herdr-like runtime could plausibly execute work governed by a Paperclip-like control plane.
- **AgentVision’s product thesis must be the integrated supervision experience**, not the isolated existence of either layer.

The next useful comparison is behavioral: give operators the same synthetic set of concurrent tasks in Herdr, Paperclip, and an AgentVision prototype, then measure time to identify what needs attention, find the evidence, intervene correctly, and recover context. Aesthetic preference alone will not establish differentiation.
