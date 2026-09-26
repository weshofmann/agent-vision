# AgentVision first retained terminal desktop slice

Implements the [accepted PR #2 design](https://github.com/weshofmann/agent-vision/pull/2#issuecomment-5844056291).
This initial checkpoint records intent only; no application or lifecycle result
is claimed yet. The implementation PR stays Draft and stops at review.

## Contract and boundaries

- One C++ executable, exactly two initially visible overlapping terminal windows.
- Reuse tvterm `210eb23564da06c358d2623388939bb02f7f3419`, tvision
  `640263136daa67b96a90c9bf6eb8816216ff76a3`, and the magiblot libvterm fork
  `62b27d1db0c49eed55936a1bfa35102be91afe42`; retain upstream notices.
- Preserve Ctrl-B/menu/input routing and cell-based move/resize behavior.
- Own direct-child PID/status, joinable workers, FD closure and reliable reaping.
  Retain exited output/status until explicit close; confirm live close/quit.
- Qualify real independent shells, focus/z-order/geometry, live close/quit,
  direct-child/resource cleanup, one foreground job and normal outer restoration.
- No descendant enumeration or all-descendant guarantee. Detached descendants,
  full-screen/SIGWINCH qualification, outer resize, abnormal signals, Unicode/load
  and wider portability are outside this slice.
- No daemon, persistence, orchestration, providers, MCP, remote access, backend/IPC,
  session manager, emulator/UI replacement or speculative framework.
- No top-level project license is selected. Any core patch is narrow, documented
  relative to the tested upstream and independently reviewable.

## Execution sequence

1. Integrate the exact dependency pins and preserve their notices; establish a
   build/test entry point and focused failing lifecycle regressions.
2. Patch only lifecycle ownership in the existing core: explicit shutdown,
   join/reap/status/FD completion, including natural exit and a foreground job.
3. Add the small two-window application with retained exited output and explicit
   confirmed close/quit. Exercise real PTYs and upstream keyboard behavior.
4. Publish sanitized purpose-built evidence, inspect the diff, and request an
   independent review on the Draft PR with exact head/tested SHAs.

All published commands/evidence use `<workspace>`, `<user>` or `$HOME`; inspect
both generated source and decoded captures before publication. No private
transcripts, host identifiers or environment dumps belong in this repository.

The parent runtime has no effective model/effort selector or introspection;
continue in the available session without claiming a switch. Independent final
review will request advertised GPT-6 Astra at a deliberate effort level. No
project `.kin/config` is present. Initial base is `5e68168`.
