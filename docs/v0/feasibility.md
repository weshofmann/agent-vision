# V0 feasibility — investigation in progress

Question: can existing Turbo Vision and terminal-emulation components run two
real interactive shells in independently movable, resizable, overlapping
text-mode windows on this development machine?

This initial checkpoint records task intent, not verified feasibility or an
approved product design. Only bounded, explicitly experimental build/run probes
are authorized. No retained application, framework, daemon, MCP, orchestration,
provider integration, remote access, or persistent host services.

## Investigation plan

1. Pin and inspect magiblot/tvision, magiblot/tvterm and actual transitive
   terminal-emulation dependencies; inspect licenses and build requirements.
2. Build upstream on the actual host without persistent system changes.
3. If possible, probe two shells, focus/input routing, move/overlap/resize,
   child PTY geometry, exit handling and outer terminal restoration.
4. Separate observations from source-based inference and untested behavior;
   retain concise synthetic evidence and reproducible commands.
5. Propose minimal V0 design, acceptance criteria, risks and first slice;
   obtain independent read-only review and request operator design review in a
   Draft PR comment. Stop before retained product implementation.

## Workspace/workflow provenance

- Primary: `/Users/devel/workspace/agent-vision` (unborn checkout at task start).
- Assigned: `/Users/devel/workspace/agent-vision/.codex/worktrees/v0-feasibility`.
- Branch: `codex/v0-feasibility`; base: `bf81b45`.
- No primary tracked files populated/edited; only Git metadata fetched.
- Primary had no physical `.gitignore`. Committed `origin/main:.gitignore`
  contains `/.codex/worktrees/`. Exported it to disposable
  `/private/tmp/agent-vision-worktree-ignore` and used temporary
  `core.excludesFile` for `git check-ignore -v` from primary before creation.
  This verifies the committed rule without bootstrapping primary files.
- Base AGENTS.md read remotely before fetch and again in the assigned worktree.
  Explicit assignment requires immediate Draft PR and PR-comment review requests.
- `~/.profile` absent; sourcing attempted. No `.kin/` policy in base. Kindex
  session/search initialized.
- Parent model/effort cannot be selected or independently verified through
  available controls. Independent reviewer will request GPT-6 Astra/medium
  through the advertised spawn selector; report observable effective routing.
- Host: macOS 26.6.2 (25G83), arm64; Xcode Apple clang 21.0.0. CMake, Ninja
  and pkg-config were not on PATH in the initial inventory.
