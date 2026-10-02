#!/usr/bin/env python3
"""Focused synthetic controls for the source-profile prefix checker."""

from __future__ import annotations

import json
import importlib.util
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / "tools/slippi_profile_prefix_check.py"
WORKER = ROOT / "tools/slippi_profile_prefix_runtime.mjs"


class SourceProfilePrefixControlsTest(unittest.TestCase):
    def test_python_direct_parser_and_input_controls(self):
        result = subprocess.run(
            [sys.executable, str(CHECKER), "--self-test"],
            cwd=ROOT, text=True, capture_output=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertIn("direct-parser-import", report["controls"])
        self.assertIn("raw-byte-mutation-detected", report["controls"])
        self.assertIn("processed-button-mutation-detected", report["controls"])

    def test_node_decoder_and_comparator_controls(self):
        result = subprocess.run(
            ["node", str(WORKER), "--self-test"],
            cwd=ROOT, text=True, capture_output=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["result"], "passed")
        self.assertIn("synthetic-1216-byte-decoder", report["controls"])
        self.assertIn("synthetic-state-mutation", report["controls"])
        self.assertIn("synthetic-processed-mutation", report["controls"])

    def test_owned_process_group_graceful_cleanup(self):
        supervisor_path = ROOT / "reference-capture/slippi/process.py"
        spec = importlib.util.spec_from_file_location("source_profile_process_test", supervisor_path)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            supervisor = module.ProcessSupervisor(graceful_timeout=0.1, term_timeout=0.1)
            child = supervisor.start(
                "synthetic-worker", [sys.executable, "-c", (
                    "import signal,time\n"
                    "def stop(*_): raise SystemExit(0)\n"
                    "signal.signal(signal.SIGINT, stop)\n"
                    "time.sleep(30)\n"
                )], log_path=Path(directory) / "worker.log",
                graceful_signal=signal.SIGINT)
            receipt = supervisor.stop(child)
            self.assertTrue(receipt["process_group_released"])
            self.assertIsNotNone(receipt["returncode"])
            self.assertEqual(supervisor.close(), [])

    def test_timeout_rejects_missing_cleanup_reserve(self):
        spec = importlib.util.spec_from_file_location("source_profile_checker_test", CHECKER)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            args = module.parser().parse_args([
                "--replay", "replay.slp", "--runtime", "runtime.js", "--assets", "assets",
                "--source-probe", "probe.cpp", "--source-cmake", "runtime.cmake",
                "--profile-helper", "profile.cpp", "--probe-sha256", "0" * 64,
                "--cmake-sha256", "1" * 64, "--profile-helper-sha256", "2" * 64,
                "--runtime-sha256", "3" * 64, "--wasm-sha256", "4" * 64,
                "--profile-offset", "0x1234", "--seed", "0x13579bdf",
                "--timeout", "51", "--cleanup-timeout", "45",
                "--out", str(Path(directory) / "out"),
            ])
            with self.assertRaises(module.CheckError):
                module.require_normal_args(args)

    def test_failing_worker_keeps_returncode_stderr_and_cleanup(self):
        spec = importlib.util.spec_from_file_location("source_profile_worker_outcome_test", CHECKER)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        worker = subprocess.run(
            [sys.executable, "-c", "import sys; sys.stderr.write('synthetic worker failure\\n'); sys.exit(7)"],
            text=True, capture_output=True, check=False,
        )
        cleanup = {
            "process_group_released": True,
            "supervisor_close_succeeded": True,
            "pid": 321,
        }
        outcome = module.worker_outcome(
            worker.returncode, timed_out=False, worker_error=None,
            cleanup=cleanup, worker_stderr=worker.stderr,
        )
        self.assertEqual(worker.returncode, 7)
        self.assertEqual(outcome["result"], "failed")
        self.assertEqual(outcome["worker_returncode"], 7)
        self.assertIn("synthetic worker failure", outcome["worker_stderr"])
        self.assertEqual(outcome["cleanup"], cleanup)
        self.assertTrue(outcome["cleanup_succeeded"])

    def test_unreleased_process_group_cannot_claim_pass(self):
        spec = importlib.util.spec_from_file_location("source_profile_cleanup_gate_test", CHECKER)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        cleanup = {
            "process_group_released": False,
            "supervisor_close_succeeded": True,
            "pid": 654,
        }
        self.assertFalse(module.cleanup_succeeded(cleanup))
        outcome = module.worker_outcome(
            0, timed_out=False, worker_error=None, cleanup=cleanup,
        )
        self.assertEqual(outcome["result"], "failed")
        self.assertFalse(outcome["cleanup_succeeded"])
        self.assertIn("cleanup", outcome["failure"])

    def test_timeout_rejects_nonfinite_values(self):
        spec = importlib.util.spec_from_file_location("source_profile_timeout_finite_test", CHECKER)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            for option in ("--timeout", "--cleanup-timeout"):
                for value in ("nan", "inf", "-inf"):
                    values = {
                        "--replay": "replay.slp", "--runtime": "runtime.js", "--assets": "assets",
                        "--source-probe": "probe.cpp", "--source-cmake": "runtime.cmake",
                        "--profile-helper": "profile.cpp", "--probe-sha256": "0" * 64,
                        "--cmake-sha256": "1" * 64, "--profile-helper-sha256": "2" * 64,
                        "--runtime-sha256": "3" * 64, "--wasm-sha256": "4" * 64,
                        "--profile-offset": "0x1234", "--seed": "0x13579bdf",
                        "--timeout": "180", "--cleanup-timeout": "45",
                        "--out": str(Path(directory) / f"out-{option[2:]}-{value}"),
                    }
                    values[option] = value
                    argv = []
                    for key, item in values.items():
                        if str(item).startswith("-"):
                            argv.append(f"{key}={item}")
                        else:
                            argv.extend((key, item))
                    args = module.parser().parse_args(argv)
                    with self.assertRaises(module.CheckError):
                        module.require_normal_args(args)

    def test_scene_endpoint_contract_is_bounded(self):
        spec = importlib.util.spec_from_file_location("source_profile_scene_test", CHECKER)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        self.assertEqual(module.validate_scene_last(110), 110)
        self.assertEqual(module.validate_scene_last(1341), 1341)
        with self.assertRaises(module.CheckError):
            module.validate_scene_last(111)
        with self.assertRaises(module.CheckError):
            module.validate_scene_last(1342)


if __name__ == "__main__":
    unittest.main()
