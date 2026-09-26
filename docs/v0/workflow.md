# Work/session provenance

- Primary repository: `/Users/devel/workspace/agent-vision`.
- Assigned branch: `codex/v0-feasibility`.
- Assigned worktree: `/Users/devel/workspace/agent-vision/.codex/worktrees/v0-feasibility`.
- Primary initially had an unborn main and no checked-out tracked files. Read
  current AGENTS.md using GitHub before fetching; read it again in the worktree.
- Kept primary tracked files untouched. Fetched Git metadata and created only the
  assigned worktree. No nested worktrees, substitute paths or retained product code.
- Native worktree tool could not promise the mandated location, so used
  `git worktree add` at the explicit path.
- Placement caveat: primary had no physical `.gitignore`. Verified its committed
  `origin/main:.gitignore` contains `/.codex/worktrees/`; exported that committed
  file to disposable `/private/tmp/agent-vision-worktree-ignore` and checked the
  destination from primary using temporary `core.excludesFile`. This checked the
  tracked rule without editing/bootstrap of primary. The assigned worktree's
  physical root `.gitignore` contains the rule. This is not a claim that the
  unborn primary's absent physical `.gitignore` was bootstrapped.
- Sandbox protects `.git` and `.codex`; required worktree/Git operations used
  approved escalation. No automatic approval rejection or publication blocker.
- Initial intent checkpoint `2d65afb` pushed; Draft PR #2 opened immediately,
  before upstream builds. Coherent research/probe checkpoints were pushed.
- At operator request, merged main `4e619c9d734b356d8fefa9aca4934fb1acc050cd`
  with no conflicts in merge `f76bb0bddaf8079235df86292e78743847fb0979`.
  Reread updated AGENTS.md; changes were workflow-only.
- `~/.profile` does not exist; sourcing was attempted. Base and touched
  directories have no `.kin/config`; no project policy check applies. Kindex
  used for search, lifecycle tags, discoveries, design decision and open watch.
- Model controls: no selector/independent effective model-effort introspection
  for the already running parent worker. Used current session as fallback rather
  than claiming an unobservable switch. Independent review requests GPT-6
  Astra/medium using the advertised selector; tool acceptance and reviewer
  findings will be reported on the PR, without claiming unavailable backend
  model verification.
- Review boundary: proposed design only; Draft PR remains draft. No merge to
  main, retained application or next milestone without operator authorization.

- Independent read-only design review at `964cb8e` posted on PR #2; requested
  Astra/medium selector accepted, effective backend model/effort unavailable.
  Two probe-only P2 findings were addressed in `bb917b3`: conservative child-state
  evidence and finalization on I/O failure. Five focused checks plus a normal
  real-PTY regression passed at that exact SHA. Fresh review remains the boundary.
- After detecting that the unborn primary lacked a physical ignore file, added
  only `/.codex/worktrees/` to primary `.git/info/exclude`; primary default status
  is now clean and default `check-ignore` confirms the assigned path. No primary
  tracked content was edited or checked out.
