## Tasks 11–12 continuation after accepted PR2 integration

The operator accepted PR2 in [PR #9 acceptance](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5859566997) and authorized this continuation in [PR #11](https://github.com/weshofmann/agent-vision/pull/11#issuecomment-5859569996). T10-R1 is closed for the bounded implementation and qualified Darwin arm64 environment; native retry efficacy remains unobserved and its kernel cause unproven.

Main integration `9758954a7a6d0ff53848f460c6f7e5428375320b` is a history-preserving merge of accepted PR2 head `9e4c5fb954401be43e3289ae1a4d12d90844338e`; their trees match. The existing accepted preparation `4f8cdb7298de493ad7378ebf2739424affd1fa49` was merged with main without conflicts at `3dfee765461b9aa70d2e49c24848408ea8decb49`.

This continuation executes only approved migration Tasks 11–12: two Go-authoritative terminals, typed presentation and asynchronous close/quit, then required sibling build/install and relocation qualification. Integrated preparation and inherited tests must pass before implementation. The prior preparation record below describes its historical scope and evidence, not current verification. The old local lifecycle source and tests remain intact; Task 13 removal requires later operator acceptance. PR #11 stays Draft and unmerged. Current automation, independent review and the unchecked manual checklist will be reported separately before Task 12 operator review.

---

# Dependent cutover preparation

Status: preparatory Tasks 11–12 only. The desktop still uses its local C++ PTY
factory. These specifications do not qualify a cutover implementation.

Parent: Draft [PR #9](https://github.com/weshofmann/agent-vision/pull/9). Its
original frozen code checkpoint was `4b9b9ce1883a4d1d1d47096a8ae5326ad899b13d`;
the later doc-only evidence checkpoint `7b5cf63f03d02e239695d56289d8274ed17b2bfe`
is included here by ordinary merge commit `888f2fd50f57841c13fe4e87e60ea8c357a9c71e`.
That merge adds `docs/go-core/migration-acceptance.md` and does not change the
tested candidate source inputs. This candidate is based on `codex/go-core-client`,
not main. The [overnight envelope](https://github.com/weshofmann/agent-vision/pull/9#issuecomment-5853204271)
permits dependent preparation while T10-R1 remains open; it does not accept the
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

Preparatory transport regressions are registered as
session_metadata_lifecycle, session_metadata_close-pending-live,
session_metadata_unavailable, session_metadata_loss-before-status, and
session_metadata_loss-after-status. They use only the small synthetic fake_core.py
peer and the accepted CoreConnection/SessionEndpoint seam.
They cover typed status after the output flush barrier, the exited-session
correlated Close lifecycle, unavailable status without an invented exit code,
EOF before status, and EOF after an exact status was already published. The live
Close-pending case now establishes an acknowledged positive input path before
Close, then checks both input origins, duplicate Close, unrelated transport
progress, and final output/status/Closed ordering. It does not exercise the
pending desktop window state machine or modal UI.

The bounded two-session output regression uses the existing `streams` peer mode
through CoreConnection, applies finite read deadlines, cancels only an unread
local endpoint on a missing frame, and performs graceful Shutdown/join before
reporting the failure. It protects each session's exact initial bytes and
sequence without force-killing the fake peer. Against the published candidate
fixture it produced the expected RED because session B's `Bxy` output was
missing; after the fixture correction it passed alongside the existing streams
and output-fragments cases.

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
The package_siblings seam case copies the existing core_process_test into a
temporary path containing spaces and exercises adjacent-sibling lookup from an
unrelated current directory with a runnable PATH impostor, then checks missing
and nonexecutable adjacent siblings and reaps only its direct synthetic child.
It does not exercise CMake install rules, the real Go core, version negotiation,
outer-terminal restoration, or the installed product. Existing
core_connection_failures covers a wrong handshake response; the
preparation batch records only the newly added focused cases.

The corrective source checkpoint `44fffd4e685a8fdab08085529932627adfabbd43`
was tested with the reviewed working-tree input set on top of
`c501324c0b9af566cff2233013c66b2e9bc5e2a8`; the source manifest SHA256 is
`2e55b940b187df1246f7d2b68a49b5af31d04268faec5aa42d6df219be4a921a`. The later
doc-only parent merge and this documentation update preserve those tested C++,
fixture, and CMake inputs byte-for-byte.
One approved bounded batch built
four standalone C++14 binaries from the three Core client implementation
translation units, then passed the two-session output regression, existing
streams and output-fragments cases, all five named metadata cases, and the
`package_siblings` wrapper's single `sibling-check` case. All 13 commands exited
zero; the script's wall time was 8.119 seconds and summed subprocess time was
8.081 seconds against its 60-second aggregate wall limit. It used one durable
synthetic fixture copy (SHA256
`6d1e8efe1ec2e9aa28fc6a6edd64bee590fa4b2b1969ee480d535fc1c65d541d`, mode 0700).
The R1 log records both readers collected and a graceful owned-core join with no
signal. The live Close log records all three bounded reads collected and a
graceful join with no signal; it observed ACK sequence 1, final tail and End
sequence 2, then passed the correlated Closed/status assertions. The packaging
wrapper reported sibling resolution through a relocated path with spaces and
a PATH impostor.

The exact command array, per-command results, raw logs, corrected binaries, and
source input hashes are retained in the ignored local evidence directory
`.probe/pr11-remediation/green/`; `run-summary.json` has SHA256
`48f4c02d5107801defccc758d62e7b9641410e1c44b4efd3d19878f51225b2a5`, and the
artifact checksum index is `evidence-index.sha256`. The earlier batch's
review-found fixture regression and its RED evidence remain under the same
evidence root. The sibling wrapper's temporary directory was removed after its
subprocess returned. No CMake configure, Go build, PTY/native UI test, install
test, or broad suite ran. These results qualify only the listed synthetic seams;
they do not satisfy PR2 default qualification or close the Task 10 gate.

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
