"""THROWAWAY cleanup for the probe's known outer supervisor and PTY fds."""
import json
import os
import signal
import sys
import time


def finalize_probe(evidence, pid, fds, wait_status, raw, steps):
    # Called from finally: preserve the probe failure if evidence saving fails too.
    original_error = sys.exc_info()[1]
    errors = []
    try:
        for name, data in (("outer-output.ansi", bytes(raw)),
                           ("steps.json", (json.dumps(steps, indent=2) + "\n").encode())):
            try:
                (evidence / name).write_bytes(data)
            except OSError as exc:
                errors.append(exc)
    finally:
        if wait_status is None:
            try:
                os.killpg(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass  # Supervisor's group may already be gone.
            except OSError as exc:
                errors.append(exc)
        # Close before reaping: terminal teardown can otherwise wait on output.
        for fd in fds:
            try:
                os.close(fd)
            except OSError as exc:
                errors.append(exc)
        if wait_status is None:
            deadline = time.monotonic() + 2
            while True:
                try:
                    waited, _ = os.waitpid(pid, os.WNOHANG)
                except ChildProcessError:
                    break  # Already reaped.
                except OSError as exc:
                    errors.append(exc)
                    break
                if waited:
                    break
                if time.monotonic() >= deadline:
                    errors.append(RuntimeError("supervisor not reaped within bounded cleanup"))
                    break
                time.sleep(0.02)
    if errors:
        if original_error is not None:
            for exc in errors:
                print(f"Probe finalization also failed: {exc}", file=sys.stderr)
        else:
            for exc in errors[1:]:
                print(f"Probe finalization also failed: {exc}", file=sys.stderr)
            raise errors[0]
