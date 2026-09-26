# Go Core Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Execution is not authorized by this document; operator plan approval and an explicit implementation assignment are required first.

**Goal:** Replace local C++ shell/PTY ownership with a frontend-spawned Go core while preserving the accepted two-terminal desktop.

**Architecture:** Go owns session state, PTYs, direct children and framed IPC; C++ retains Turbo Vision/libvterm presentation through a narrow transport seam. Three sequential PRs introduce an independently tested core, an opt-in C++ adapter, then the two-terminal cutover. Darwin's serialized reaper is confined behind a lifecycle interface.

**Tech Stack:** Go 1.27.0 qualified toolchain, creack/pty v1.1.24, C++14, CMake 3.31.10, existing pinned tvterm/tvision/libvterm.

**Spec:** [Approved architecture](../specs/2026-09-26-go-core-architecture-design.md) plus [operator decisions D1–D8/G1](https://github.com/weshofmann/agent-vision/pull/5#issuecomment-5845488710).

## Global constraints

- Planning only on PR #5. All checkboxes are future work; operator plan approval and a separate implementation assignment are required.
- Qualified target: macOS 26.6.2 arm64, Go 1.27.0, Apple Clang 21, C++14, CMake 3.31.10. Reject unsupported core platforms explicitly. Darwin reaping is platform-confined.
- Pin creack/pty v1.1.24/checksums and accepted tvterm 210eb23564da06c358d2623388939bb02f7f3419, tvision 640263136daa67b96a90c9bf6eb8816216ff76a3, libvterm 62b27d1db0c49eed55936a1bfa35102be91afe42. Preserve complete notices/source guards.
- Frontend-spawned agentvision-core --ipc-fd=3 --mode=frontend-spawned; no PATH lookup/shell evaluation, daemon, listener, reconnect, persistence, remote transport or orchestration.
- Wire: AVCP, 32-byte big-endian header, body maximum 65,536; version 0 handshake/1 negotiated; flags/reserved zero. Exact message schemas/error numbers are spec §5, never native structs/wait bitfields.
- 16 sessions including Starting; 48 ordinary +16 Credit +16 Close +1 Shutdown slots. Exactly 256 KiB output window/session, 4 MiB aggregate raw output, 128 KiB control reserve, 64 KiB input/session, chunks/input ≤32,768 bytes. Bound allocation/metadata overhead separately.
- Created precedes public events; all successful/in-flight reads admitted before seal precede Exited. No output after Exited/Closed; one terminal response per accepted request on usable connection. Delivery failure is contact loss, never fabricated exit.
- Initial named implementation policy: 64 KiB/100 ms drain; 2 s handshake/first-byte partial-frame/write-no-progress/credit-no-progress/backend cleanup/frontend shutdown; 1 s input deadline; 100 ms credit flush/worker join target; 250 ms HUP/TERM grace; 1 s frontend post-KILL reap observation. D5 permits evidence-based timing/drain tuning without protocol bump when observable bounded-drain/error/seal semantics remain compatible. Schema/resource limits remain v1 limits.
- Shell: absolute regular executable SHELL or /bin/sh; no login flag; intended inherited cwd/env, SHELL override, TERM=xterm-256color, COLORTERM=truecolor. No raw environment over IPC. Preserve accepted explicit V0 slave termios.
- Darwin: os.StartProcess with slave *os.File streams, positive immutable ownedPID before checked Release, sole Wait4/signalling owner. No exec.Cmd/copiers/concurrent Process.Wait/SIGCHLD reaper; no signal after reap or uncertain ECHILD.
- Keep presentation queue-lock separation/predicates, atomic stop, joinable workers, emulator timeout ownership, flush/dirty/wakeup. No blocking socket call under state/emulator locks. Local scroll/selection remain after exit/loss.
- Public evidence is synthetic fixtures/reviewed summaries only, no raw environment/live PIDs/PTY names/home paths/private captures/credentials. Archive necessary local evidence in approved durable project storage with provenance/hash/readability checks; ignored build/.probe and /private/tmp alone do not satisfy retention.

## Review focus

1. Slow-drip/mid-frame EOF fails boundedly before unbounded allocation; cleanup never waits for Error delivery (Tasks 1, 6, 8).
2. Cancellation during open/spawn adopts returned resources; no Created/child can appear after Shutdown Ack (Tasks 3, 6).
3. Read held across completion/credit exhaustion reaches seal once before Exited (Tasks 5, 6, 9).
4. State-held synchronous scrollbar callback and DSR/CPR during output consumption cannot deadlock or silently lose response (Tasks 9–10).
5. Missing/relocated/mismatched sibling/stopped core/local endpoint cancellation preserves restoration and unaffected sessions where contact remains usable (Tasks 7–8, 11–12).

These are assigned test obligations below, not extra feature scope.

---

## Sequencing and file ownership

One migration plan, three independently reviewable PRs. Future branches/worktrees: codex/go-core at <primary-workspace>/.codex/worktrees/go-core; codex/go-core-client at go-core-client; codex/go-core-cutover at go-core-cutover under that same worktree parent. Every implementation PR targets **main**, created only after its predecessor is accepted and separately authorized to merge. Refresh main, record its actual SHA, then branch. Never assume PR #5 or a preceding implementation head has merged. Do not create those worktrees/branches now.

Every PR requires early coherent checkpoint/Draft PR, task checkpoints, exact-SHA independent review, operator acceptance and separate ready/merge authorization. No automatic next milestone.

| PR / paths (new unless marked modify) | Responsibility |
| --- | --- |
| 1: core/go.mod, go.sum; core/internal/policy/policy.go | Pinned module and named policy/resource constants |
| 1: core/internal/protocol/{frame.go,messages.go,codec.go}, matching *_test.go; protocol/v1.md; tests/protocol/v1/*.hex | Typed wire contract/language-neutral literal golden corpus |
| 1: core/internal/session/{types.go,manager.go,start.go,commands.go,output.go,credit.go,shutdown.go}, matching *_test.go | Reservation/resource ownership, command order, output ledger/seal, shutdown |
| 1: core/internal/session/{process.go,process_darwin.go,termios_darwin.go,poller_darwin.go,platform_unsupported.go}, matching *_test.go | Narrow native lifecycle, PTY readiness and platform rejection |
| 1: core/internal/server/{connection.go,scheduler.go}, matching *_test.go; core/cmd/agentvision-core/{main.go,main_test.go}; tests/core_client.py | Inherited stream, sole decoder/writer, independent synthetic client |
| 1: cmake/GoCore.cmake; modify CMakeLists.txt, README.md, THIRD_PARTY_NOTICES.md; third_party/notices/creack-pty.LICENSE | Explicit Go build/notice integration; default desktop stays local |
| 2: src/{core_protocol.h,core_protocol.cpp,core_process.h,core_process.cpp,core_connection.h,core_connection.cpp,ipc_session.h,ipc_session.cpp}; tests/{core_protocol.cpp,core_process.cpp,core_connection.cpp,ipc_session.cpp,fake_core.py} | C++ codec, core-child owner, connection, bounded session endpoint |
| 2: patches/tvterm-transport.patch; modify patches/tvterm-lifecycle.patch, patches/README.md, cmake/apply_patch.py, tests/test_source_guard.py, tests/scrollback_lock.cpp, CMakeLists.txt; tests/terminal_transport.cpp | Controller transport seam, local compatibility, retained presentation synchronization |
| 2: tests/ipc_terminal.cpp; docs/go-core/migration-acceptance.md | Opt-in real-core rendering harness and coverage/gate matrix |
| 3: modify src/{app.h,app.cpp,window.h,window.cpp,main.cpp}, CMakeLists.txt, cmake/GoCore.cmake, README.md, THIRD_PARTY_NOTICES.md, tests/desktop_pty.py; tests/{session_metadata.cpp,package_siblings.py} | Default two-terminal cutover, typed state, sibling install/relocation, acceptance |
| 3: remove lifecycle.patch/tests/lifecycle.cpp after evidence gate; modify transport patch/README/source-guard/scrollbar tests/acceptance report | Remove obsolete local ownership while retaining presentation fixes |

Paths represent future deliverables. Tests below define named Go tests or named cases in standalone C++ CTest executables. They use existing C++14 conventions and package-private test barriers/syscall injection, never exported product scheduling hooks.

### Producer/consumer interfaces

Go protocol:

- SessionID/RequestID are uint64; Header has Version/Type/Flags/Reserved uint16, Length uint32, Request RequestID, Session SessionID. Frame has Header and owned Body []byte.
- ReadFrame(io.Reader) (Frame,error); WriteFrame(io.Writer,Frame) error; Decode(Frame) (Message,error); Encode(Message,RequestID,SessionID,uint16) (Frame,error). Message has typed payload structs for all 14 spec §5 messages.
- ExitStatus{Kind uint8; Value uint32; CoreDump bool}, DrainReason uint8 live in protocol; session consumes these types to avoid import cycles.

Go lifecycle:

- Process interface: RequestHangup(), RequestKill(), Done() <-chan ProcessResult; ProcessResult{Status protocol.ExitStatus; Err error}. Requests go to the sole owner, never direct kill from manager.
- Spawner.Spawn(context.Context,SpawnConfig) (*Resources,error); SpawnConfig{Rows,Cols uint16; Shell,Cwd string; Env []string}. Resources owns master/slave/process and Rollback(context.Context) error, idempotent. Any returned resources are adopted even when cancellation wins.
- Poller.Wait(ctx context.Context, fd int, writable bool) error. Darwin implementation uses standard-library syscall kqueue plus CLOEXEC/nonblocking wake pipe; cancellation wakes waits, users join before FD close/reuse. No new polling dependency. Unsupported platform returns explicit failure rather than accidental qualification.

Go manager/server:

- NewManager(policy.Policy,Spawner,Sink) *Manager; Admit(protocol.Frame) error; Shutdown(context.Context) error; Abort(error); Done() <-chan struct{}.
- Sink.Enqueue(Event) error; Event{Session protocol.SessionID; Message protocol.Message; Request protocol.RequestID; Output *OutputTicket}. Output is nonnil only for OutputBytes. Admission rejection returns typed protocol error to connection; accepted operations own exactly one response. Task5 defines shared DeliveryLedger/OutputTicket; Task6 connection writer must drive its write-state transitions, not equate Enqueue with sent.
- server.Serve(context.Context,net.Conn,*session.Manager,policy.Policy) error. One decoder/connection admission boundary serializes transitions; ordered per-session Input/Resize worker; decoder-side Credit and Close/Shutdown bypass it.

C++:

- Fixed-width Frame semantic fields with owned std::vector<uint8_t> payload; decodeFrame(const std::vector<uint8_t>&) -> DecodeResult; encodeFrame(const Frame&) -> std::vector<uint8_t>. DecodeResult has success/frame/typed error; parsing does not require exceptions.
- SessionMetadata{SessionId id; SessionState state; ExitStatus status; DrainReason drainReason; uint64_t lastSequence}; no terminal PID/native wait bitfield.
- CoreProcess::launch(const std::string &absolutePath) -> LaunchResult; requestStop(), join(). Result transfers IPC endpoint/owned child or startup error.
- CoreConnection::start(const std::string &absolutePath) -> StartResult; createSession(uint16_t rows,uint16_t cols) -> RequestId; submitInput(SessionId,const std::vector<uint8_t>&,InputOrigin) -> EnqueueResult; submitResize(SessionId,uint16_t,uint16_t) -> EnqueueResult; closeSession(SessionId) -> RequestId; shutdown(), cancelLocal(), join(); pollEvent(ConnectionEvent&) noexcept -> bool (nonblocking UI metadata notifications); endpoint(SessionId) -> std::shared_ptr<SessionEndpoint>; returnCredit(SessionId,uint32_t) -> RequestId. Result owns connection or error; EnqueueResult Queued/Closed/Overflow, overflow explicit failure.
- tvterm::SessionTransport has virtual destructor and enqueueInput(TSpan<const char>,InputOrigin) noexcept -> EnqueueResult; readChunk() noexcept -> TransportChunk; consumed(size_t) noexcept (default no-op, IPC override); resize(TPoint) noexcept; cancelLocal() noexcept. TransportChunk owns vector<char> bytes, sequence and Data/End/Lost kind until consumption.
- TerminalController::createWithTransport(TPoint,TerminalEmulatorFactory&,std::unique_ptr<SessionTransport>) noexcept -> TerminalController* transfers ownership; finishPresentation() noexcept joins local workers outside callbacks and retains state, never closes a Go session. The existing controller-owned buffering ClientDataWriter remains the emulator’s nonowning Writer; destroy emulator before that Writer/transport.
- IpcSessionTransport(std::shared_ptr<CoreConnection>,std::shared_ptr<SessionEndpoint>) owns its bindings; metadata() -> SessionMetadata; requestClose() -> RequestId; consumed(size_t) noexcept overrides the transport hook after emulator releases bytes; flushed(uint64_t) noexcept overrides a new default-no-op transport hook after final state publication. Metadata/Close stays in AgentVision, not tvterm. Local endpoint cancellation never closes shared IPC.

### Go output accounting ownership and linearization (P1)

Task5's session/credit.go owns a synchronized per-session DeliveryLedger shared through opaque OutputTicket references with Task6's sole connection writer. This is an internal accounting interface, not a new protocol field. The sole reader reserves raw-byte credit exactly once at successful read admission and creates a ticket in Queued state; Sink admission alone never records sent bytes.

- DeliveryLedger.ReserveRead(sequence uint64, rawBytes uint32) (*OutputTicket,error): one reservation, already included in session/aggregate output storage.
- OutputTicket.BeginWrite() error: Queued→Writing before the writer's first frame byte; only one active ticket on the sole connection writer.
- OutputTicket.CommitWrite() (*Event,error): after the complete header/body is written, Writing→SentUncredited and atomically resolves any deferred Credit, returning at most one correlated completion for the existing reserved lane.
- OutputTicket.FailWrite(error): partial/failed/cancelled frame makes contact unusable; invalidate ticket/pending delivery bookkeeping and call Manager.Abort through connection failure handling. No credit refund followed by continued stream use or fabricated lifecycle.
- DeliveryLedger.ApplyCredit(request protocol.RequestID, rawBytes uint32) (CreditDisposition,*Event): Applied/Deferred/Rejected. The decoder calls it independently of PTY work; ledger mutex guards counters/ticket state only, never socket I/O, Sink enqueue or cleanup joins.

Accounting separates ReadReserved/Queued/Writing/SentUncredited/Returned; writing or sending never charges credit again. Credit ≤SentUncredited applies now; if valid peer receipt outruns CommitWrite and the amount exceeds sent but fits sent plus the current Writing ticket, hold the one pending Credit request until full-write commit. It consumes the already-reserved per-session Credit lane, never blocks decoder/Close/Shutdown. Bytes only Queued are ineligible: amount beyond sent plus Writing is rejected. Duplicate/overflow handling remains spec STATE/accounting error, not a second pending Credit. Commit atomically marks sent and applies held credit; correlated reply is queued outside the mutex. Partial-write failure aborts contact and resolves no undeliverable success. Abort/Close cleanup does not wait for a ticket, credit or socket delivery; contact failure releases bounded buffers/bookkeeping after local owners cancel/join.

Task5 tests TestQueuedUnsentCreditRejected (sent0/queued bytes only → rejection), TestCreditBeforeWriteCommit (peer consumes complete frame while writer held before commit → Deferred, decoder still admits Close/other-session traffic, commit yields one Ack/one return), and TestPartialWriteAbortLedger (header/body prefix then failure → contact loss, no Ack/Exited success, cleanup independent of pending credit). Ticket/ledger storage is bounded by existing byte budgets plus a separately capped chunk count (128 output chunks/session, including in-flight tickets); stop reads if either bound fills, never lose admitted data. The chunk cap is local policy, not a new advertised window. Task6 consumes these signatures explicitly.

### C++ endpoint, status publication and reply reserve (P2)

Task8 owns SessionEndpoint: readChunk() noexcept -> TransportChunk is the sole controller reader's cancellable wait; consumed(size_t) noexcept coalesces via CoreConnection.returnCredit; metadata() -> SessionMetadata is a synchronized snapshot; flushed(uint64_t) noexcept commits pending final status after actual emulator flush; cancelLocal() noexcept detaches/wakes only this endpoint. CoreConnection demultiplexes directly to bounded A/B queues; pollEvent is for UI notifications, never for competing controller byte readers.

SessionCreated first registers a shared endpoint; its ConnectionEvent carries the SessionId for UI acquisition via endpoint(id), before any output dispatch. Task9 binds that handle and shared connection to IpcSessionTransport. Queued payloads and endpoint stay alive through reader/transport completion; the connection outlives all endpoints and retains Credit correlation until exactly one Ack/Error or contact loss, including after local Close. Late UNKNOWN_SESSION credit is harmless only for an already-closed endpoint. cancelLocal stops this presentation and wakes its reader, never returns credit for unconsumed bytes or silently closes B/shared IPC. Production confirmed Close keeps the endpoint consuming/flushing until correlated Closed, then detaches; early detachment is reserved for contact loss/startup rollback, and cannot publish a fabricated Exited. Connection preserves bounded late-data bookkeeping until Closed/contact loss, without consuming on another controller. Local cancellation alone does not alter authoritative core state; Close/Shutdown/core timeout policy owns cleanup.

Wire Exited is staged behind queued chunks; End carries lastSequence. Controller consumes all preceding chunks, calls consumed after each borrowed span is released, forces final updateState/dirty/wakeup outside queue lock, then calls transport.flushed(lastSequence). Only then does endpoint metadata/pollEvent expose Exited; UI cannot outrun final consumption. Lost is a separate contact state and never creates exact exit status. Local scroll/copy remain available after End/Lost.

Internal InputOrigin is User or EmulatorReply, no wire change. Around each synchronous handleEvent(ClientDataRead), the controller's existing buffering ClientDataWriter uses a scoped EmulatorReply origin; restore User before processing key/mouse/focus or other queued events, even within the same reader iteration. Preserve tagged emission segments instead of concatenating provenance away; flush through SessionTransport.enqueueInput(bytes,origin) in original emission order. Writer::write signature and libvterm bytes stay unchanged; no DSR parsing/guessing or emulator rewrite. Resize/ordinary input use the same ordered wire lane.

Task8 central frontend policy reserves 4 KiB of each 64 KiB input staging budget for replies (60 KiB user admission), and one of the 48 ordinary correlation slots for reply Input (user/Create/Resize admissions at most47; replies may use slot48). Reserve affects admission only, never reorders already accepted bytes. Multiple reply segments may queue within bounded reserve while the one reply slot waits for Ack; release on exact completion. Exhausted reply byte/metadata reserve fails explicitly, without waiting under emulator/state lock. These are local admission constants, not extra wire/control lanes. Tasks8/9 add endpoint A/B-read ownership, pending-credit-after-detach, final-flush-status and saturated-user-DSR/reply-reserve-exhaustion assertions.

If actual pinned Writer/controller signatures need adjustment, update matching producer/consumer interfaces/tests before progressing. A broad rendering/emulator/API rewrite is an operator stop boundary.

## PR1 — independently tested protocol and Go session core

### Task 1: Pinned module, wire codec and golden corpus

**Files:** core/go.mod/go.sum; core/internal/policy/policy.go; core/internal/protocol/{frame.go,messages.go,codec.go} and matching *_test.go; protocol/v1.md; tests/protocol/v1/*.hex.
**Consumes:** spec §5/§10, operator D3/D5. **Produces:** protocol/status/policy interfaces above; corpus reused by C++ Task 7.

- [ ] Write TestGoldenAllMessages, TestMalformedFrames, TestFragmentedFrames, TestRequestRules, FuzzReadFrame, FuzzDecode. Literal .hex corpus covers all types/error codes/exit and drain kinds, arbitrary 00/ff/ESC bytes, maximum payload and big-endian u64 IDs. Invalid flags/reserved/type/direction/UTF-8/NUL/enums/trailing bytes/zero or reused request reject. Oversize rejects before body allocation; EOF at every offset, short/zero-progress reads/writes and coalescing cannot corrupt subsequent frames.
- [ ] Run from core: go test ./internal/protocol -run 'TestGolden|TestMalformed|TestFragmented|TestRequest'; expect failure for missing codec before implementation.
- [ ] Define structs/codecs/document bytes; split structural decoding from state validation. Pin module github.com/weshofmann/agent-vision/core, Go1.27.0, creack/pty v1.1.24. Add policy names DrainBytes, DrainTime, HandshakeTimeout, FrameTimeout, WriteTimeout, CreditTimeout, CleanupTimeout, InputTimeout, CreditFlush, WorkerJoinTarget, SignalGrace, CoreReapTimeout with global initial values; separate v1 resource/schema constants; MaxOutputChunks=128 is a local allocation policy.
- [ ] Run package tests; go test ./internal/protocol -fuzz=FuzzReadFrame -fuzztime=30s, then separate FuzzDecode run. Expect PASS/no panic/unbounded allocation; retain only sanitized minimal new inputs. Task 6 owns deadlines around generic io.Reader.
- [ ] Commit: feat: define bounded Go core protocol.

### Task 2: Darwin PTY/readiness and single-owner lifecycle

**Files:** core/internal/session/{types.go,process.go,process_darwin.go,termios_darwin.go,poller_darwin.go,platform_unsupported.go} and corresponding *_test.go.
**Consumes:** Task 1 status/policy. **Produces:** Process/Resources/SpawnConfig/Poller and DarwinSpawner implementing Spawner.

- [ ] Write TestDarwinPTYAndTermios, TestShellFallback, TestInteractiveJobControl, TestSerializedReaper, TestUncertainWaitStopsSignals, TestPollCancelBeforeFDReuse. Assert accepted V0 termios readback, tty on all std streams, direct shell controlling-session/foreground-pgrp, 31×91 then33×97. Invalid relative/nonregular/nonexecutable SHELL falls back with matching argv/env. Interactive no-login startup; Ctrl-C interrupts foreground job, Ctrl-Z stops it, fg resumes, resize SIGWINCH, normal attached hangup.
- [ ] Run go test ./internal/session -run 'TestDarwin|TestShell|TestInteractive|TestSerialized|TestUncertain|TestPoll'; expect absent implementation failures.
- [ ] Implement pty.Open/Setsize, audited V0 slave policy from accepted pinned pty.cc/termios fixture, os.StartProcess with Files slave/slave/slave and Setsid/Setctty/Ctty0. Roll back FDs on every stage; close parent slave. Single owner captures positive PID before Release; Wait4 WNOHANG/EINTR, owned signals, tagged status/checked Release; injected ECHILD stops all signals and reports uncertainty. Frontend resets inherited SIGCHLD ignore/NOCLDWAIT before spawning core; native core harness does likewise. Qualify Go runtime signal handling without replacing its handlers: no competing child reaper/copiers. Test inherited ignored SIGCHLD produces valid statuses through the normalized startup path. Kqueue/wake pipe/stable captured FD with join-before-close.
- [ ] Run GODEBUG=execwait=2 GOGC=1 go test -race ./internal/session -count=1 -timeout=60s plus -run '^TestSerializedReaper$' -count=10. Each test has watchdog; 50 alternating natural/close cycles per repeat, exit17/signal/core mapping, Wait4(savedPID) second ECHILD, explicit GC, FD/goroutine baselines. Private syscall tests cover EINTR/ECHILD/Release failure; no forced PID-reuse claim.
- [ ] Commit: feat: own Darwin PTYs and direct-child lifecycle.

### Task 3: Starting reservations, registry and rollback

**Files:** core/internal/session/{manager.go,start.go,manager_test.go,start_test.go}.
**Consumes:** Tasks 1–2, Sink/Spawner. **Produces:** Manager APIs, opaque IDs, Starting→Live commit barrier.

- [ ] Write TestStartingShutdownBarriers, TestStartingEOFBarriers, TestStartupFailureRollback, TestReservationLimit, TestOpaqueEpochIDs. Hold before open/after open/after spawn/before Created/after Created: Shutdown/EOF adopts all returned resources. Cancel-wins yields one Error STATE/no unpublished events; Created-wins yields normal lifecycle. No child/Created after Ack; 16 Starting consumes limit; IDs never repeat/encode PID, epoch isolates runs.
- [ ] Run go test ./internal/session -run 'TestStarting|TestStartup|TestReservation|TestOpaque'; expect missing manager failures.
- [ ] Implement serialized admission reservation before work; one owner for every resource, cancellation joins rollback, public output only after Created commit. Independently fail open/termios/size/spawn. Retain Exited records until Close, released IDs UNKNOWN_SESSION.
- [ ] Run same tests -race -count=20 plus native failed-start FD/direct-child baselines; injected barriers now test implementation ownership rather than only conceptual model.
- [ ] Commit: feat: serialize session startup and rollback.

### Task 4: Ordered input/resize and cancellation

**Files:** core/internal/session/{commands.go,commands_test.go}.
**Consumes:** Live registry/Poller/master, policy. **Produces:** per-session ordered commands and exact responses.

- [ ] Write TestCommandAdmissionOrder, TestPartialInputTimeout, TestCloseBypassesBlockedInput, TestTwoSessionCommands. Raw nonreading slave EAGAIN; Resize stays behind Input; EINTR/short writes preserve prefix. First-attempt 1s deadline; Error includes exact written bytes and closes session. Cancel resolves queued commands once; B continues; no automatic input retry.
- [ ] Run go test ./internal/session -run 'TestCommand|TestPartial|TestCloseBypasses|TestTwoSession'; expect missing worker failures.
- [ ] Implement ≤64 KiB input queue plus bounded metadata, single writer with cancellable readiness, no decoder/reaper lock around PTY I/O. Resize pty.Setsize in admission order; error RESIZE does not claim local UI surface is PTY size. Ctrl-C/Z are bytes only. Close/Shutdown wake independent cancellation.
- [ ] Run targeted -race -count=20 plus native backpressure/input/resize; assert worker join before descriptor close and exactly one response per admitted operation on usable contact.
- [ ] Commit: feat: order PTY commands with bounded cancellation.

### Task 5: Output seal, credit ledger and scheduler

**Files:** core/internal/session/{output.go,credit.go,output_test.go,credit_test.go}; core/internal/server/{scheduler.go,scheduler_test.go}.
**Consumes:** lifecycle completion/Task1 messages/Sink. **Produces:** sole-reader sequencer/ledger and fair bounded scheduler.

- [ ] Write TestReadHeldAcrossExitSeal, TestEOFBeforeWait, TestWaitBeforeTail, TestHeldContinuousSlaveDrain, TestCreditLedger, TestCreditProgressWith48Ordinary, TestOutputFairness, TestQueuedUnsentCreditRejected, TestCreditBeforeWriteCommit, TestPartialWriteAbortLedger. Held successful read crosses completion: seq1/2 before Exited(last2), one charge at read admission, no later output. Exact reason for EOF/no-data/byte/time/credit/error/explicit-close; in-flight ≤32 KiB included even crossing cap. Zero credit never discards observed data. 48 ordinary cannot block reserved controls. Assert Queued-only credit rejects; peer-return-before-Commit defers once without decoder blockage; partial frame failure aborts contact and cleanup without claiming delivery.
- [ ] Run go test ./internal/session ./internal/server -run 'TestRead|TestEOF|TestWait|TestHeld|TestCredit|TestOutput|TestQueued|TestPartialWrite'; expect absent sequencer failures.
- [ ] Implement shared DeliveryLedger/OutputTicket signatures above: read reserves once, writer Begin/Commit/Fail controls sent accounting, decoder ApplyCredit defers only current-Writing bytes. Implement reader-owned payload/sequence, seal acknowledged only after all in-flight results enqueue. Limit read to available credit/storage, validate Credit against sent-but-uncredited bytes, not queued bytes; decoder applies independently of PTY worker. Double/overflow reject; Draining/Closing/Exited credit works while registered, late released ID UNKNOWN_SESSION. Round-robin output/control reserve preserves Created/output/Exited/Closed FIFO despite reply priority.
- [ ] Run race/barrier/fake-clock/native held-slave tests; verify 256 KiB/session,4 MiB total,128 KiB control plus bounded metadata at16 sessions. B/control progresses with A slow until actual socket stall; 2s no credit/write progress fails contact and cleans rather than fabricating Exited.
- [ ] Commit: feat: seal output and bound credit scheduling.

### Task 6: Inherited stream server and cleanup

**Files:** core/internal/server/{connection.go,connection_test.go}; core/internal/session/{shutdown.go,shutdown_test.go}; core/cmd/agentvision-core/{main.go,main_test.go}; tests/core_client.py; cmake/GoCore.cmake; modify CMakeLists.txt, README.md, THIRD_PARTY_NOTICES.md; third_party/notices/creack-pty.LICENSE.
**Consumes:** Tasks1–5. **Produces:** server.Serve signature, inherited-FD CLI and synthetic client.

- [ ] Write TestHandshakeState, TestSlowDripFrameDeadline, Test81Lanes, TestMalformedDisconnectCleanup, TestShutdownJoinsStarts, TestIPCDisappears, TestCleanupUncertainNoAck. No pre-Hello session; incompatible/duplicate Hello fails; monotonically increasing/wrap rules. First-byte deadline not reset by drips; idle established stream valid. All81 lanes bounded; duplicate pending Credit/Close STATE, bounded LIMIT Errors. Ack only after starts/workers/masters/direct children; uncertain ECHILD/stuck start retains ownership, no success.
- [ ] Run go test ./internal/server ./internal/session ./cmd/agentvision-core; expect missing server/CLI failures.
- [ ] Validate FD/mode/connected Unix stream; net.FileConn then close original duplicate, prevent IPC/master/wake FD inheritance. Sole decoder/writer/deadlines; writer calls ticket BeginWrite before first byte and CommitWrite only after full WriteFrame, enqueues returned Credit completion outside ledger lock; failed write calls FailWrite and Abort. Cleanup independent of socket/ticket/credit completion. Concurrent session cleanup under one2s backend watchdog: cancel/join before master close, Wait/HUP250ms/KILL via owner. Sanitized stderr separate from IPC. Main exit0 only owned orderly cleanup; nonzero protocol/start/cleanup error, never exits claiming uncertain resources clean. No arbitrary exec/listener options.
- [ ] Run core go test -race ./... -timeout=120s and native endpoint EOF/blocked socket/PTY/16 starts. Build then from repo root python3 tests/core_client.py build-core/agentvision-core: owns socketpair/core, golden/input/resize/raw/tail/exit7/SIGTERM/Close/Shutdown, real FD/worker/direct-child baselines and exec FD inventory. Only reviewed synthetic summaries retained.
- [ ] Add cmake/GoCore.cmake and CMakeLists integration: AGENTVISION_BUILD_CORE default OFF for PR1; explicit absolute AGENTVISION_GO_EXECUTABLE/version1.27.0 check, unsupported target rejection, readonly modules/-trimpath/build dependency tracking, sibling artifact. Add core tests/client CTest only when enabled; include full creack-pty/Go notices. Test missing/unsupported Go/rebuild/config OFF behavior (core_build_config); default local V0 remains unchanged.
- [ ] Run fresh cmake -S . -B build-core -DAGENTVISION_BUILD_CORE=ON -DAGENTVISION_GO_EXECUTABLE=<qualified-go> -DCMAKE_BUILD_TYPE=Debug; build/ctest plus fuzz30s each and500-cycle stress. V0 source_guard/scrollback/lifecycle/desktop still pass. Document setup/notices in README/THIRD_PARTY_NOTICES.
- [ ] Publish exact SHA/evidence; independent PR1 protocol/lifecycle review, operator acceptance/merge gate. Keep Draft/stop; no PR2 until separately accepted/merged PR1.

## PR2 — C++ connection and opt-in adapter; default remains local

### Task 7: C++ codec and core-child owner

**Files:** src/core_protocol.*, src/core_process.*; tests/core_protocol.cpp, tests/core_process.cpp, tests/fake_core.py; CMakeLists.txt.
**Consumes:** Task1 corpus and Task6 core. **Produces:** C++ types/codec/CoreProcess APIs.

- [ ] Write CTest core_protocol consuming every literal .hex case (encoded byte equality/decoded fields/rejections); core_process cases source FD already3, CLOEXEC, spawn failure, missing/nonexecutable sibling, exec FD inventory, natural reap/stopped-core escalation/no post-reap signal.
- [ ] Run ctest --test-dir build-client -R 'core_protocol|core_process'; expect absent executables/assertions before implementation.
- [ ] Implement socketpair/posix_spawn, duplicate source away from3, close all original/unintended endpoints on each path, SO_NOSIGPIPE. Single corechild owner waitpid/signals/uncertain ownership; never terminal-shell waits. Resolve sibling from executable directory via Darwin executable-path API/canonical validation, not cwd/PATH; internal explicit path only for harness. Shared byte codecs allocate boundedly, never native ABI.
- [ ] Build C++14/warnings; run golden/native fake-core status/ECHILD/FD tests, no competing SIGCHLD reaper.
- [ ] Commit: feat: add C++ framing and core-child ownership.

### Task 8: Shared connection and endpoint cancellation

**Files:** src/core_connection.*; tests/core_connection.cpp; tests/fake_core.py.
**Consumes:** Task7 codec/CoreProcess. **Produces:** CoreConnection/SessionEndpoint/events/metadata/returnCredit and origin-aware submitInput APIs; shared endpoint owns per-session payloads, connection retains correlations.

- [ ] Write cases fragmentation, slow-drip, control-lanes, blocked-writer, cancel-one-session, hello-stall, shutdown-stall, stopped-core, malformed-status, concurrent-endpoint-readers, credit-after-detach, staged-exit-flush, saturated-user-reply, reply-reserve-exhaustion. Assert A/B readers cannot steal data; detach A preserves B and resolves A pending Credit once; final status waits for flushed(lastSequence); 47 user requests/full60KiB user staging still admits DSR through slot48/4KiB reserve and reserve overflow fails. Fake core fragments/coalesces/raw00ff/end sequence, Ack without exit; A endpoint cancellation leaves B usable; full ordinary queue cannot starve Credit/Close/Shutdown. First-byte/idle policy correct, partial-write offset exact.
- [ ] Run ctest --test-dir build-client -R core_connection; expect failure before implementation.
- [ ] One reader/demux and writer, bounded response lanes/owned endpoint queues, FIFO/sequence/lastSequence validation, monotonic requests and typed contact loss. Register Created shared SessionEndpoint before session dispatch; endpoint(id) binds Task9 transport, pollEvent only serves UI metadata. Keep final Exited pending until consumed chunks plus flushed(lastSequence), connection retains detached pending Credit correlation. Implement origin-aware user/reply reservation above, no wire type or ordering changes. Reader never blocks on UI, no dropping to fabricate exit. At most one Credit pending/session, return only consumed bytes, flush≤100ms quiet; reserve outgoing emulator response capacity; explicit overflow failure. cancelLocal closes/wakes local endpoint only; connection-wide cancel closes IPC/joins waits before core escalation.
- [ ] Run deterministic clock/queue and native stalled/stopped cases:2s stage/TERM250ms/KILL/1s reap observation, local restoration callback independent of reaping, join/FD baselines. Native timing has scheduling allowance; cleanup uncertainty never presented graceful.
- [ ] Commit: feat: multiplex bounded frontend sessions.

### Task 9: Controller transport seam and emulator ownership

**Files:** patches/tvterm-transport.patch; modify lifecycle.patch, patches/README.md, cmake/apply_patch.py, tests/test_source_guard.py, tests/scrollback_lock.cpp, CMakeLists.txt; src/ipc_session.*; tests/terminal_transport.cpp, tests/ipc_session.cpp.
**Consumes:** Task8 endpoints and existing concrete controller/renderer. **Produces:** SessionTransport/createWithTransport/finishPresentation/IpcSessionTransport APIs.

- [ ] Write owned-payload, dsr-cpr, silent-resize, exit-flush, local-after-exit, response-overflow, writer-lifetime cases. Overwrite decoder storage before consumption, output remains intact. Real libvterm ESC[5n/ESC[6n emits exact InputBytes response; 47 outstanding user operations/full60KiB user staging still leaves4KiB/slot48 for reply; full reply reserve fails explicitly without blocking rendering. A/B endpoint read ownership and pending-credit-after-detach remain Task8 tests. Silent resize publishes new surface immediately, Exited visible after final consume/flush. Scroll/selection after exit/loss works, process input suppressed.
- [ ] Run new cases before seam; expect missing seam/old resize failures; existing real scrollback_lock stays green.
- [ ] Replace controller-owned PtyMaster with owned SessionTransport. Local factory wraps current PtyMaster for PR2 default compatibility. Split patches atomically against pristine pinned HEAD: transport patch owns all controller/presentation changes, remaining lifecycle patch only local PTY hunks; no duplicate termctrl modifications. Extend source guard for composed effective diff, pristine/already-applied/conflicting/staged/partial composition/gitlinks/hidden Git flags; rejection never edits dependencies.
- [ ] Implement bounded IpcSessionTransport enqueueInput/readChunk/consumed/flushed; scope ClientDataWriter provenance around ClientDataRead and preserve tagged segments through flush; borrowed ClientDataRead lifetime ends before owned chunk release/credit. queueMutex isolated from state/emulator, no IPC blocking under either. End/Lost narrows rejection to process input, local events continue. finishPresentation joins outside callbacks; destroy emulator before Writer; Close handled asynchronously at app layer, never inferred from read End.
- [ ] Run ctest --test-dir build-client -R 'source_guard|scrollback_lock|terminal_transport|ipc_session' plus all existing lifecycle/default desktop tests. Force actual state-held scrollbar callback versus publication, DSR during consumption. Stop for operator redesign if seam requires cloning renderer/emulator, broad view/window rewrite or broader lock redesign.
- [ ] Commit: feat: introduce owned terminal transport seam.

### Task 10: Opt-in real-core presentation and PR2 gate

**Files:** tests/ipc_terminal.cpp; modify CMakeLists.txt, patches/README.md; docs/go-core/migration-acceptance.md.
**Consumes:** Tasks7–9 and accepted PR1. **Produces:** harness using actual view/window/emulator; default app remains local.

- [ ] Write ipc_terminal two-session/DSR/input/resize/exact-exit-retention/closeA-Bcontinues/core-loss-local-selection tests through production seam and rendering locks.
- [ ] Run expecting missing integration failures, then add opt-in CTest when core enabled. No default backend toggle/menu/dynamic-session UI.
- [ ] Run complete C++/Go race/default/native desktop tests; map each old lifecycle/presentation regression to its Go/adapter replacement in acceptance report. Default desktop must remain unchanged.
- [ ] Publish exact SHA; independent adapter/patch/lock review; operator PR2 acceptance, keep Draft/stop. PR3 only after accepted PR2 merges. Keep local PTY path/tests until cutover equivalence gate.

## PR3 — default two-terminal cutover, then removal

### Task 11: Desktop authoritative metadata and asynchronous close

**Files:** modify src/app.h/app.cpp/window.h/window.cpp/main.cpp, CMakeLists.txt; tests/session_metadata.cpp; tests/desktop_pty.py.
**Consumes:** accepted PR1–2 APIs. **Produces:** two-window desktop with Go sessions and typed lifecycle UI.

- [ ] Write session_metadata Live/Exited/Closed/Lost/unavailable cases, no PID/raw-wait caption or EOF-derived exit. Cancel close/quit sends nothing; confirmed Close suppresses input, waits asynchronously for correlated Closed before destroy. Exited retains scroll/selection. Desktop harness expects corechild plus2 shells owned by Go.
- [ ] Run assertions before cutover; expect local-child/raw-wait behavior failure.
- [ ] App starts single connection/Hello before two Creates, inserts only after Created. Failure after first Create cleans first session/core, never partial ready success. Connection outlives controllers; UI consumes posted metadata on existing idle/wakeup path. Counts live by authoritative state; typed signal/unavailable/drain-tail/lost display. Quit Shutdown never blocks render locks, joins local workers and restores outer terminal even forced core loss. C++ main owns only corechild SIGCHLD/reaping; Go owns shells.
- [ ] Run close/quit cancel/confirm, core crash before/after Created, resize error, final output/status, input suppression/lost retention and independent routing. Preserve modal movement/selection and source_guard/scrollback checks.
- [ ] Commit: feat: move two-terminal desktop to Go core.

### Task 12: Sibling package and equivalent native acceptance before removal

**Files:** modify cmake/GoCore.cmake, CMakeLists.txt, README.md, THIRD_PARTY_NOTICES.md, tests/desktop_pty.py; tests/package_siblings.py; docs/go-core/migration-acceptance.md.
**Consumes:** cutover. **Produces:** sibling build/install/relocation and reviewed acceptance.

- [ ] Write package_siblings unrelated cwd/PATH, relocated directory with spaces, missing/nonexecutable/version-mismatched core, malformed HelloAck/stopped core cases. One diagnostic, no owned resource leak/unintended PATH executable, restored outer terminal. Run before packaging; expect missing install/relocation support failure.
- [ ] Default core ON for migrated desktop; reject core OFF clearly. Install both to bin, full notices under share/agentvision/licenses; version check/readonly modules/-trimpath/source dependency rebuild preserved. No persistent service.
- [ ] Run fresh Debug build/full CTest, Go race/PTY suite, both protocol fuzz30s,500-cycle reaper stress, synthetic desktop and installed/relocated harness. Supplement with actual operator interaction: focus/z-order/Ctrl-B menu, mouse+keyboard move/resize/maximize, A/B routing/stty size, finite scroll then input/UI, Unicode/raw escape/DSR, Ctrl-C/Z/fg, close/quit cancel/confirm, natural exit/status retention, stopped/crashed core shutdown and outer-terminal mode/cursor restoration. Record SHA/commands/prerequisites/results/skips; build/synthetic automation alone does not qualify native UX.
- [ ] Archive necessary local evidence durably with approved project policy/provenance/hash/readability; public acceptance summary synthetic/sanitized, never raw PIDs/env/captures. Storage approval/manual operator checks are future execution dependencies; if unavailable remain Draft and do not remove local lifecycle path.
- [ ] Request independent cutover review and operator acceptance of equivalent evidence before Task13 removal. Failed candidate may be reverted via focused branch commit while PR2 local path remains; no automatic runtime fallback/duplicate shell spawning. Preserve unrelated work/history.

### Task 13: Remove obsolete C++ lifecycle and final acceptance

**Files:** remove patches/tvterm-lifecycle.patch, tests/lifecycle.cpp; modify transport patch/patches README, CMakeLists.txt, source-guard/scrollbar tests, README, acceptance report.
**Consumes:** Task12 operator equivalence gate and coverage matrix. **Produces:** frontend-only controller, presentation patch/tests retained.

- [ ] Write source/target checks proving app/controller no forkpty/createPty/PtyMaster shell wait/ioctl/signal/raw shell status. Map former lifecycle/status/fallback/controller/cycles/signal/resize/foreground/blocked/held-slave tests to Go/adapter equivalents. Run expecting compatibility wrapper/remnant failures.
- [ ] Remove only obsolete local PTY wrapper/factory/accessors/lifecycle hunks/tests. Keep queueMutex/predicates/atomic/join/flush/wakeup presentation hunks in transport patch. Pinned upstream unused PTY files may remain as source, but no linked AgentVision execution/ownership path depends on them; narrow source-list exclusion if necessary, no renderer/emulator fork. Guard validates final sole patch against pristine pins; retain historical evidence.
- [ ] Fresh build/new dependency tree, complete Go/fuzz/stress/C++/guard/scrollbar/package/synthetic desktop and affected native checks at final SHA. Never extrapolate prior acceptance to modified executable.
- [ ] Publish final exact-SHA independent/operator acceptance request and rollback summary; keep Draft/no merge/ready without authorization. Revert focused removal commit if equivalence breaks and seek revised review. Dynamic terminal milestone requires separate design/task, never automatic continuation.

## Coverage, rollback and stop boundaries

| Requirement | Owning tasks / gate |
| --- | --- |
| Spec1–4 topology/ownership; D1/D2 | 6–8,11–12; no default cutover PR1/2 |
| Spec5 framing/correlation/errors | 1,6–8; shared golden/fuzz/native fragmentation |
| Spec6 startup/state | 3,6,8,11; no Ack before cleanup/starts |
| Spec7 PTY/reaper; D6 | 2–4,6; Darwin confined, uncertain wait never success |
| Spec8 seal/drain; D4/D5 | 5–6,9,11; observed output before status, tunable named policy |
| Spec9 presentation/patch; D7 | 9–13; removal after Task12 equivalent evidence/operator gate |
| Spec10 credit/concurrency | 4–6,8–10; no ordinary/UI/PTY starvation of controls |
| Spec11 loss/restoration | 6–8,11–13; forced core death no descendant-cleanup claim |
| Spec12/17 package/notices | 6,12; sibling relocation/license/module pins |
| Spec13/14 testing/decomposition; D8 | each task; sequential main bases and review boundaries |
| Spec15/16 security/deferred scope | global constraints, no sandbox/replay/persistence claims |
| Spec18 risk | seal/start/lock barriers and native acceptance; no PID reuse/platform extrapolation |

Task checkpoints/independent approvals never authorize merge or next PR. Protocol semantic changes require revised spec/corpus/operator review. D5 numerical tuning needs focused evidence/tests/docs; timing observations are not realtime guarantees. Stop for broad tvterm rewrite, new runtime/poll dependency, platform expansion, weakened ownership/seal or irreproducible acceptance. PR1/2 preserve default V0; failed cutover/remove commits can be reverted narrowly without history rewriting or discarding user work. No runtime fallback or unrelated refactor.

## Planning verification and handoff

Self-review: map every spec section, check matching producer/consumer names/types, five Review Focus tests, actual existing paths, patch composition/removal order, branch/base/gates, public hygiene. All implementation commands above are future acceptance steps, **not results from this documentation PR**. Planning verification is document/link/path/interface consistency, focused diff/hygiene and independent plan review at published exact SHA.

Execution method is not selected. Later assignment may choose native or bounded agents/fresh reviews under repository model policy (ordinary Sol/medium, lifecycle Sol/high, independent concurrency review Astra/high). This task stops after independent plan review/remediation and operator plan-review request on Draft PR5. Architecture approved; plan approval/retained implementation authorization pending.
