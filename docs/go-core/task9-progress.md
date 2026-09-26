# PR2 Task 9: terminal transport seam

Task 9 starts from `8c8f58529475f0bbf371414793f80e53ba52d6d1` after the Task 8 gate. The local PTY desktop remains the default.

Scope: owned transport chunks, tagged emulator replies, final flush ordering, immediate silent resize, retained local interaction, and an identity-bound IPC adapter. Split controller presentation changes into a transport patch while retaining local PTY lifecycle behavior. Validate composed dependency source without changing rejected checkouts.

Verification will include behavioral RED/GREEN tests, real libvterm DSR/CPR, endpoint binding and reserves, the forced scrollbar lock schedule, all lifecycle and default desktop tests. Task 10, wire/backend changes, cutover and renderer changes are outside this checkpoint.

Status: owned seam, matching endpoint identity helpers, IPC adapter and composed
source guard implemented. Source guard 24 cases, seam/adapter and forced scrollbar
checks pass. Real saturated IPC reply qualification and complete lifecycle/default
desktop suite remain pending at this checkpoint. This is not Task 9 completion.
