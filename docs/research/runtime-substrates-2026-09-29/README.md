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

1. Terminal multiplexer capability survey — architecture, sessions, clients,
   remote access, control surfaces, extension models, maturity, and licensing.
2. Herdr and Paperclip capability survey — product and architecture descriptions
   followed by a direct comparison.
3. AgentVision reuse and leverage guide — reusable ideas, architecture options,
   decision criteria, and bounded validation experiments.
4. Source and license index — primary sources, release baselines, license notes,
   and evidence limitations.

The detailed documents will be added as the next checkpoint on this Draft PR.

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
