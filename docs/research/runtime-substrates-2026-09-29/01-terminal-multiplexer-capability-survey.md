# Terminal multiplexer capability survey

**Research date:** 2026-09-29

**Candidates:** tmux, Zellij, WezTerm mux, abduco

**Related components:** `libghostty-vt`, `libtsm`

## 1. The comparison model

“Multiplexer” is an overloaded label. The useful comparison starts by separating six responsibilities:

1. **Process supervision** — starts a child, owns its PTY, observes exit, and preserves it across client detach.
2. **Terminal emulation** — interprets VT escape sequences and maintains the screen, cursor, modes, attributes, and scrollback.
3. **Workspace topology** — sessions, windows, tabs, panes, splits, floats, focus, and resize rules.
4. **Client protocol** — how one or more frontends observe state, send input, resize, recover after lag, and reconnect.
5. **Automation and extension** — commands, APIs, event subscriptions, hooks, plugins, and stable object identities.
6. **Durability and remote reach** — what survives detach, server death, host restart, and network interruption; how remote hosts are attached.

The architectural question for AgentVision is not simply “which multiplexer has the most features?” It is “which responsibilities can be reused without fighting AgentVision’s own desktop, object model, and UX?”

## 2. Comparison at a glance

| Dimension | tmux 3.7c | Zellij 0.45.1 | WezTerm mux | abduco 0.6 |
|---|---|---|---|---|
| License | ISC | MIT | MIT | ISC |
| Primary implementation | C | Rust | Rust | C |
| Process/PTY owner | Server | Server | Mux domain/server | Per-session server |
| Canonical parsed screen | Yes | Yes | Yes | No |
| Native hierarchy | Session → window → pane | Session → tab → terminal/plugin pane | Domain → workspace/window → tab → pane | Named session → one process/PTY |
| Tiled panes | Mature | Mature | Mature | No |
| Floating terminal panes | Stable 3.7 feature, explicitly early/limited in 3.7c | Mature first-class feature | No documented terminal-space equivalent | No |
| Detach preserves live process | Yes, while server lives | Yes, while server lives | Yes when using a persistent mux server/domain | Yes, while session server lives |
| Cold restart resurrects process | No | No; recreates layout/commands | No documented live-process resurrection | No |
| Multi-client | Yes | Yes, collaborative | Yes | Yes |
| Read-only client | Yes | Yes, including read-only web tokens | Observation/control depends on client/API path | Yes, convenience only |
| Foreign-client interface | **Documented control mode** | Strong CLI/NDJSON rendered-state interface; internal full-client protocol not public API | Lua/CLI; no documented stable general frontend protocol | Small raw socket protocol in source, no supported SDK/API |
| External pane data | Raw application bytes plus snapshots | Rendered viewport/scrollback, optionally ANSI-styled | Captured text/escapes; live drawing is native client behavior | Raw live bytes, no snapshot/history |
| Remote model | Run tmux through SSH; control mode works over SSH | Built-in authenticated web client and HTTPS terminal attach; SSH remains common | Native SSH, Unix, and TLS mux domains | Use SSH or another transport around local socket access |
| Plugin model | Config, commands, hooks, scripts; no native plugin ABI | Permissioned WASM/WASI plugins | Lua config/events/plugins inside WezTerm | None documented |
| Native Windows | No | Yes in current releases | Yes | No documented native support |
| Current release signal | Active stable and prerelease cadence | Active stable cadence | Old stable tag; active nightlies | Last release in 2016 |
| Best AgentVision use | First substrate probe | Second substrate/automation probe; UX reference | Architecture and remote-domain reference; possible later integration | Minimal-lifetime reference or component, not whole substrate |

The table is deliberately harsh about cold restart: “session persistence” commonly means that a background server remains alive after the user closes a client. It does not usually mean reconstructing an arbitrary process, memory image, file descriptors, or PTY after that server or host dies.

---

## 3. tmux

### 3.1 Product and architecture

