#!/usr/bin/env python3
"""Apply only the documented downstream patch; never reset an existing tree."""
from pathlib import Path
import subprocess
import sys

source, patch = map(Path, sys.argv[1:])
patch = patch.resolve()
base = ["git", "-C", str(source), "apply"]
def check(reverse=False):
    return subprocess.run(base + (["--reverse"] if reverse else []) + ["--check", str(patch)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
if check():
    subprocess.run(base + [str(patch)], check=True)
elif not check(reverse=True):
    sys.exit("Downstream patch does not match source; use a fresh build directory, never reset user changes")
# Detect unexpected changes rather than silently build a different core.
actual = subprocess.check_output(["git", "-C", str(source), "diff", "--binary", "--no-color", "--no-ext-diff"])
if actual != patch.read_bytes():
    sys.exit("Unexpected upstream working-tree modifications; use a clean pinned source")
