#!/usr/bin/env python3
"""Focused checks for throwaway evidence classification and cleanup failures."""
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
ARCHIVE = HERE.parents[1] / "docs/v0/evidence"

class ClassificationChecks(unittest.TestCase):
    def test_closed_shell_states(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for source in (ARCHIVE / "screens").glob("*.txt"):
                shutil.copyfile(source, root / source.name)
            original = json.loads((ARCHIVE / "input-steps.json").read_text())
            b_pid = json.loads((ARCHIVE / "results.json").read_text())["shell_b_pid"]
            for state in (None, "Z", "S", "R", "T"):
                with self.subTest(state=state):
                    steps = json.loads(json.dumps(original))
                    event = next(x for x in steps if "owned_shell_processes_after_b_close" in x)
                    if state:
                        event["owned_shell_processes_after_b_close"].append(f"{b_pid} 123 {state}")
                    (root / "steps.json").write_text(json.dumps(steps))
                    result = subprocess.run([sys.executable, str(HERE / "verify_probe.py"), str(root)],
                                            text=True, capture_output=True)
                    self.assertEqual(result.returncode, 1 if state else 0, result.stderr)
                    summary = json.loads(result.stdout)
                    self.assertEqual(summary["core_feasibility_checks"], "passed")
                    self.assertEqual(summary["closed_b_absent_at_sample"], state is None)
                    self.assertNotIn("closed_b_reaped", summary)
                    self.assertEqual(summary["closed_b_states_at_sample"], [state] if state else [])

class CleanupChecks(unittest.TestCase):
    def setUp(self):
        from probe_cleanup import finalize_probe
        self.finish = finalize_probe
        self.fds = os.pipe()

    def tearDown(self):
        for fd in self.fds:
            try:
                os.close(fd)
            except OSError:
                pass

    def assert_closed(self):
        for fd in self.fds:
            with self.assertRaises(OSError):
                os.fstat(fd)

    def test_evidence_failure_preserves_original_and_still_reaps(self):
        original = RuntimeError("original probe failure")
        with tempfile.TemporaryDirectory() as folder, \
             patch("probe_cleanup.sys.stderr", new_callable=io.StringIO) as diagnostics, \
             patch.object(Path, "write_bytes", side_effect=OSError("disk full")), \
             patch("probe_cleanup.os.killpg") as kill, \
             patch("probe_cleanup.os.waitpid", return_value=(123, 9)) as wait:
            with self.assertRaises(RuntimeError) as caught:
                try:
                    raise original
                finally:
                    self.finish(Path(folder), 123, self.fds, None, b"", [])
            self.assertIs(caught.exception, original)
            self.assertIn("disk full", diagnostics.getvalue())
            kill.assert_called_once()
            wait.assert_called_once_with(123, os.WNOHANG)
            self.assert_closed()

    def test_already_gone_group_and_reaped_child(self):
        with tempfile.TemporaryDirectory() as folder, \
             patch("probe_cleanup.os.killpg", side_effect=ProcessLookupError), \
             patch("probe_cleanup.os.waitpid", side_effect=ChildProcessError):
            self.finish(Path(folder), 123, self.fds, None, b"", [])
            self.assert_closed()

    def test_evidence_failure_is_reported_after_normal_cleanup(self):
        with tempfile.TemporaryDirectory() as folder, \
             patch.object(Path, "write_bytes", side_effect=OSError("disk full")), \
             patch("probe_cleanup.os.killpg") as kill, \
             patch("probe_cleanup.os.waitpid") as wait:
            with self.assertRaisesRegex(OSError, "disk full"):
                self.finish(Path(folder), 123, self.fds, 0, b"", [])
            kill.assert_not_called()
            wait.assert_not_called()
            self.assert_closed()

    def test_reap_timeout_is_bounded(self):
        with tempfile.TemporaryDirectory() as folder, \
             patch("probe_cleanup.os.killpg"), \
             patch("probe_cleanup.os.waitpid", return_value=(0, 0)) as wait, \
             patch("probe_cleanup.time.monotonic", side_effect=[0, 3]):
            with self.assertRaisesRegex(RuntimeError, "bounded cleanup"):
                self.finish(Path(folder), 123, self.fds, None, b"", [])
            wait.assert_called_once_with(123, os.WNOHANG)
            self.assert_closed()

if __name__ == "__main__":
    unittest.main(verbosity=2)