**Verified facts.** tmux’s server owns programs, PTYs, parsed pane state, history, windows, sessions, options, and attached-client state. Programs run in panes; panes belong to windows; windows can be linked into one or more sessions; clients attach to one session. Multiple clients may attach to the same session. The server normally exits after there are no programs left. The official [Getting Started guide](https://github.com/tmux/tmux/wiki/Getting-Started) is explicit about this hierarchy and ownership.

This object model has a useful distinction that is easy to miss: a **window is not owned exclusively by one session**. Linking the same window into multiple sessions lets different navigation collections refer to the same underlying panes. AgentVision probably should not copy this unless a real workflow needs aliasing; it complicates ownership and deletion semantics.

tmux is both a process/session manager and a terminal emulator. It reads each pane’s PTY stream, parses terminal control sequences, maintains history, and renders a composite terminal for each client. That is why a reattaching normal client can redraw the current screen even though it missed the original byte stream.

### 3.2 Layout and interaction

tmux’s mature layout model is tiled. It supports horizontal and vertical splits, preset arrangements, pane movement/swapping, zoom, window linking, copy mode, chooser modes, popups, menus, and a programmable status line.

**Correction to older comparisons:** tmux 3.7 added actual floating panes. They sit above tiled panes and behave as panes rather than modal popups. However, the [3.7c change log](https://github.com/tmux/tmux/blob/3.7c/CHANGES) calls the feature early and relatively limited: movement and resize are mouse-oriented, and custom layouts containing floats are not restored in the same way as tiled layouts. The [3.8 prerelease changes](https://github.com/tmux/tmux/blob/master/CHANGES) add much richer floating-pane movement, resize, modal behavior, tiling/floating conversion, and JSON-subset layout serialization. Those 3.8 features must not be treated as stable 3.7 behavior.

**Interpretation.** tmux no longer fails AgentVision’s overlap requirement categorically. It still does not yet supply a proven keyboard-first, arbitrary overlapping desktop model in the stable release. AgentVision should therefore treat tmux floats as something to test, not as the foundation of its window manager.

### 3.3 Detach, persistence, and recovery

Detaching a client leaves the server, PTYs, child processes, current screen, and scrollback alive. Reattachment is a live continuation. If the server dies or the host reboots, tmux does not reconstruct arbitrary live processes. External tools can save layouts and commands for later recreation, but that is not process resurrection.

The lifecycle boundary is therefore:

```text
client exit / SSH disconnect     → live session continues
tmux server exit / host restart  → live process state is gone
```

This makes tmux suitable for a **standalone detach/reattach substrate
experiment**. It is not a requirement of AgentVision’s current initial proof:
the approved Go-core boundary remains frontend-spawned and connection-scoped,
so frontend shutdown or IPC loss tears down its sessions and core. Persistent
detach/reattach, reconnect, and daemon operation remain later capabilities unless
separately authorized. The experiment would test whether tmux can provide those
capabilities without requiring process checkpoint/restore.

### 3.4 Multi-client and geometry semantics

Multiple clients can view the same session. Because one tmux window has one effective pane geometry, clients of different sizes cannot each have a fully independent layout for that window. tmux exposes `window-size` policies including `largest`, `smallest`, `latest`, and `manual`; other clients may see padding or cropping depending on policy. [Advanced Use](https://github.com/tmux/tmux/wiki/Advanced-Use) describes this size arbitration.

This is not a cosmetic detail. PTY size changes can change application behavior, line wrapping, alternate-screen layout, and terminal output. AgentVision must decide whether:

- one client is the size authority;
- the largest/recent client is authoritative;
- each view gets a distinct process/pane;
- or a provider-specific policy is surfaced honestly.

tmux supports read-only attached clients. Read-only is useful for observation and accidental-input prevention, but it is not a host security boundary.

### 3.5 Control mode: why tmux is unusually reusable

tmux [control mode](https://github.com/tmux/tmux/wiki/Control-Mode) is a documented text protocol designed so iTerm2 could act as the frontend. A control client:

- attaches like a normal tmux client;
- sends ordinary tmux commands;
- receives command results framed by `%begin` and `%end` or `%error`;
- receives asynchronous notifications for sessions, windows, panes, layout, focus, exits, and other changes;
- receives per-pane `%output` containing escaped bytes emitted by the application.

The output may contain arbitrary bytes and terminal escape sequences and is not guaranteed to be UTF-8. That is an advantage for a foreign frontend that wants to own terminal emulation: the frontend gets the child-visible VT stream rather than only a pre-rendered text snapshot.

There are important catches:

1. **The live stream is not the whole truth.** A late client needs `capture-pane` to obtain current screen/history, then must join that snapshot to subsequent output without gaps or duplication.
2. **Backpressure can create a recovery boundary.** Control mode can pause output to a lagging client. After pause, the client is responsible for resynchronizing its view, normally with a capture.
3. **tmux-generated modes are separate.** Copy and chooser UIs generated by tmux are not ordinary child output. AgentVision must either represent them through tmux’s client behavior, replace them with native UI, or avoid exposing them.
4. **Stable object IDs matter.** Session, window, and pane IDs should be used instead of user-visible indexes, which can change.
5. **The client can opt into sizing.** `refresh-client -C` tells tmux the control client’s dimensions; without it, the control client does not affect the shared window size.

These are exactly the behaviors a bounded probe should measure.

### 3.6 Automation, extension, platform, and license

tmux has an unusually broad command language, formats, hooks, environment and option state, key tables, scripting, and shell-based plugin ecosystem. The [formats system](https://github.com/tmux/tmux/wiki/Formats) makes server state available to commands and status rendering. There is no native in-process plugin ABI comparable to Zellij’s WASM model; plugins typically compose commands and shell scripts.

The project lists OpenBSD, FreeBSD, NetBSD, Linux, macOS, and Solaris. It does not provide a native Windows/ConPTY server. Its license is [ISC](https://github.com/tmux/tmux/blob/master/COPYING), a permissive license compatible with reuse and distribution.

Latest verified stable release: [3.7c](https://github.com/tmux/tmux/releases/tag/3.7c). A 3.8 release candidate exists, but this survey does not count prerelease behavior as the stable baseline.

### 3.7 AgentVision assessment

**Interpretation:** tmux is the strongest first provider candidate because it exposes the right seam: AgentVision can own its desktop and talk to tmux as an alternative client. The risk is not whether tmux can keep PTYs alive; it can. The risk is whether AgentVision can maintain a faithful, recoverable screen while adapting tmux’s modes, sizing, and lifecycle into a different object model.

**What to reuse:** PTY/process lifetime, session/pane identities, exit observation, history snapshots, detach/attach, remote-through-SSH behavior, and control events.

**What not to inherit blindly:** tmux’s user-facing session/window semantics, key tables, status bar, copy/chooser UX, linked-window aliasing, or stable-3.7 floating layout limitations.

---

## 4. Zellij

### 4.1 Product and architecture

**Verified facts.** Zellij is a Rust terminal workspace with a server-side screen and layout model. Its [architecture document](https://github.com/zellij-org/zellij/blob/main/docs/ARCHITECTURE.md) places pane relationships, resize, and rendering state in the `Screen` subsystem; each terminal pane has a PTY and parsed grid/scrollback. Zellij therefore owns both process lifetime and canonical terminal state.

The principal user hierarchy is session → tab → pane. Panes can be terminals or plugins. Zellij’s own UI components are built with its plugin system, making the extension model more structural than tmux’s script ecosystem.

### 4.2 Workspace features

Zellij supports:

- tiled panes and declarative layouts;
- floating panes with move, resize, pinning, and fullscreen behavior;
- stacked panes for compact groups;
- tabs and pane/tab naming;
- pane frames, title modes, focus-follows-mouse options, and borderless panes;
- session management and a session manager UI;
- mouse and keyboard control;
- Kitty graphics and modern-terminal integration;
- nested-session assistance.

Current release notes matter because Zellij has evolved rapidly. [0.45.0 and 0.45.1](https://github.com/zellij-org/zellij/releases) added or refined nested sessions, stacked-pane UI, Kitty images, shell-aware scrollback, mobile web behavior, native Windows fixes, initial-command session creation, and a protobuf client/server compatibility contract.

### 4.3 Multi-client collaboration and geometry

Zellij has an explicit multiplayer model: clients have independent focus and colored cursors and may work in different panes. Since 0.45, clients viewing different tabs can give those tabs different sizes; clients viewing the same tab still share that tab’s size. This is a more collaborative interaction model than the usual “one active pane per session,” but it still has a single canonical geometry where clients converge on the same tab.

Read-only remote/web access can prevent input and resizing. As with other read-only terminal clients, this is an interaction permission inside the same user’s session, not a substitute for OS isolation.

### 4.4 Live detach versus session resurrection

When a Zellij server remains alive, detach/reattach preserves the original live processes and screen.

Zellij also has [session resurrection](https://zellij.dev/documentation/session-resurrection.html). Roughly once per second it serializes enough state to recreate an exited session. By default it records layout and discovered pane commands; optional settings record viewport and scrollback. On resurrection it starts new processes. Restored commands are normally held for confirmation rather than executed immediately unless configured otherwise.

That distinction should be explicit:

```text
reattach to live server   → same PTY and process
resurrect exited session  → new process from saved description
```

Command discovery can be imperfect because the command visible from outside a shell is not always the logical application state. This is especially relevant to agents whose resumable conversation identity is not equivalent to the current process arguments.

Zellij 0.45 establishes a protobuf client/server compatibility contract intended to prevent future upgrades from orphaning existing sessions. The release itself remains the transition boundary; the release notes say older live sessions are not compatible with the new contract.

### 4.5 Programmatic control

Zellij’s current [programmatic control guide](https://zellij.dev/documentation/programmatic-control.html) is much stronger than older comparisons suggest. External processes can:

- create or attach sessions in the foreground or background;
- list panes and tabs as JSON;
- obtain pane IDs, geometry, focus, command, working directory, and exit status;
- create, move, resize, float, embed, and close panes;
- send keys or paste to a specific pane;
- dump screen contents or layout;
- wait for output conditions;
- subscribe to pane output as NDJSON;
- inspect connected clients;
- sequence control actions through the server.

The decisive semantic difference from tmux control mode is that Zellij’s subscription reports **rendered viewport or scrollback state**, not the original PTY byte stream. `--ansi` can preserve styling in the rendered output, but cursor motion and other input sequences have already been interpreted.

That may be an advantage or a limitation:

- A frontend that wants textual observation and automation avoids reimplementing VT parsing.
- A frontend that wants exact cell-level state may be able to adapt rendered updates.
- A frontend that wants to act as a complete independent emulator cannot reproduce every terminal state transition from rendered text alone.

The public guide does not promise the internal full-client protocol as a stable third-party frontend API. AgentVision should not build on undocumented internal protocol details without deciding it is willing to track Zellij internals.

### 4.6 Plugins and remote access

Zellij provides a [WASM/WASI plugin system](https://zellij.dev/documentation/plugins.html). Plugins are first-class workspace panes: they can render UI, receive events, and mutate the workspace. The current API exposes more than 100 commands and dozens of event types. Plugins request explicit permissions for capabilities such as reading application state, changing layout, opening terminals, running commands, writing input, reading pane contents, intercepting input, accessing the filesystem, controlling the web server, and writing the clipboard. [Plugin permissions](https://zellij.dev/documentation/plugin-api-permissions) document this boundary.

The built-in [web client](https://zellij.dev/documentation/web-client.html) offers authenticated browser access, session creation/attach/resurrection, login-token management, HTTPS, a PWA/mobile UI, and terminal-to-terminal remote attach over HTTPS. Non-loopback service requires TLS. Official guidance still recommends a reverse proxy for exposure to untrusted networks because the built-in server does not provide rate limiting. Authenticated web users are assumed to be trusted as the local account whose terminals they access.

This is a much broader remote story than “SSH to a server and run the multiplexer,” although SSH remains useful and common.

### 4.7 Platform, maturity, and license

Current installation artifacts cover Linux, macOS, and native Windows. Older statements that Zellij is only available on Windows through WSL are stale for the current release line.

Latest stable release: [0.45.1](https://github.com/zellij-org/zellij/releases/tag/v0.45.1). License: [MIT](https://github.com/zellij-org/zellij/blob/main/LICENSE.md).

### 4.8 AgentVision assessment

**Interpretation:** Zellij is the closest of these multiplexers to a modern in-terminal workspace and is arguably the best feature reference. Its automation and web capabilities are reusable immediately through commands. It is a less obvious hidden substrate for a completely different desktop because the supported observation contract is rendered-state-oriented and the complete client protocol is not a public integration contract.

**What to reuse:** layouts and interaction ideas, rendered pane observation, targeted automation, session resurrection semantics, plugin permission concepts, collaborative-client concepts, and remote web attach patterns.

**What not to assume:** that Zellij can be treated as a raw PTY service, that resurrection preserves processes, or that its internal client protocol is a stable SDK.

---

## 5. WezTerm mux

### 5.1 Product and architecture

WezTerm combines a GPU terminal emulator, native GUI, SSH client, and multiplexing layer. Its [multiplexing documentation](https://wezterm.org/multiplexing.html) organizes sessions into **domains**. A domain is a set of mux windows and tabs backed by a particular process/connection context. The default local GUI domain is not the same durability choice as configuring a standalone local or remote mux server.

The [Lua mux module](https://wezterm.org/config/lua/wezterm.mux/index.html) operates on panes, tabs, windows, domains, and workspaces. The [Pane object](https://wezterm.org/config/lua/pane/index.html) owns or represents the PTY/serial endpoint, associated process information, parsed screen, and scrollback. WezTerm therefore has the richest integrated terminal-emulator/mux ownership of the candidates, but it is designed first for its own native client.

### 5.2 UI and workspace features

WezTerm provides:

- native GUI windows;
- tabs and split panes;
- named workspaces;
- pane movement, rotation, zoom, selection, and navigation;
- searchable scrollback and rich terminal rendering;
- hyperlinks, ligatures, color emoji, Kitty/iTerm2 images, and experimental Sixel;
- Lua configuration, events, key tables, and plugins.

Native operating-system windows can overlap, but that is not the same thing as a terminal-cell desktop with overlapping managed PTY windows. The documented mux layout centers on tabs and splits. AgentVision should not count WezTerm GUI windows as satisfying an in-terminal Turbo Vision layout.

### 5.3 Mux domains and remote behavior

WezTerm documents several domain types:

- **local/default domain** — programs associated with the current local GUI or mux instance;
- **Unix domain** — a client connects to a standalone mux over a local Unix socket; this can bridge a Windows GUI to WSL;
- **SSH domain** — the client uses SSH to launch/connect to a compatible WezTerm mux on the remote host;
- **TLS domain** — SSH can bootstrap credentials, after which the client reconnects over a TLS-protected TCP connection;
- **execution domains** — configurable spawning behavior.

TLS-domain clients can reconnect after network interruption and resume the remote terminal session as long as the remote mux and processes remain alive. This is network continuity, not server-death recovery.

Multiple clients are visible through `wezterm cli list-clients`, with per-client focused pane/workspace information. The official documentation does not clearly specify arbitration when differently sized clients view the same pane, so AgentVision should measure it instead of inferring behavior.

### 5.4 Persistence

A properly configured standalone mux server keeps child programs alive after a GUI client disconnects. Ordinary tabs owned only by a GUI process do not magically become a durable background session when the GUI exits.

WezTerm does not document checkpointing arbitrary processes across mux-server death. `mux-startup` events and Lua configuration can recreate workspace structure and spawn programs, but that is re-execution, not preservation of process state.

### 5.5 Control and extension surface

The CLI can list panes/clients, spawn tabs/windows, split panes, activate items, send text, adjust zoom, and capture pane text including scrollback and escaped/styled forms. Lua configuration and event callbacks can directly manipulate mux objects and implement sophisticated behavior within WezTerm.

For AgentVision, the limitation is boundary placement:

- Lua is powerful **inside WezTerm’s process and object model**.
- The CLI is useful for automation and inspection.
- The public documentation does not offer a stable, general, streaming protocol for an unrelated complete frontend.

Using WezTerm’s internal mux codec or Rust crates directly might be technically possible, but it would couple AgentVision to implementation details rather than a supported integration contract. That is a different maintenance proposition from using tmux control mode.

### 5.6 Platform, maturity, and license

WezTerm runs on Linux, macOS, Windows, FreeBSD, and NetBSD. Its latest tagged stable release remains [20240203-110809-5046fc22](https://github.com/wezterm/wezterm/releases), while official nightly builds continue from `main`. The docs call multiplexing young and evolving. The old stable tag is a release-cadence caution; it is not, by itself, evidence that development has stopped.

License: [MIT](https://github.com/wezterm/wezterm/blob/main/LICENSE.md).

### 5.7 AgentVision assessment

**Interpretation:** WezTerm is the best reference for a unified emulator/mux with first-class remote domains and rich native rendering. It is not currently the best-supported foreign-backend contract. An AgentVision integration should begin as a bounded feasibility study through documented CLI/Lua behavior, not an assumption that internal mux libraries are a public SDK.

---

## 6. abduco

### 6.1 Product and architecture

abduco deliberately solves a smaller problem. One named session runs one command in one PTY. A small server survives client detach and forwards PTY bytes over a Unix-domain socket. There is no native pane/window hierarchy, parsed screen, scrollback model, layout manager, status bar, command language, or plugin system.

The author describes abduco as session attach/detach support and commonly pairs it with `dvtm` for tiling. The [project guide](https://www.brain-dump.org/projects/abduco/) and [source](https://github.com/martanne/abduco) make the separation explicit.

### 6.2 Lifecycle and clients

abduco supports:

- named session create/attach/list;
- a configurable detach key;
- live process continuation after detach;
- multiple attached clients;
- a read-only attach mode;
- retention/reporting of the child exit status;
- socket recreation with `SIGUSR1` if the socket file is removed;
- resize authority assigned to the most recently attached writable client.

Writable clients all target the same PTY. Read-only mode prevents accidental input but is explicitly not a security feature.

### 6.3 The consequence of raw relay

abduco forwards the live PTY stream and does not maintain a canonical screen or history. A client that was detached has missed bytes. On reattach, many full-screen programs redraw in response to terminal signals or input, but the session manager itself cannot give the client an authoritative current screen snapshot.

This creates a very different contract from tmux:

| Event | tmux | abduco |
|---|---|---|
| Client attaches late | Server can render/capture current screen | Client receives only new bytes unless application redraws |
| Client lags | Flow control plus screen/history resync is available | No canonical screen for repair |
| Scrollback | Server-maintained | Outer terminal/client responsibility |
| Multiple logical panes | Built in | External program such as dvtm required |

For a visual workbench, a raw relay is only half a substrate. AgentVision would need a terminal emulator and history model on the server or a protocol that distributes canonical snapshots.

### 6.4 Remote, platform, maturity, and license

abduco has no built-in remote transport. The normal remote path is to SSH to the host and attach locally. Its use of `forkpty` and Unix sockets makes it a Unix/POSIX design; no authoritative native Windows support is documented.

Latest release: [0.6](https://github.com/martanne/abduco/releases), published in 2016. That does not make its small code wrong, but it changes the risk profile: fewer contemporary release signals, less coverage of modern terminal behavior, and more integration responsibility for AgentVision.

License: [ISC](https://github.com/martanne/abduco/blob/master/LICENSE).

### 6.5 AgentVision assessment

**Interpretation:** abduco is useful as a conceptual lower bound and possibly as disposable code/reference for Unix PTY ownership. It does not remove enough work to be the default AgentVision substrate. If AgentVision chooses a bespoke server, abduco shows how small the pure lifetime layer can be—but it also demonstrates why redraw, scrollback, recovery, and protocol semantics cannot be postponed indefinitely.

---

## 7. `libghostty-vt` and `libtsm` are not multiplexers

These libraries appear in the same design discussion because they solve terminal-state parsing, but they occupy a different layer.

### `libghostty-vt`

Ghostty’s [repository and C API](https://github.com/ghostty-org/ghostty) describe `libghostty-vt` as VT parsing and terminal-state maintenance extracted from Ghostty. The underlying terminal implementation is substantial, but the public library API is still described as evolving.

It can help turn raw child output into a screen model. It does not, by itself:

- allocate PTYs or supervise children;
- define sessions or panes;
- keep a server alive after detach;
- arbitrate clients and resize;
- provide a remote protocol;
- define a desktop layout.

Herdr is useful evidence that `libghostty-vt` can be embedded in a serious terminal workspace, but adopting it still leaves AgentVision responsible for all lifecycle and protocol layers.

### `libtsm`

[`libtsm`](https://github.com/kmscon/libtsm) is a DEC VT100–VT520 terminal-emulator state machine. It similarly supplies parsing/state machinery rather than rendering, process supervision, or window management. Its core is MIT licensed, with a bundled hash-table implementation carrying LGPL-2.1-or-later terms that must be evaluated if copied or distributed in a particular form.

### Architectural meaning

The choice is not “tmux versus Ghostty.” A more accurate option tree is:

```text
complete server substrate
  ├── tmux
  ├── Zellij
  └── WezTerm mux

minimal process-lifetime substrate + AgentVision screen model
  ├── abduco-like PTY server
  └── libghostty-vt or libtsm for VT state

fully bespoke
  └── AgentVision owns PTY + terminal state + protocol + lifecycle
```

---

## 8. Decision matrix for AgentVision

Scores are judgments, not upstream facts. `5` is strongest for AgentVision’s stated need; `1` is weakest.

| Criterion | Weight | tmux | Zellij | WezTerm mux | abduco |
|---|---:|---:|---:|---:|---:|
| Proven live PTY/session lifecycle | 5 | 5 | 5 | 4 | 4 |
| Supported foreign-frontend boundary | 5 | 5 | 3 | 2 | 2 |
| Access to raw per-pane VT stream | 4 | 5 | 2 | 2 | 5 |
| Recoverable canonical screen/snapshot | 5 | 5 | 5 | 5 | 1 |
| AgentVision can own the complete UI | 5 | 4 | 3 | 2 | 5 |
| Existing spatial/layout capability | 3 | 4 | 5 | 3 | 1 |
| Automation/events/object IDs | 4 | 5 | 5 | 4 | 1 |
| Remote/multi-client maturity | 3 | 4 | 5 | 5 | 2 |
| Native Windows | 2 | 1 | 4 | 5 | 1 |
| Current maintenance/release signal | 3 | 5 | 5 | 3 | 1 |
| Low integration surface area | 4 | 4 | 3 | 2 | 2 |
| **Weighted total / 215** |  | **192** | **173** | **140** | **106** |

The numeric result should not be mistaken for a procurement truth. Its value is showing why tmux ranks first for the **specific provider experiment**: foreign-client support, raw bytes, snapshots, and mature lifecycle receive the largest weights. If AgentVision instead wanted to adopt an existing user-facing workspace largely intact, Zellij would rank much closer or first.

## 9. Recommended validation sequence

This is the **substrate-specific technical sequence**, not a second competing
product-validation roadmap. In the reuse guide’s next-three sequence, E2 is the
bounded form of M1 and E3 is the bounded form of M2. M3 follows only if one of
those provider probes passes; it is not one of the next three experiments.

### Experiment M1 — tmux provider fidelity

Build a disposable adapter, separate from migration work, that exercises:

- create/list/attach/detach;
- stable session/window/pane identities;
- live `%output` parsing;
- capture-plus-stream handoff without gaps;
- control-mode pause and recovery;
- alternate screen, cursor addressing, DEC modes, bracketed paste, mouse mode;
- Unicode, combining marks, emoji, and wide cells;
- pane resize and multi-client size authority;
- child exit and server loss;
- nested copy/chooser behavior;
- terminal restoration after abrupt frontend exit.

**Pass condition:** AgentVision can present a correct independent pane view and recover after detach, lag, and resize without hidden corruption.

### Experiment M2 — Zellij rendered-state adapter

Use only documented CLI and subscription behavior to answer:

- Is the rendered NDJSON stream complete enough for a responsive foreign view?
- Can AgentVision preserve cell styling and cursor semantics without internal protocol access?
- How are full-screen programs, scrollback, images, and rapid resize represented?
- Can pane lifecycle and output subscriptions be joined without races?
- Does the protobuf compatibility contract cover the surfaces AgentVision would use?

**Pass condition:** AgentVision can implement its frontend without depending on undocumented Zellij internals and without losing behavior required by real agent TUIs.

### Experiment M3 — provider-boundary proof

Run the same minimal AgentVision window against:

- the existing direct/reference PTY provider;
- the tmux probe;
- optionally the Zellij probe.

The frontend should consume the same minimal contract: list resources, attach, snapshot, stream updates, send input, resize, and observe exit. Any provider-specific behavior should appear as declared capability flags, not leaked assumptions.

**Pass condition:** switching providers does not force a rewrite of window management or durable work objects.

## 10. Bottom line

- **Probe tmux first** because it exposes the most suitable documented foreign-client seam.
- **Probe Zellij second** because its modern rendered-state and automation surface may eliminate more frontend work if the semantics are sufficient.
- **Treat WezTerm as a design and remote-domain reference** until a supported external frontend boundary is demonstrated.
- **Treat abduco as a minimal component/reference**, not a complete AgentVision architecture.
- **Keep terminal emulation libraries in their proper layer.** They can complement a bespoke or minimal PTY server; they cannot replace one.

The decision remains reversible if AgentVision defines a narrow terminal-provider contract before migration. The most expensive mistake would be allowing one candidate’s session/layout vocabulary to become the product’s permanent object model before its integration behavior is proven.
