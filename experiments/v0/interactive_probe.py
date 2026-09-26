#!/usr/bin/env python3
"""THROWAWAY V0 evidence driver. Real, unmodified tvterm; no product code.
Requires pyte 0.8.2. Input is delivered to an outer PTY, never a child fd.
"""
import argparse
import errno
import fcntl
import json
import os
from pathlib import Path
import pty
import re
import select
import subprocess
import struct
import termios
import time

import pyte
from probe_cleanup import finalize_probe

parser = argparse.ArgumentParser()
parser.add_argument("binary", type=Path)
parser.add_argument("evidence", type=Path)
args = parser.parse_args()
args.evidence.mkdir(parents=True, exist_ok=True)
master, slave = pty.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 120, 0, 0))
# Only synthetic data: do not pass the user's full environment or shell rc files.
env = {"PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "SHELL": "/bin/sh",
       "TERM": "xterm-256color", "LANG": "en_US.UTF-8", "HOME": str(args.evidence.resolve()),
       "ENV": "/dev/null", "PS1": "probe> "}
# The outer session gets its own controlling terminal.
pid = os.fork()
if pid == 0:
    os.close(master)
    os.setsid()
    fcntl.ioctl(slave, termios.TIOCSCTTY, 0)
    for fd in (0, 1, 2):
        os.dup2(slave, fd)
    if slave > 2:
        os.close(slave)
    # Keep the outer session leader alive while checking restored attributes.
    # macOS revokes this PTY when its session leader exits, making a later
    # tcgetattr from the driver invalid (ENOTTY), even if restoration succeeded.
    before = termios.tcgetattr(0)
    app_pid = os.fork()
    if app_pid == 0:
        os.execve(str(args.binary.resolve()), [str(args.binary.resolve())], env)
    _, app_status = os.waitpid(app_pid, 0)
    after = termios.tcgetattr(0)
    result = {"app_wait_status": app_status,
              "outer_termios_restored": before == after,
              "before_termios": repr(before), "after_termios": repr(after)}
    os.write(1, b"\r\nOUTER_READY\r\n")
    readable, _, _ = select.select([0], [], [], 5)
    recovered = os.read(0, 128) if readable else b""
    result["outer_input_recovered"] = recovered == b"outer-check\n"
    result["outer_input_hex"] = recovered.hex()
    after_input = termios.tcgetattr(0)
    result["after_input_termios"] = repr(after_input)
    result["outer_termios_restored_after_input"] = before == after_input
    (args.evidence / "supervisor.json").write_text(json.dumps(result, indent=2) + "\n")
    os._exit(0 if app_status == 0 else 1)

class ProbeScreen(pyte.Screen):
    def set_margins(self, *args, private=False):
        # CSI ? ... r restores private modes; it is NOT DECSTBM. pyte 0.8.2
        # misdispatches it to set_margins and otherwise raises TypeError.
        if not private:
            super().set_margins(*args)

screen = ProbeScreen(120, 40)
stream = pyte.ByteStream(screen)
raw = bytearray()
steps = []
dsr_queries_handled = 0

def drain(seconds=0.6):
    global dsr_queries_handled
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        ready, _, _ = select.select([master], [], [], min(0.05, max(0, end-time.monotonic())))
        if ready:
            try:
                data = os.read(master, 65536)
            except OSError as exc:
                if exc.errno == errno.EIO:
                    break
                raise
            if not data:
                break
            raw.extend(data)
            stream.feed(data)
            # A real outer terminal answers DSR. pyte only decodes a screen;
            # answer the upstream shutdown query, including split reads.
            queries = raw.count(b"\x1b[6n")
            while dsr_queries_handled < queries:
                os.write(master, f"\x1b[{screen.cursor.y+1};{screen.cursor.x+1}R".encode())
                dsr_queries_handled += 1

def snapshot(name):
    drain()
    text = "\n".join(screen.display) + "\n"
    (args.evidence / f"{name}.txt").write_text(text)
    steps.append({"snapshot": name})
    return text

def send(data):
    os.write(master, data.encode())
    steps.append({"input": data})
    drain(0.15)

def menu(key):
    send("\x02")
    drain(0.25)
    send(key)

def shell(command):
    send(command + "\r")
    drain()

status = None
try:
    snapshot("01-start")
    shell("label=A; PS1='A> '; printf '\\033]0;Shell A\\007'; printf 'SESSION %s PID %s\\n' \"$label\" \"$$\"; tty; stty size")
    snapshot("02-shell-a")
    menu("n")
    shell("label=B; PS1='B> '; printf '\\033]0;Shell B\\007'; printf 'SESSION %s PID %s\\n' \"$label\" \"$$\"; tty; stty size")
    snapshot("03-shell-b")
    # Keyboard drag: shrink B by 35 columns/12 rows, then move right/down.
    menu("r")
    snapshot("04-drag-mode")
    for _ in range(35):
        send("\x1b[1;2D")
    for _ in range(12):
        send("\x1b[1;2A")
    for _ in range(15):
        send("\x1b[C")
    for _ in range(5):
        send("\x1b[B")
    send("\r")
    shell("printf 'FOCUS %s PID %s SIZE ' \"$label\" \"$$\"; stty size")
    snapshot("05-b-resized-moved")
    menu("\t")
    shell("printf 'FOCUS %s PID %s SIZE ' \"$label\" \"$$\"; stty size")
    snapshot("06-focus-a")
    menu("r")
    for _ in range(50):
        send("\x1b[1;2D")
    for _ in range(17):
        send("\x1b[1;2A")
    send("\x1b[C")
    send("\x1b[C")
    send("\x1b[B")
    send("\r")
    shell("printf 'FOCUS %s SIZE ' \"$label\"; stty size")
    snapshot("07-a-resized-overlap")
    menu("\t")
    shell("printf 'FOCUS %s SIZE ' \"$label\"; stty size")
    snapshot("08-focus-b-overlap")
    shell("exit 7")
    snapshot("09-b-exit")
    send(" ")  # upstream closes disconnected window on next key.
    shell("printf 'SURVIVOR %s\\n' \"$label\"")
    snapshot("10-a-survives")
    # Retain only process metadata of the synthetic shell identities we saw.
    shell_pids = set()
    for name in ("02-shell-a", "03-shell-b"):
        text = (args.evidence / f"{name}.txt").read_text()
        shell_pids.update(re.findall(r"SESSION [AB] PID (\d+)", text))
    proc_text = subprocess.check_output(["/bin/ps", "-axo", "pid,ppid,stat"], text=True)
    steps.append({"owned_shell_processes_after_b_close": [line.strip() for line in proc_text.splitlines()
                   if line.split() and line.split()[0] in shell_pids]})
    shell("exit 0")
    snapshot("11-a-exit")
    send(" ")
    menu("q")
    snapshot("12-quit-menu")
    send("outer-check\n")
    drain(2)
    waited, wait_status = os.waitpid(pid, os.WNOHANG)
    if waited == 0:
        raise RuntimeError("tvterm did not quit within bounded wait")
    status = wait_status
    result = json.loads((args.evidence / "supervisor.json").read_text())
    steps.append({**result, "supervisor_wait_status": status,
                  "alternate_screen_enter": b"\x1b[?1049h" in raw,
                  "alternate_screen_leave": b"\x1b[?1049l" in raw})
finally:
    finalize_probe(args.evidence, pid, (master, slave), status, raw, steps)
print(json.dumps(steps[-1], indent=2))
