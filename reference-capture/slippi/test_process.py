# SPDX-License-Identifier: MIT
"""Focused process ownership checks for the local Slippi harness."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from process import ProcessSupervisor


class ProcessSupervisorTests(unittest.TestCase):
    def test_graceful_signal_reaps_owned_child(self):
        with tempfile.TemporaryDirectory(prefix="slippi-process-test-") as directory:
            root = Path(directory)
            ready = root / "ready.json"
            log = root / "client.log"
            code = (
                "import json, os, pathlib, signal, sys, time; "
                "signal.signal(signal.SIGINT, lambda *_: exit(0)); "
                "signal.signal(signal.SIGTERM, lambda *_: exit(3)); "
                "pathlib.Path(sys.argv[1]).write_text(json.dumps({'pid':os.getpid()})); "
                "time.sleep(30)"
            )
            supervisor = ProcessSupervisor(graceful_timeout=1, term_timeout=1)
            child = supervisor.start(
                "fake-client",
                [sys.executable, "-c", code, str(ready)],
                log_path=log,
                graceful_signal=signal.SIGINT,
            )
            try:
                deadline = time.monotonic() + 2
                while not ready.exists() and time.monotonic() < deadline:
                    time.sleep(0.01)
                self.assertTrue(ready.exists(), "child did not publish its startup receipt")
                result = supervisor.close()
                self.assertEqual(len(result), 1)
                self.assertEqual(result[0]["name"], "fake-client")
                self.assertEqual(result[0]["returncode"], 0)
                self.assertTrue(result[0]["process_group_released"])
                self.assertEqual(child.process.poll(), 0)
            finally:
                supervisor.close()

    def test_shutdown_escalates_for_remaining_process_group_child(self):
        with tempfile.TemporaryDirectory(prefix="slippi-process-group-test-") as directory:
            root = Path(directory)
            log = root / "service.log"
            code = (
                "import pathlib, signal, subprocess, sys, time; "
                "signal.signal(signal.SIGINT, lambda *_: exit(0)); "
                "subprocess.Popen([sys.executable, '-c', "
                "'import signal,time; signal.signal(signal.SIGINT, signal.SIG_IGN); "
                "print(\"child-ready\", flush=True); time.sleep(30)'], "
                "stdout=sys.stdout, stderr=sys.stderr); "
                "print('parent-ready', flush=True); "
                "time.sleep(30)"
            )
            supervisor = ProcessSupervisor(graceful_timeout=0.5, term_timeout=1)
            child = supervisor.start(
                "fake-service",
                [sys.executable, "-c", code],
                log_path=log,
                graceful_signal=signal.SIGINT,
            )
            try:
                deadline = time.monotonic() + 2
                while (
                    "child-ready" not in log.read_text(errors="replace")
                    and time.monotonic() < deadline
                ):
                    time.sleep(0.01)
                self.assertIn("child-ready", log.read_text(errors="replace"))
                result = supervisor.close()
                self.assertEqual(result[0]["returncode"], 0)
                self.assertTrue(result[0]["process_group_released"])
                with self.assertRaises(ProcessLookupError):
                    os.killpg(child.pid, 0)
            finally:
                supervisor.close()

    def test_explicit_child_stop_keeps_other_children_managed(self):
        with tempfile.TemporaryDirectory(prefix="slippi-stop-test-") as directory:
            root = Path(directory)
            supervisor = ProcessSupervisor(graceful_timeout=1, term_timeout=1)
            first = supervisor.start(
                "first", [sys.executable, "-c", "import time; time.sleep(30)"],
                log_path=root / "first.log",
            )
            second = supervisor.start(
                "second", [sys.executable, "-c", "import time; time.sleep(30)"],
                log_path=root / "second.log",
            )
            result = supervisor.stop(first)
            self.assertEqual(result["pid"], first.pid)
            self.assertIsNotNone(first.process.poll())
            remaining = supervisor.close()
            self.assertEqual([row["pid"] for row in remaining], [second.pid])

    def test_parent_sigint_runs_bounded_owned_process_cleanup(self):
        with tempfile.TemporaryDirectory(prefix="slippi-interrupt-test-") as directory:
            root = Path(directory)
            child = None
            with self.assertRaises(KeyboardInterrupt):
                with ProcessSupervisor(graceful_timeout=1, term_timeout=1) as supervisor:
                    child = supervisor.start(
                        "interrupt-child",
                        [sys.executable, "-c", "import time; time.sleep(30)"],
                        log_path=root / "child.log",
                        graceful_signal=signal.SIGTERM,
                    )
                    os.kill(os.getpid(), signal.SIGINT)
            self.assertIsNotNone(child)
            self.assertIsNotNone(child.process.poll())
            self.assertFalse(ProcessSupervisor._group_exists(child.pid))


if __name__ == "__main__":
    unittest.main()
