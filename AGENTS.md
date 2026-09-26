# AGENTS.md

Repository-wide guidance for agents working on `weshofmann/agent-vision`.
Follow explicit task instructions and applicable higher-priority instructions. Read
this file and any relevant directory-local guidance before making changes.

## Project direction

Agent Vision is a text-mode desktop with movable, resizable, overlapping terminal
windows, with a possible later evolution into a spatial multi-agent workbench.
Start by proving the terminal desktop is useful; do not build the entire harness.

- Prefer evaluating and reusing the existing open-source Turbo Vision implementation
  and terminal-emulation components before proposing a clone.
- Verify upstream capabilities, licenses, dependency revisions, and actual build
  compatibility. Treat conversational architecture suggestions as hypotheses,
  not verified dependency facts or an approved implementation specification.
- Keep the first scope small. A daemon, persistence across frontend restarts,
  orchestration, MCP, remote access, and provider integrations are later decisions.
- A C++ UI with a later Go backend is a candidate, not a requirement to introduce
  two languages or IPC before the first working desktop.

## Required workspace and worktree placement

`<workspace>` means the original/primary checkout of THIS repository, not the
parent folder containing multiple repositories and not the current linked
worktree. Resolve it to an absolute path before creating a worktree.

**All worktrees MUST be placed under `<workspace>/.codex/worktrees/`.**

Example:

```text
<workspace>/
  AGENTS.md
  .gitignore
  .codex/
    worktrees/
      v0-feasibility/
      terminal-desktop/
```

- Inspect `git worktree list --porcelain` and the current branch/status first.
  Record the absolute workspace, assigned worktree, and branch in your handoff.
- Use a task branch such as `codex/v0-feasibility` with its worktree at
  `<workspace>/.codex/worktrees/v0-feasibility`. Independent writing tasks need
  separate branches/worktrees; do not concurrently edit another worker's files.
- If already in the correctly placed worktree assigned to your task, reuse it.
  Do not create a worktree inside that worktree. Do not mistake the result of
  `git rev-parse --show-toplevel` inside a linked worktree for the primary workspace.
- Do not substitute `.worktrees/`, `worktrees/`, `/tmp`, sibling directories, or
  `~/.codex/worktrees/`. Native worktree tooling must honor the required location.
- The root `.gitignore` must ignore `/.codex/worktrees/`. Verify the destination
  is ignored from the primary checkout before creation. Do not ignore all of
  `.codex/`; reviewed project configuration may belong in version control.
- If permissions or the active harness prevent compliant placement, report the
  blocker. Do not silently fall back to another directory or edit the primary
  checkout instead.
- Keep the primary checkout for integration unless the operator explicitly
  authorizes a direct bootstrap/edit. Do not move, reset, remove, or repurpose
  another task's worktree or branch. Preserve unrelated changes.

## Preferred models and reasoning effort

Favor **GPT-6 Luna, GPT-6 Sol, and GPT-6 Astra**. Choose model and effort deliberately
for each job rather than inheriting an expensive setting for every subtask.
The following are project routing preferences, adjustable to observed task risk
and the levels supported by the installed runtime:

| Job | Preferred model | Starting effort |
| --- | --- | --- |
| File inventory, narrow documentation lookup, mechanical edits | GPT-6 Luna | Low or medium |
| Bounded fixes, focused tests, well-specified exploratory tasks | GPT-6 Luna | Medium; high when tracing edge cases |
| Normal implementation, integration, and test development | GPT-6 Sol | Medium |
| Difficult debugging, PTY/process lifecycle, terminal integration | GPT-6 Sol | High |
| Architecture tradeoffs and independent design/code review | GPT-6 Astra | Low or medium; increase with complexity |
| Security-sensitive review, subtle concurrency, unresolved hard failures | GPT-6 Astra | High; extra-high only when supported and justified |

- Default ordinary implementation to Sol/medium. Use Luna where the task is
  sufficiently bounded; escalate when uncertainty or failure cost warrants it.
  Return to a lighter setting after resolving the difficult part.
- Do not default every task to maximum effort or use older GPT-5.x models merely
  because an inherited example names them. Prefer the GPT-6 family when available.
- These instructions are NOT runtime configuration. Use actual supported model
  selectors, agent configuration, or delegation controls. Check the installed
  runtime's advertised model IDs and effort values; do not invent API fields,
  assume every model supports every effort, or claim a model switch occurred
  merely because a prompt requested it.
- Report requested versus effective model/effort when observable. If selection
  or verification is unavailable, disclose that limitation and the fallback;
  use only an available configuration within existing permissions.
- Delegate only genuinely separable tasks with a concrete outcome. Prefer a
  separate read-only reviewer for meaningful changes. Label self-review honestly;
  it is not independent review. Do not spawn agents just to consume tokens.

## Development and publication workflow

- Inspect the repository and applicable skills before planning. Keep scope,
  assumptions, acceptance criteria, and verification proportional to the task.
  New architecture needs a reviewed design before retained product implementation.
- Favor the smallest runnable vertical slice. Avoid speculative frameworks,
  broad refactors, and implementing future milestones without authorization.
- Add focused tests for changed behavior. For terminal/UI work, supplement tests
  with real interactive checks: focus, input routing, resize, child exit, cleanup,
  and restoration of the outer terminal. Do not equate a build with a working UI.
- Use reproducible dependency references and document setup/build/test commands
  as they become real. Do not fabricate successful commands or platform support.
- Commit and push coherent checkpoints after meaningful milestones and roughly
  every 30-60 minutes of active work when feasible. Clearly label incomplete or
  failing checkpoints; do not represent them as verified. Report push blockers.
- Open a Draft PR early and keep it updated with scope, decisions, commands,
  results, limitations, current head SHA, and the exact tested SHA. Keep PRs
  reviewable; stop and split scope before an unbounded change accumulates.
- Before claiming completion, rerun relevant verification against the final
  changes and inspect the diff/status. Report failures, skipped checks, and
  unavailable environments explicitly. Distinguish tested code from later
  documentation-only commits.
- Do not merge, force-push, delete branches/worktrees, or alter repository
  protections without explicit authorization. Stop at the assigned review boundary.

## Safety and evidence

- Treat terminal windows and PTYs as interfaces, not security sandboxes. Do not
  claim process, filesystem, credential, or provider isolation without implementing
  and verifying the relevant boundary.
- Use synthetic data in demonstrations. Never commit credentials, private agent
  transcripts, personal files, or unredacted environment dumps.
- Do not add paid-service usage, access production credentials, weaken permissions,
  or install persistent host services without explicit authorization.
- Keep evidence concise and reproducible. A final handoff should identify the PR,
  branch, absolute worktree path, head/tested SHAs, observed results, known gaps,
  and the next bounded step.
