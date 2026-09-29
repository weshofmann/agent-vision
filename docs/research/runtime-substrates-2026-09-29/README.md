# AgentVision substrate and prior-art options

**Research date:** 2026-09-29

**Status:** Research and technology watch

**Scope:** tmux, Zellij, WezTerm mux, abduco, Herdr, and Paperclip

This dossier explains current upstream capabilities, compares architecture and
product boundaries, and identifies what AgentVision might reuse or validate.
It is not a roadmap commitment, dependency selection, migration authorization,
or approval to begin implementation.

The dossier complements the runtime decisions recorded in
[Issue #7](https://github.com/weshofmann/agent-vision/issues/7) and the direction
described by [Draft PR #10](https://github.com/weshofmann/agent-vision/pull/10).
It does not alter the implementation trial in
[Draft PR #13](https://github.com/weshofmann/agent-vision/pull/13).

## Document map

1. [Terminal multiplexer capability survey](01-terminal-multiplexer-capability-survey.md) — architecture, sessions, clients, remote access, control surfaces, extension models, maturity, and licensing.
2. [Herdr and Paperclip capability survey](02-herdr-paperclip-capability-survey.md) — product and architecture descriptions followed by a direct comparison.
3. [AgentVision reuse and leverage guide](03-agentvision-reuse-and-leverage-guide.md) — reusable ideas, architecture options, decision criteria, and bounded validation experiments.
4. [Source and license index](04-source-and-license-index.md) — primary sources, release baselines, license notes, and evidence limitations.

## Executive conclusion

There are three different problems hiding behind the phrase “terminal
multiplexer,” and no candidate is strongest at all three:

1. **PTY/process lifetime** — keep a real interactive process alive while clients
   detach.
2. **Terminal-state ownership** — turn the child’s byte stream into a canonical
   screen, scrollback, cursor, and mode model.
3. **Workspace presentation and control** — arrange panes/windows, route
   focus/input, expose APIs, and support human supervision.

The candidates place those responsibilities differently:

| Candidate | What it primarily owns | Strongest reusable value | Main cost for AgentVision |
| --- | --- | --- | --- |
| **tmux** | PTYs, parsed terminal state, history, sessions/windows/panes, attached clients | Mature lifecycle plus a documented foreign-client protocol that emits per-pane application bytes | AgentVision must maintain and reconcile its own terminal view; tmux modes and geometry semantics need careful adaptation |
| **Zellij** | PTYs, parsed terminal state, rich layout, collaboration, plugins, web/terminal clients | Modern workspace UX, strong automation, WASM plugins, rendered-state observation, remote web attach | Its documented external surface exposes rendered state rather than raw PTY output; a completely independent frontend is not a documented first-class use case |
| **WezTerm mux** | PTYs, parsed terminal state, mux domains, native GUI integration, remote domains | Deep emulator/mux integration and useful local/SSH/TLS domain concepts | The documented extension surface is primarily WezTerm Lua and CLI, not a stable general frontend protocol |
| **abduco** | One detached PTY/process per session and raw-byte forwarding | Very small, understandable process-lifetime primitive | AgentVision would still need terminal emulation, scrollback, layout, metadata, recovery, and most protocol design |

For a **disposable substrate experiment**, tmux remains the most promising first
probe because control mode is explicitly intended for an alternative client. That
is not a recommendation to adopt tmux permanently. Zellij should be the second
probe if rendered screen updates can satisfy AgentVision’s frontend needs. WezTerm
is a valuable architecture and remote-domain reference, but its current public
integration boundary is less favorable to a foreign TUI. abduco is useful mainly
as the lower bound: the smallest credible thing that buys live detach/reattach.

Herdr and Paperclip are not substitutes for one another:

| Product | Product center | What it proves already exists |
| --- | --- | --- |
| **Herdr** | Persistent terminal workspace and live coding-agent supervision | Multi-client persistent PTYs, multi-machine aggregation, worktrees, agent attention states, agent automation, terminal reads, event subscriptions, and plugins |
| **Paperclip** | Durable work and governance control plane for organizations of agents | Structured tasks, assignment and atomic checkout, runs, wake/recovery semantics, budgets, approvals, review gates, audit history, connections, adapters, and experimental execution workspaces |

The defensible AgentVision opportunity is therefore not “terminal multiplexer
plus agent badges.” Herdr already occupies much of that space. Nor is it “task
board for agents.” Paperclip already has a broad implementation of that layer.
The product hypothesis worth testing is a **spatial, keyboard-first supervision
workbench that makes live terminals and durable work evidence visibly one
system**: task, run, process, terminal, test result, review state, and human
decision should be related without forcing the operator to mentally join a
terminal multiplexer to a web control plane.

## Evidence labels

The detailed documents distinguish:

- **Verified fact** — directly supported by current upstream documentation,
  source, release notes, or licenses.
- **Interpretation** — an architectural consequence inferred from verified
  behavior.
- **Hypothesis** — a claim AgentVision still has to validate with a prototype or
  user workflow.

No software was installed or executed for this research. “Current capability”
means documented or source-verifiable capability as of the research date.
Performance, fidelity, and operational quality still require real PTY experiments.

## Reuse principle

AgentVision should avoid making a single early choice do three jobs:

```text
durable work model
        │
        ▼
AgentVision presentation / interaction model
        │
        ▼
terminal provider boundary
        │
        ├── direct PTY/reference provider
        ├── tmux provider
        ├── possible Zellij provider
        └── later remote/provider-specific implementations
```

A terminal substrate should not define AgentVision’s durable task model.
Conversely, a task control plane should not dictate how terminal cells are
rendered or focus is routed. Preserving that boundary lets AgentVision learn
from tmux, Zellij, WezTerm, Herdr, and Paperclip without becoming a skin over
any one of them.
