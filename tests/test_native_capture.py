"""Focused safety and evidence checks for scripts/native_capture.py."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import native_capture as capture  # noqa: E402


class _NeverEndingProcess:
    def __init__(self):
        self.calls = []
        self.returncode = None

    def poll(self):
        return None

    def wait(self, timeout=None):
        self.calls.append(("wait", timeout))

    def send_signal(self, value):
        self.calls.append(("signal", value))

    def terminate(self):
        self.calls.append(("terminate",))

    def kill(self):
        self.calls.append(("kill",))


class NativeCaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def _exports(self, *, pid=1234, start=0, duration=2_000_000_000,
                 window="1 minute, 30 seconds"):
        toc = self.root / "toc.xml"
        states = self.root / "states.xml"
        toc.write_text(f"""<?xml version="1.0"?>
<trace-toc><run number="1"><info><target><process pid="{pid}"/></target>
<summary><duration>2.25</duration><recording-mode>Windowed ({window})</recording-mode>
<time-limit>5 seconds</time-limit><start-date>x</start-date><end-date>y</end-date>
<end-reason>User pressed Stop</end-reason></summary></info></run></trace-toc>""",
                       encoding="utf-8")
        states.write_text(f"""<?xml version="1.0"?>
<trace-query-result><row><start-time>{start}</start-time>
<duration>{duration}</duration><process><pid>{pid}</pid></process></row>
</trace-query-result>""", encoding="utf-8")
        return toc, states

    def _receipt(self, *, template="System Trace", retention=90.0,
                 expires=None):
        evidence_names = ["configuration.json", "native.trace",
                          "native-toc.xml", "native-thread-state.xml"]
        evidence = self.root / "configuration.json"
        for name in evidence_names:
            if name == "native.trace":
                (self.root / name).mkdir(exist_ok=True)
                (self.root / name / "data").write_bytes(b"trace bundle")
            else:
                (self.root / name).write_text(f"{name}\n", encoding="utf-8")
        config = {"platform": "macOS", "template": template,
                  "scope": capture.DEFAULT_SCOPE,
                  "retention_window_seconds": retention,
                  "minimum_retained_seconds": 0.5,
                  "xctrace_version": "xctrace version 16.0 (test)",
                  "xctrace_identity_sha256": "a" * 64,
                  "host_identity": {"os": "macOS", "os_release": "test", "machine": "arm64"},
                  "xctrace_template": template,
                  "record_mode": "attach-one-explicit-pid",
                  "export_mode": "toc-and-thread-state-xpath"}
        receipt = {
            "schema": capture.SCHEMA,
            "version": capture.RECEIPT_VERSION,
            "status": "passed",
            "created_utc_ms": 1,
            "expires_utc_ms": int(expires if expires is not None else 9_999_999_999_999),
            "scope": {"id": capture.DEFAULT_SCOPE, "kind": "one-attached-process"},
            "configuration": config,
            "configuration_sha256": hashlib.sha256(capture._canonical(config)).hexdigest(),
            "tool": {"version": config["xctrace_version"]},
            "probe": {"role": "owned-non-game-process"},
            "session": {},
            "coverage": {"retained_seconds": 2.0},
            "evidence_files": {
                name: capture._artifact_identity(self.root / name)
                for name in evidence_names},
        }
        receipt = capture._seal(receipt, "receipt_sha256")
        path = self.root / "preflight.json"
        path.write_bytes(capture._canonical(receipt))
        return path

    def test_exported_target_range_and_window_are_checked(self):
        toc, states = self._exports()
        result = capture.validate_exported_coverage(
            toc, states, expected_pid=1234, expected_window_seconds=90,
            min_retained_seconds=1.5, required_range_seconds=(0.25, 1.5))
        self.assertEqual(result["target_pid"], 1234)
        self.assertEqual(result["retained_seconds"], 2.0)
        self.assertEqual(result["required_range_seconds"], [0.25, 1.5])

    def test_coverage_rejects_wrong_pid_and_short_range(self):
        toc, states = self._exports(pid=1234)
        with self.assertRaisesRegex(capture.NativeCaptureError, "TOC target PID"):
            capture.validate_exported_coverage(toc, states, expected_pid=999,
                                               expected_window_seconds=90)
        toc, states = self._exports(duration=100_000_000)
        with self.assertRaisesRegex(capture.NativeCaptureError, "below minimum"):
            capture.validate_exported_coverage(toc, states, expected_pid=1234,
                                               expected_window_seconds=90,
                                               min_retained_seconds=1.0)

    def test_coverage_rejects_uncovered_requested_range(self):
        toc, states = self._exports(duration=1_000_000_000)
        with self.assertRaisesRegex(capture.NativeCaptureError, "do not cover required"):
            capture.validate_exported_coverage(toc, states, expected_pid=1234,
                                               expected_window_seconds=90,
                                               min_retained_seconds=0.5,
                                               required_range_seconds=(0.5, 2.0))

    def test_expired_and_incompatible_receipts_are_rejected(self):
        expired = self._receipt(expires=100)
        with self.assertRaisesRegex(capture.NativeCaptureError, "expired"):
            capture.validate_preflight_receipt(expired, now_ms=100)
        incompatible = self._receipt(retention=60)
        with self.assertRaisesRegex(capture.NativeCaptureError, "retention window"):
            capture.validate_preflight_receipt(incompatible, retention_seconds=90)
        incompatible = self._receipt(template="Time Profiler")
        with self.assertRaisesRegex(capture.NativeCaptureError, "template"):
            capture.validate_preflight_receipt(incompatible)

    def test_receipt_tampering_and_evidence_mutation_are_rejected(self):
        path = self._receipt()
        data = json.loads(path.read_text(encoding="utf-8"))
        data["scope"]["id"] = "other"
        path.write_text(json.dumps(data), encoding="utf-8")
        with self.assertRaisesRegex(capture.NativeCaptureError, "receipt_sha256"):
            capture.validate_preflight_receipt(path)
        path = self._receipt()
        (self.root / "configuration.json").write_text("changed\n", encoding="utf-8")
        with self.assertRaisesRegex(capture.NativeCaptureError, "changed or is missing"):
            capture.validate_preflight_receipt(path)

    def test_existing_output_directory_is_never_reused(self):
        directory = self.root / "evidence"
        directory.mkdir()
        with self.assertRaisesRegex(capture.NativeCaptureError, "already exists"):
            capture._new_evidence_dir(directory)

    def test_receipt_rejects_changed_recorder_helper(self):
        path = self._receipt()
        tool = json.loads(path.read_text())["tool"]
        tool["helper_sha256"] = "changed"
        with self.assertRaisesRegex(capture.NativeCaptureError, "helper_sha256"):
            capture.validate_preflight_receipt(path, current_tool=tool)

    def test_directory_bundle_and_empty_log_integrity(self):
        path = self._receipt()
        receipt = json.loads(path.read_text())
        (self.root / "empty.stderr").write_bytes(b"")
        receipt["evidence_files"]["empty.stderr"] = capture._artifact_identity(self.root / "empty.stderr")
        path.write_bytes(capture._canonical(capture._seal(receipt, "receipt_sha256")))
        capture.validate_preflight_receipt(path)
        (self.root / "native.trace/data").write_bytes(b"changed bundle")
        with self.assertRaisesRegex(capture.NativeCaptureError, "changed or is missing"):
            capture.validate_preflight_receipt(path)

    def test_failed_startup_is_reported_with_startup_phase(self):
        def failed_runner(command, timeout):
            raise subprocess.TimeoutExpired(command, timeout)

        with self.assertRaisesRegex(capture.NativeCaptureError, "startup"):
            capture._tool_identity(command_runner=failed_runner)

    def test_failed_export_is_reported_with_export_phase(self):
        trace = self.root / "native.trace"
        trace.write_bytes(b"trace")

        def failed_runner(command, timeout):
            raise capture.NativeCaptureError("startup", "export process failed")

        with self.assertRaisesRegex(capture.NativeCaptureError, "export"):
            capture._export_recording(trace, self.root, timeout=1,
                                      command_runner=failed_runner)

    def test_failed_finalization_escalates_only_owned_profiler(self):
        process = _NeverEndingProcess()
        with self.assertRaisesRegex(capture.NativeCaptureError, "SIGKILL"):
            capture._stop_owned_process(process, stop_timeout=0, kill_timeout=0)
        self.assertEqual([call[0] for call in process.calls],
                         ["signal", "wait", "terminate", "wait", "kill", "wait"])

    def test_receipt_validation_is_required_before_record(self):
        with mock.patch.object(capture, "_target_alive", return_value=True):
            with self.assertRaisesRegex(capture.NativeCaptureError, "cannot read JSON"):
                capture.run_capture(pid=1234, output=self.root / "capture",
                                    receipt=self.root / "missing.json")


if __name__ == "__main__":
    unittest.main()
