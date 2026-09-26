# Future direction: execution core and replaceable frontends

This note records a possible direction beyond the first Turbo Vision terminal
desktop. It is a design input, not an implementation plan or acceptance criterion
for the current Go-core work.

## Intended shape

AgentVision could evolve from a useful local terminal desktop into a neutral
runtime for human-supervised processes and agents. A Go core would own execution,
PTYs, process lifecycle, session identity and observable events; presentation
clients would attach through a defined boundary. The C++ Turbo Vision desktop
would be the first client, with native GUI, CLI or other frontends possible later.
The classic overlapping-window experience remains a valuable product interface,
but it need not define the runtime's data model.

Paperclip is relevant prior art for wakeups, adapters, coordination and task
execution. AgentVision's core should expose general mechanisms rather than
encode a company, CEO, manager, employee or department model. A client or
optional policy layer could express a planner/implementer/reviewer workflow using
the same underlying execution and observation primitives.

Potential core concepts include sessions, processes, terminals, jobs, events,
messages and capabilities. These are examples for later design, not a settled
schema. Likewise, whether terminal emulation lives in each frontend (as with
tvterm/libvterm) or in a shared backend screen model remains an experiment to
resolve; Go ownership of PTYs does not itself settle that choice.

## Multi-machine direction

Eventually, several AgentVision nodes could form one logical pool. A session
would have a stable identity, an owning execution node and an explicit lifecycle;
frontends could discover and attach without assuming that the process runs in
the frontend's address space. Placement might use capabilities such as operating
system, architecture, memory or GPU availability. Node loss, disconnected
clients, authorization, data locality and recovery would require separate
design; a remote PTY is not automatically migratable or secure.

Kubernetes could later supply scheduling, workload lifecycle and resource
management for some execution locations. It should be evaluated as an optional
provider beneath AgentVision's own session interface, alongside local and
remote-node execution, rather than made a requirement of a local desktop. Mesos
offers useful historical precedent for a shared execution pool, but selecting a
cluster platform is deferred.

## Sequence and current boundary

1. Make the local desktop genuinely useful with multiple interactive shells.
2. Establish a clean Go process/PTY boundary for the Turbo Vision client and
   verify behavior with real PTYs. The separate Go-core design work owns the
   details of this step.
3. If justified by use, add stable session state and reconnect semantics,
   followed by multiple attached clients.
4. Prove a second node, then investigate placement, capabilities, failure
   handling and trust boundaries.
5. Evaluate optional agent adapters and orchestration policies as clients of
   proven primitives; evaluate Kubernetes only when multi-machine workloads
   create a concrete need.

The useful constraint today is to avoid making frontend process locality part
of a session's identity or public contract. That does **not** call for building
networking, persistence, clustering, orchestration or provider integrations in
the present slice. All steps after the current local work need their own design
and evidence before implementation.
