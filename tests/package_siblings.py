#!/usr/bin/env python3
"""Run only the direct-child sibling-resolution seam from a path with spaces."""
import os
import shutil
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: package_siblings.py CORE_PROCESS_TEST")
    source = os.path.abspath(sys.argv[1])
    if not os.path.isfile(source) or not os.access(source, os.X_OK):
        raise SystemExit("core process test executable is unavailable")

    with tempfile.TemporaryDirectory(prefix="agentvision package with spaces ") as directory:
        frontend = os.path.join(directory, "agentvision frontend")
        shutil.copy2(source, frontend)
        os.chmod(frontend, 0o700)
        result = subprocess.run(
            [frontend, "sibling-check"],
            cwd=directory,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=15,
            check=False,
        )
        if result.returncode:
            if result.stdout:
                sys.stderr.write(result.stdout)
            if result.stderr:
                sys.stderr.write(result.stderr)
            raise SystemExit(result.returncode)

    print("PASS: sibling resolution across relocated path with spaces and PATH impostor")


if __name__ == "__main__":
    main()
