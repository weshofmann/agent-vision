# Dependent cutover preparation

Status: preparatory Tasks 11–12 only. The desktop still uses its local C++ PTY
factory. These specifications do not qualify a cutover implementation.

Parent: Draft [PR #9](https://github.com/weshofmann/agent-vision/pull/9), frozen
published SHA `4b9b9ce1883a4d1d1d47096a8ae5326ad899b13d`. This candidate is based
on `codex/go-core-client`, not main. The [overnight envelope](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5853204271)
permits dependent preparation while T10-R1 remains open. It does not accept the
parent. The approved [migration plan](../superpowers/plans/2026-09-26-go-core-migration-plan.md)
and [architecture](../superpowers/specs/2026-09-26-go-core-architecture-design.md)
remain the implementation specification.

## Retained implementation gate

All three prerequisites must be recorded before retained Tasks 11–12 code:

- An independent reviewer recommends evidence-backed T10-R1 technical closure.
- Fresh required OFF and ON qualification passes at the relevant exact published
  code SHA, with original failures retained and manual gaps identified.
- No blocking technical findings remain; reviewer links and tested SHAs are
  recorded on both parent and candidate PRs.

The gate is currently **not satisfied**. Matching master-open diagnostic error
reproduction is evidence, not a correction or qualification waiver. No shipping
selection, lifecycle ownership, wire schema or timing policy changes belong in
this preparatory checkpoint. Task 13 removal requires operator equivalent-evidence
and manual acceptance; it is not authorized by the overnight envelope.

## Task 11 regression specification

| Scenario | Required observable behavior | Preparatory evidence boundary |
| --- | --- | --- |
| Running | Caption/count/input eligibility use typed authoritative state; no shell PID/raw wait caption | Fake core metadata; desktop wiring pending |
| Exit/code | Code and drain metadata appear only after output consumed and emulator flushed through lastSequence | Transport regression; caption/render check pending |
| Signal/unavailable | Preserve typed signal/core-dump/unavailable distinctions; do not fabricate exit | Fake typed status; native signals pending |
| EOF/contact loss | Display loss of authority/contact; preserve output/scroll/selection and any previously published exact status | Fake EOF; native restoration pending |
| Close cancel | No request, input suppression or destruction after cancellation | Desktop modal interaction pending |
| Close confirm | Suppress further input, admit one correlated Close, await Closed asynchronously before destruction | Fake delayed Closed; UI state machine pending |
| Quit cancel | No Shutdown or window teardown | Desktop modal interaction pending |
| Quit confirm | One core Shutdown, bounded local worker stop/restoration, owned direct child completion | Fake core seam; actual UI shutdown pending |
| Partial startup | Failure after first Created cleans that session and core; never two-terminal ready success | Fake second Create rejection; app integration pending |
| Output tail | Last output remains visible before exact exit status and subsequent close; no credit for unconsumed/discarded bytes | Fake output barrier; emulator/native evidence pending |
| Local cancellation | Wakes only its endpoint; shared connection and other session remain usable | Existing PR2 seam plus preparatory coverage |

Tests may exercise the accepted PR2 seams with synthetic peers. Passing those
checks does not prove future captions, modal cancellation, application lifetime,
terminal restoration or controller wiring. Pending cases stay explicit.

## Task 12 sibling packaging specification

| Case | Acceptance requirement |
| --- | --- |
| Unrelated cwd/PATH | Locate only the executable-adjacent core; never execute a PATH impostor |
| Relocated directory with spaces | Frontend and sibling execute without shell evaluation |
| Missing/nonexecutable sibling | One clear startup diagnostic; no duplicate local-shell fallback |
| Wrong version/malformed HelloAck | Reject incompatibility and stop/reap only the direct owned child |
| Stopped core | Bound shutdown; restore presentation before escalation; do not claim shell cleanup from core termination |
| Install | Both binaries in bin and complete notices under share/agentvision/licenses |
| Source rebuild | Preserve exact Go version, readonly module pins, trimpath and source dependencies |

Current sibling installation/default cutover is not implemented. Preparatory
fake-sibling tests must be labeled as seam tests, not installed-product evidence.
No production core or extra native PTY campaign is needed to prepare these cases.

## Operator and native acceptance checklist

- [ ] Parent technical gate satisfied and documented at exact tested SHA.
- [ ] Independent candidate lifecycle/concurrency/packaging review complete.
- [ ] Fresh required automated cutover evidence and installed/relocated harness.
- [ ] Focus/z-order/Ctrl-B menu; keyboard/mouse move, resize and maximize.
- [ ] Independent A/B input routing and stty rows/columns after resize.
- [ ] Finite scrollback and selection remain usable after output, exit and loss.
- [ ] Unicode/raw escape/DSR response byte order and no lock-held blocking IPC.
- [ ] Ctrl-C, Ctrl-Z and fg behavior under Go shell ownership.
- [ ] Close and quit cancel/confirm behavior, natural exit/status retention.
- [ ] Stopped/crashed core cleanup and outer terminal mode/cursor restoration.
- [ ] Operator equivalent-evidence/manual acceptance before any local lifecycle removal.

No manual result is inferred from synthetic tests. Keep the local lifecycle
implementation and its regression coverage throughout preparation and cutover
candidate work. Main and PR #9 keep their existing desktop default. Integrate any
later parent fix by ordinary reviewed merge into this candidate, with affected
evidence invalidated and rechecked; never rebase/force-push an active review target.
