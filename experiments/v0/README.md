# Throwaway V0 probes

These scripts exist only to reproduce feasibility evidence. They are not an
application scaffold or a supported test framework. The upstream app is unchanged.

From the assigned worktree (or another permitted clone of this branch), use a
Python 3 with pip; this host's bundled interpreter is:

```sh
export PROBE_PYTHON=/Users/devel/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3
sh experiments/v0/build_probe.sh
PYTHONPATH="$PWD/.probe/tools" "$PROBE_PYTHON" experiments/v0/interactive_probe.py \
  .probe/build/tvterm .probe/reproduced-interaction
```

Dependencies/tools/builds go to ignored `.probe/`; nothing installs into the
system. Xcode command-line tools and Perl must already be present. CMake is
pinned to 3.31.10 because the bundled libvterm CMake file still declares 3.5;
CMake 4's older-policy handling was not tested. The scripts do not install Ninja
or pkg-config. Screen decoder pins: pyte 0.8.2, wcwidth 0.9.1.

The driver runs ~45 seconds, using a 120×40 outer PTY. It sends real menu and
shell input, captures decoded text snapshots plus raw ANSI output, and measures
restored attributes in an outer supervisor. Fixed short pacing makes this a
bounded host probe, not a portable timing-independent regression suite.

Manual check (not claimed as performed):

```sh
env -i PATH=/usr/bin:/bin:/usr/sbin:/sbin SHELL=/bin/sh TERM=xterm-256color \
  LANG=en_US.UTF-8 HOME="$PWD/.probe" ENV=/dev/null .probe/build/tvterm
```

Ctrl-B then N opens a second shell; Ctrl-B then Tab changes focus; Ctrl-B then R
enters move/resize, arrows move, Shift-arrows resize, Enter accepts, Esc cancels.
Use synthetic `label=A/B`, `echo "$label $$"`, `tty`, and `stty size`. Exit each
shell, press a key to close its disconnected window, then Ctrl-B → Q to quit.
Do not run probes with production secrets, personal shell rc files or transcripts.
