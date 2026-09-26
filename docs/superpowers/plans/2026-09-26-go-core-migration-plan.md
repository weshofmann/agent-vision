# Go Core Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Execution is not authorized by this document; operator plan approval and an explicit implementation assignment are required first.

**Goal:** Replace local C++ shell/PTY ownership with a frontend-spawned Go core while preserving the accepted two-terminal desktop.

**Architecture:** Go owns session state, PTYs, direct children and framed IPC; C++ retains Turbo Vision/libvterm presentation through a narrow transport seam. Three sequential PRs introduce an independently tested core, an opt-in C++ adapter, then the two-terminal cutover. Darwin's serialized reaper is confined behind a lifecycle interface.

**Tech Stack:** Go 1.27.0 qualified toolchain, creack/pty v1.1.24, C++14, CMake 3.31.10, existing pinned tvterm/tvision/libvterm.

**Spec:** [Approved architecture](../specs/2026-09-26-go-core-architecture-design.md) plus [operator decisions D1–D8/G1](https://github.com/weshofmann/agent-vision/pull/5#issuecomment-5845488710).

Planning checkpoint: project direction is aligned with G1. Detailed file/interface,
task/test, packaging and review gates are being written before independent review.
No implementation files, build targets or product behavior change in this PR.
