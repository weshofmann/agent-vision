#!/usr/bin/env python3
"""THROWAWAY evidence checks; separates core feasibility from V0 qualification."""
import json
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
def screen(name):
    return (root / f"{name}.txt").read_text()

a = screen("02-shell-a")
b = screen("03-shell-b")
a_pid = re.search(r"SESSION A PID (\d+)", a).group(1)
b_pid = re.search(r"SESSION B PID (\d+)", b).group(1)
assert a_pid != b_pid
assert re.search(r"/dev/ttys\d+", a).group() != re.search(r"/dev/ttys\d+", b).group()
assert f"FOCUS B PID {b_pid} SIZE 25 83" in screen("05-b-resized-moved")
assert f"FOCUS A PID {a_pid} SIZE 37 118" in screen("06-focus-a")
assert "FOCUS A SIZE 20 68" in screen("07-a-resized-overlap")
assert "FOCUS B SIZE 25 83" in screen("08-focus-b-overlap")
assert "(Disconnected)" in screen("09-b-exit")
assert "SURVIVOR A" in screen("10-a-survives")
assert "(Disconnected)" in screen("11-a-exit")
# Check measured positions/occlusion of the real outer display, not just markers.
assert screen("05-b-resized-moved").splitlines()[6].index("╔") == 15
assert screen("07-a-resized-overlap").splitlines()[6].startswith("║")
assert screen("08-focus-b-overlap").splitlines()[6].index("╔") == 15
steps = json.loads((root / "steps.json").read_text())
result = steps[-1]
assert result["app_wait_status"] == result["supervisor_wait_status"] == 0
assert result["alternate_screen_enter"] and result["alternate_screen_leave"]
assert result["outer_input_recovered"]
assert result["outer_termios_restored_after_input"]
processes = next(x["owned_shell_processes_after_b_close"] for x in steps
                 if "owned_shell_processes_after_b_close" in x)
zombies = [line for line in processes if line.split()[0] == b_pid and "Z" in line.split()[2]]
summary = {"core_feasibility_checks": "passed", "shell_a_pid": a_pid, "shell_b_pid": b_pid,
           "closed_b_reaped": not zombies, "owned_shell_metadata": processes,
           "immediate_termios_equal": result["outer_termios_restored"],
           "termios_equal_after_input": result["outer_termios_restored_after_input"],
           "v0_qualification": "failed: closed shell B remains zombie" if zombies else "not fully tested"}
print(json.dumps(summary, indent=2))
# A successful evidence collection is not a qualified V0 application.
sys.exit(1 if zombies else 0)
