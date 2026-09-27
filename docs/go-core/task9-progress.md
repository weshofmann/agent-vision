# PR2 Task 9: terminal transport seam

Task 9 starts from `8c8f58529475f0bbf371414793f80e53ba52d6d1` after the Task 8 gate. The local PTY desktop remains the default.

Scope: owned transport chunks, tagged emulator replies, final flush ordering, immediate silent resize, retained local interaction, and an identity-bound IPC adapter. Split controller presentation changes into a transport patch while retaining local PTY lifecycle behavior. Validate composed dependency source without changing rejected checkouts.

Verification will include behavioral RED/GREEN tests, real libvterm DSR/CPR, endpoint binding and reserves, the forced scrollbar lock schedule, all lifecycle and default desktop tests. Task 10, wire/backend changes, cutover and renderer changes are outside this checkpoint.

Status: the seam, bound adapter, ordered reply/resize segments and composed
source guard are implemented. Behavioral corrections distinguish process input
Closed from read End/Lost and permit explicit cleanup of retained authoritative
Exited bindings after presentation finish. Actual saturated libvterm replies,
forced scrollbar, adapter and targeted lifecycle qualification checks pass.

A prior full suite failed 32/34 at immediate native task counts after exact owned
joins and FD cleanup. The approved Apple test boundary checks owned joins/FDs
immediately and native baseline once at 100 ms, with failure probes; earlier
failures remain retained. Old/new diagnostic counts do not establish causation.
Clean final full-suite qualification and independent Task9 review are pending.
Default desktop remains local; Task10 has not started.
