# Source, release, and license index

**Research date:** 2026-09-29

**Source policy:** upstream project documentation, repositories, source, release notes, and licenses were preferred. No capability claim in the companion documents depends on a third-party comparison article.

## 1. Release and license baseline

| Project | Stable baseline used | Newer channel observed | License | License implication at a glance |
|---|---|---|---|---|
| tmux | [3.7c](https://github.com/tmux/tmux/releases/tag/3.7c) | 3.8 release candidates | [ISC](https://github.com/tmux/tmux/blob/master/COPYING) | Permissive; preserve copyright and permission text |
| Zellij | [0.45.1](https://github.com/zellij-org/zellij/releases/tag/v0.45.1) | `main` development | [MIT](https://github.com/zellij-org/zellij/blob/main/LICENSE.md) | Permissive; preserve copyright and license text |
| WezTerm | [20240203-110809-5046fc22](https://github.com/wezterm/wezterm/releases) | Continuous/nightly builds | [MIT](https://github.com/wezterm/wezterm/blob/main/LICENSE.md) | Permissive; preserve copyright and license text; individual dependencies still require review |
| abduco | [0.6](https://github.com/martanne/abduco/releases), released in 2016 | Repository source | [ISC](https://github.com/martanne/abduco/blob/master/LICENSE) | Permissive; preserve copyright and permission text |
| Ghostty / `libghostty-vt` | No independent stable library-version commitment used | Repository `main` API is evolving | [MIT](https://github.com/ghostty-org/ghostty/blob/main/LICENSE) | Permissive; API stability and vendored dependency notices still require review |
| `libtsm` | Repository source/documentation | Repository development | [Core license and bundled notices](https://github.com/kmscon/libtsm/blob/master/COPYING) | Core is MIT; bundled hash-table code includes LGPL-2.1-or-later terms—review exact files/build before reuse |
| Herdr | [v0.9.1](https://github.com/herdrdev/herdr/releases/tag/v0.9.1) | Preview builds, including 2026-09-28 | [Apache-2.0](https://github.com/herdrdev/herdr/blob/master/LICENSE) | Permissive with notice, license, and patent provisions; modified-file/NOTICE handling should be reviewed for code reuse |
| Paperclip | [v2026.916.1](https://github.com/paperclipai/paperclip/releases/tag/v2026.916.1) | Repository `master` and experimental feature gates | [MIT](https://github.com/paperclipai/paperclip/blob/master/LICENSE) | Permissive; preserve copyright and license text; plugin/connector dependencies require separate review |

This is an engineering summary, not legal advice. Any actual vendoring, linking, redistribution, or binary packaging should produce a dependency-specific bill of materials and notice review.

### Source snapshot commits

The following branch heads were recorded on the research date so claims based on
fast-moving repository documentation can be reproduced even after the named
branches advance. Release-tag links above remain the stable baselines.

| Project | Inspected repository snapshot |
| --- | --- |
| tmux | [`ffbbdcc8529e9184ecf2386cc75cedfa91d6f172`](https://github.com/tmux/tmux/tree/ffbbdcc8529e9184ecf2386cc75cedfa91d6f172) |
| Zellij | [`a79e15e178cd30b77b057db59ef0f05f2318c9f9`](https://github.com/zellij-org/zellij/tree/a79e15e178cd30b77b057db59ef0f05f2318c9f9) |
| WezTerm | [`cab25161054c50fd6c705db4ceefef0f1e5a9575`](https://github.com/wezterm/wezterm/tree/cab25161054c50fd6c705db4ceefef0f1e5a9575) |
| abduco | [`8c32909a159aaa9484c82b71f05b7a73321eb491`](https://github.com/martanne/abduco/tree/8c32909a159aaa9484c82b71f05b7a73321eb491) |
| Ghostty / `libghostty-vt` | [`0538f7535be0cbca6bbe54e6fde654d5c628f1f2`](https://github.com/ghostty-org/ghostty/tree/0538f7535be0cbca6bbe54e6fde654d5c628f1f2) |
| `libtsm` | [`ef2365d0f9f1a370d721a82470d69c3646fe97f0`](https://github.com/kmscon/libtsm/tree/ef2365d0f9f1a370d721a82470d69c3646fe97f0) |
| Herdr | [`898f733f92dbbe53c428845534b03adb573f92be`](https://github.com/herdrdev/herdr/tree/898f733f92dbbe53c428845534b03adb573f92be) |
| Paperclip | [`b3eb03fcbaaa1872776efbee18962def3a5e2d40`](https://github.com/paperclipai/paperclip/tree/b3eb03fcbaaa1872776efbee18962def3a5e2d40) |

## 2. tmux primary sources

| Topic | Source | What it establishes |
|---|---|---|
| Architecture and object hierarchy | [Getting Started](https://github.com/tmux/tmux/wiki/Getting-Started) | Server/client ownership; session/window/pane model; detach behavior; window linking |
| Foreign client | [Control Mode](https://github.com/tmux/tmux/wiki/Control-Mode) | Command framing, asynchronous notifications, raw escaped pane output, flow control, capture-based recovery, resize participation |
| State queries | [Formats](https://github.com/tmux/tmux/wiki/Formats) | Structured formatting for sessions, windows, panes, clients, commands, filters, and status |
| Size policies | [Advanced Use](https://github.com/tmux/tmux/wiki/Advanced-Use) | Shared window dimensions, client size policies, manual sizing |
| Stable 3.7 behavior | [3.7c CHANGES](https://github.com/tmux/tmux/blob/3.7c/CHANGES) | Floating panes added and explicitly described as early/limited |
| Upcoming behavior | [Current CHANGES](https://github.com/tmux/tmux/blob/master/CHANGES) | 3.8 prerelease floating-pane, layout, theme, and input changes; not treated as stable baseline |
| Platforms/repository | [tmux repository](https://github.com/tmux/tmux) | Supported Unix-family targets, project activity, build references |
| License | [COPYING](https://github.com/tmux/tmux/blob/master/COPYING) | ISC terms |

## 3. Zellij primary sources

| Topic | Source | What it establishes |
|---|---|---|
| Internal architecture | [ARCHITECTURE.md](https://github.com/zellij-org/zellij/blob/main/docs/ARCHITECTURE.md) | Screen/layout ownership, PTY bus, terminal pane/grid responsibilities |
| Current release | [v0.45.1](https://github.com/zellij-org/zellij/releases/tag/v0.45.1) | Current stable patch and 0.45 feature context |
| Detailed changes | [CHANGELOG](https://github.com/zellij-org/zellij/blob/main/CHANGELOG.md) | Windows fixes, per-tab sizing, mobile/PWA, protocol and layout changes |
| Programmatic control | [Programmatic Control](https://zellij.dev/documentation/programmatic-control.html) | JSON queries, targeted actions, screen dumps, NDJSON subscriptions, concurrency/ordering semantics |
| Persistence | [Session Resurrection](https://zellij.dev/documentation/session-resurrection.html) | Layout/command recreation, optional viewport/scrollback, distinction from live-process survival |
| Plugins | [Plugins](https://zellij.dev/documentation/plugins.html) | WASM/WASI first-class plugin panes and UI/event/control role |
| Plugin security | [Permissions](https://zellij.dev/documentation/plugin-api-permissions) | Capability requests for state, input, commands, filesystem, pane contents, web server, clipboard, and more |
| Remote/browser | [Web Client](https://zellij.dev/documentation/web-client.html) | Auth tokens, HTTPS, browser UI, PWA/mobile, terminal-to-terminal remote attach, security guidance |
| Installation/platforms | [Installation](https://zellij.dev/documentation/installation.html) | Current binary/platform distribution, including native Windows artifacts |
| License | [LICENSE.md](https://github.com/zellij-org/zellij/blob/main/LICENSE.md) | MIT terms |

## 4. WezTerm primary sources

| Topic | Source | What it establishes |
|---|---|---|
| Mux architecture and domains | [Multiplexing](https://wezterm.org/multiplexing.html) | Local, Unix, SSH, and TLS domains; remote bootstrap/reconnect; feature maturity warning |
| Mux object API | [`wezterm.mux`](https://wezterm.org/config/lua/wezterm.mux/index.html) | Windows, tabs, panes, domains, workspaces, spawn/manipulation within Lua runtime |
| Pane ownership | [Pane object](https://wezterm.org/config/lua/pane/index.html) | PTY/process association, parsed screen, scrollback, pane functions |
| CLI surface | [`wezterm cli`](https://wezterm.org/cli/cli/index.html) | Pane/client inventory, spawn/split/activate/input/capture operations |
| Capture | [`get-text`](https://wezterm.org/cli/cli/get-text.html) | Text, scrollback, and escape-aware pane capture |
| Multi-client inventory | [`list-clients`](https://wezterm.org/cli/cli/list-clients.html) | Connected client and focus/workspace data |
| Platforms/features | [Features](https://wezterm.org/features.html) | Supported systems, terminal graphics, remote mux capabilities, splits/tabs/scrollback |
| Release cadence | [Releases](https://github.com/wezterm/wezterm/releases) and [changelog](https://wezterm.org/changelog.html) | Latest stable tag and continued nightly development |
| License | [LICENSE.md](https://github.com/wezterm/wezterm/blob/main/LICENSE.md) | MIT terms |

## 5. abduco and terminal-state libraries

| Project/topic | Source | What it establishes |
|---|---|---|
| abduco behavior | [Project guide / README](https://github.com/martanne/abduco) | Named session attach/detach, read-only mode, exit status, resize policy, socket recovery, dvtm pairing |
| abduco server internals | [`server.c`](https://github.com/martanne/abduco/blob/master/server.c) | Raw PTY packet relay, attached-client behavior, lack of canonical screen/history |
| abduco client internals | [`client.c`](https://github.com/martanne/abduco/blob/master/client.c) | Outer terminal setup/restoration and data relay |
| abduco release/license | [Releases](https://github.com/martanne/abduco/releases), [LICENSE](https://github.com/martanne/abduco/blob/master/LICENSE) | 0.6 age and ISC terms |
| Ghostty library scope | [Ghostty repository](https://github.com/ghostty-org/ghostty), [`vt.h`](https://github.com/ghostty-org/ghostty/blob/main/include/ghostty/vt.h) | Terminal parsing/state API rather than PTY/session management; API evolution warning |
| `libtsm` scope/license | [libtsm repository](https://github.com/kmscon/libtsm), [COPYING](https://github.com/kmscon/libtsm/blob/master/COPYING) | VT state-machine scope and mixed bundled license notices |

## 6. Herdr primary sources

| Topic | Source | What it establishes |
|---|---|---|
| Product/object model | [Concepts](https://herdr.dev/docs/concepts/) | Session/workspace/tab/pane/agent model; server/client ownership; multi-client sizing |
| Day-to-day capability | [Quick Start](https://herdr.dev/docs/quick-start/) | Mouse/keyboard navigation, persistent panes, sidebar agent state |
| Agent model | [Agents](https://herdr.dev/docs/agents/) | Supported agents, state authorities, screen manifests, fallbacks, uncertainty |
| Agent control | [Agent Automation](https://herdr.dev/docs/agent-automation/) | Start/prompt/wait/read workflow and its race/turn limitations |
| Full control surface | [Socket API](https://herdr.dev/docs/socket-api/) | NDJSON schema, events, pane/worktree/agent/plugin operations, snapshot and wait semantics |
| CLI/worktrees | [CLI Reference](https://herdr.dev/docs/cli-reference/) | Command families, machine routing, worktree separation, terminal attach |
| Persistence | [Session State and Restore](https://herdr.dev/docs/session-state/) | Live detach, cold restart, screen-history risk, native agent resume, live handoff boundaries |
| Remote modes | [How to Work](https://herdr.dev/docs/how-to-work/), [Connecting Machines](https://herdr.dev/docs/connecting-machines/) | SSH-on-host, local remote client, multi-machine aggregation and failure boundaries |
| Configuration/graphics | [Configuration](https://herdr.dev/docs/configuration/) | Worktree path, layout/UI, popups, notifications, terminal graphics, screen history |
| Plugins | [Plugin material within Socket API](https://herdr.dev/docs/socket-api/#plugin-apis), [Marketplace](https://herdr.dev/docs/marketplace/) | Executable plugin manifests, actions/hooks/panes, persistence, trust boundary |
| Windows | [Windows Support](https://herdr.dev/docs/windows-beta/) | ConPTY behavior, partial capabilities, cursor/graphics caveats |
| Release/license | [Releases](https://github.com/herdrdev/herdr/releases), [LICENSE](https://github.com/herdrdev/herdr/blob/master/LICENSE) | Stable/preview baseline and Apache-2.0 terms |

## 7. Paperclip primary sources

| Topic | Source | What it establishes |
|---|---|---|
| Product definition | [PRODUCT.md](https://github.com/paperclipai/paperclip/blob/master/doc/PRODUCT.md) | Company control-plane model, design goals, boundaries, current direction |
| Runtime architecture | [Architecture](https://github.com/paperclipai/paperclip/blob/master/docs/start/architecture.md) | React/Express/Postgres stack, adapter invocation flow, control-plane boundary |
| Current execution semantics | [Execution Semantics](https://github.com/paperclipai/paperclip/blob/master/doc/execution-semantics.md) | Checkout/run identity, liveness, human versus agent ownership, recovery and configuration blockers |
| Core concepts | [Key Concepts](https://docs.paperclip.ing/guides/welcome/key-concepts/) | Company, agents, tasks, approvals, budgets, operating model |
| Issue/task API | [Issues API](https://docs.paperclip.ing/reference/api/issues/) | Fields, statuses, assignment, blockers, atomic checkout, status transitions |
| Agents/adapters | [Agents](https://docs.paperclip.ing/guides/org/agents/), [Adapters](https://docs.paperclip.ing/reference/adapters/overview/) | Agent configuration, runtime bridges, session/usage capture, skills |
| Scheduling | [Routines](https://docs.paperclip.ing/guides/projects-workflow/routines/) | Cron/webhook creation, concurrency policies, wake behavior, catch-up |
| Execution workspaces | [Workspaces](https://docs.paperclip.ing/guides/projects-workflow/workspaces/) | Primary/reused/isolated workspaces, Git worktrees, provisioning, cleanup, experimental status |
| Review policy | [Execution Policy](https://docs.paperclip.ing/guides/power/execution-policy/) | Review/approval stage routing, changes requests, escalation |
| Governance | [Approvals](https://docs.paperclip.ing/guides/day-to-day/approvals/) | Human approval queue and durable decision paths |
| Activity/audit | [Activity Log](https://docs.paperclip.ing/guides/day-to-day/activity-log/) | Actor-attributed durable activity visibility |
| API/CLI | [API Overview](https://docs.paperclip.ing/reference/api/overview/), [CLI Overview](https://docs.paperclip.ing/reference/cli/overview/) | OpenAPI and broad operator/agent command surface |
| Current release | [v2026.916.0](https://github.com/paperclipai/paperclip/releases/tag/v2026.916.0), [v2026.916.1](https://github.com/paperclipai/paperclip/releases/tag/v2026.916.1) | Connections, per-person identities, chat connectors, experimental Runner, patch baseline |
| License | [LICENSE](https://github.com/paperclipai/paperclip/blob/master/LICENSE) | MIT terms |

## 8. Evidence limitations

### Not runtime-tested

This survey did not install or execute any candidate. The following remain hypotheses until behavioral tests are run:

- terminal fidelity under real coding-agent TUIs;
- high-throughput/backpressure behavior;
- race-free snapshot/stream joining;
- multi-client resize experience;
- remote latency and reconnect behavior;
- plugin/API stability in practice;
- operational failure recovery;
- performance and memory usage;
- interoperability with AgentVision’s current direct provider.

### Documentation drift

Fast-moving projects sometimes have inconsistent surfaces:

- tmux wiki pages can mention the latest prerelease; stable 3.7 and 3.8 prerelease behavior were separated where material.
- Zellij’s older FAQ statements may lag current native Windows releases; current installation artifacts and 0.45 changes were preferred.
- WezTerm’s stable tag is old while documentation and nightlies continue; both facts are reported.
- Herdr’s live docs can reflect preview development beyond v0.9.1; stable claims were checked against releases/changelog where possible.
- Paperclip’s `master` product docs and live docs may include features newer than v2026.916.1; recent experimental features are labeled accordingly.

### Absence claims

Statements such as “no documented task graph” or “no documented stable foreign-frontend protocol” mean that the reviewed official public surface does not define one. They do not prove that no internal code, branch, plugin, or unpublished interface exists.

## 9. Verification checklist before adoption

Before adopting any dependency, re-verify:

- exact selected tag/commit and release channel;
- direct and transitive licenses;
- supported operating systems and packaging;
- documented versus internal API boundary;
- protocol/version compatibility guarantees;
- security and authentication assumptions;
- server/client upgrade behavior;
- process and data cleanup semantics;
- saved terminal-content and secret exposure;
- real PTY fidelity tests against the exact AgentVision workloads.
