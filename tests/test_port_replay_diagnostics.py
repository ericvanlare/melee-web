"""Strict diagnostics for a port trace that stopped before teardown."""

from copy import deepcopy
from contextlib import contextmanager
import json
from pathlib import Path
import sys
import struct
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from port_replay_diagnostics import (MAX_CAPTURE_BYTES, _read_port,
                                     diagnose_paths)  # noqa: E402
from test_retail_replay_validation import candidate  # noqa: E402
from test_port_replay_validation import fixture  # noqa: E402


def _write(path: Path, rows) -> None:
    path.write_text("".join(json.dumps(row, separators=(",", ":")) + "\n"
                         for row in rows), encoding="utf-8")


class PortReplayDiagnosticTests(unittest.TestCase):
    def test_oversized_capture_is_rejected_before_loading(self):
        with tempfile.TemporaryDirectory(prefix="port-diagnostic-size-") as directory:
            path = Path(directory) / "oversized.jsonl"
            with path.open("wb") as stream:
                stream.truncate(MAX_CAPTURE_BYTES + 1)
            with mock.patch.object(Path, "open", side_effect=AssertionError(
                    "oversized capture was opened")) as opened:
                with self.assertRaisesRegex(ValueError, "exceeds byte limit"):
                    _read_port(path)
            opened.assert_not_called()

    @contextmanager
    def _paths(self, port_rows, reference_a=None, reference_b=None):
        reference_a = candidate() if reference_a is None else reference_a
        reference_b = candidate() if reference_b is None else reference_b
        with tempfile.TemporaryDirectory(prefix="port-diagnostic-") as directory:
            directory = Path(directory)
            paths = (directory / "reference-a.jsonl", directory / "reference-b.jsonl",
                     directory / "port.jsonl")
            _write(paths[0], reference_a)
            _write(paths[1], reference_b)
            _write(paths[2], port_rows)
            yield paths

    def test_incomplete_prefix_reports_observed_frames_without_completion(self):
        _, port = fixture()
        prefix = port[:-1]
        with self._paths(prefix) as paths:
            report = diagnose_paths(*paths)
        self.assertEqual(report["status"], "incomplete_capture_diagnostic")
        self.assertFalse(report["complete"])
        self.assertFalse(report["gold_admitted"])
        self.assertEqual(report["performance"], "not_evaluated")
        self.assertEqual(report["observed_frames"], 3)
        self.assertEqual(report["frames_compared"], 3)
        self.assertIsNone(report["first_divergence"])
        self.assertEqual(report["reference_repeatability"], "pass")
        self.assertEqual(set(report["capture_hashes"]), {"reference_a", "reference_b", "port"})

    def test_prefix_first_difference_reports_input_and_state_without_synthesized_end(self):
        _, port = fixture()
        port = deepcopy(port[:-1])
        port[4]["supplied_inputs"][0] = "01" + "00" * 10
        port[5]["fighters"][1]["motion"] = 99
        with self._paths(port) as paths:
            report = diagnose_paths(*paths)
        self.assertEqual(report["status"], "incomplete_capture_diagnostic")
        self.assertEqual(report["first_divergence"]["record"], "frame")
        self.assertEqual(report["first_divergence"]["frame"], 1)
        self.assertEqual(report["first_divergence"]["field"], "[0]")
        self.assertEqual(report["checks"]["inputs"], "diverged")
        self.assertEqual(report["checks"]["fighters"], "diverged")

    def test_malformed_extra_and_reordered_prefixes_are_rejected(self):
        _, good = fixture()
        mutations = []
        extra = deepcopy(good[:-1])
        extra.append({"record": "error", "error": "crash"})
        mutations.append(extra)
        reordered = deepcopy(good[:-1])
        reordered[4]["index"] = 0
        mutations.append(reordered)
        too_many = deepcopy(good[:-1])
        too_many.append(deepcopy(too_many[-1]))
        too_many[-1]["index"] = 3
        mutations.append(too_many)
        for rows in mutations:
            with self.subTest(record=rows[-1].get("record")):
                with self._paths(rows) as paths:
                    report = diagnose_paths(*paths)
                self.assertEqual(report["status"], "invalid_capture")
                self.assertIn("error", report)

    def test_complete_capture_is_refused_for_strict_validator(self):
        _, port = fixture()
        with self._paths(port) as paths:
            report = diagnose_paths(*paths)
        self.assertEqual(report["status"], "invalid_capture")
        self.assertIn("end record", report["error"])
        self.assertIn("compare_paths", report["error"])

    def test_version_two_pad_history_difference_is_reported(self):
        def snapshot():
            config = bytes.fromhex(
                "0000002d00000008001e0000000000007f0000ff0000ff007fffff000000")
            return (config + bytes(12 * 66)).hex()

        def as_v2(rows):
            rows = deepcopy(rows)
            rows[0]["version"] = 2
            for key in ("pad_lib_hex", "pad_master_hex", "pad_game_hex"):
                rows[1].pop(key, None)
            for row in rows[1:-1]:
                row["pad_state_hex"] = snapshot()
            return rows

        reference_a = as_v2(candidate())
        reference_b = as_v2(candidate())
        _, port = fixture()
        port = as_v2(port[:-1])
        port[-1]["pad_state_hex"] = snapshot()
        raw = bytearray.fromhex(port[4]["pad_state_hex"])
        struct.pack_into(">i", raw, 30 + 4 * 66 + 20, 7)
        port[4]["pad_state_hex"] = raw.hex()
        with self._paths(port, reference_a, reference_b) as paths:
            report = diagnose_paths(*paths)
        self.assertEqual(report["status"], "incomplete_capture_diagnostic")
        self.assertEqual(report["first_divergence"]["frame"], 1)
        self.assertEqual(report["checks"]["pad_state_hex"], "diverged")

    def test_duplicate_json_keys_are_rejected(self):
        reference_a = candidate()
        reference_b = candidate()
        _, port = fixture()
        with tempfile.TemporaryDirectory(prefix="port-diagnostic-duplicate-") as directory:
            directory = Path(directory)
            paths = (directory / "a.jsonl", directory / "b.jsonl", directory / "port.jsonl")
            _write(paths[0], reference_a)
            _write(paths[1], reference_b)
            paths[2].write_text(
                json.dumps(port[0], separators=(",", ":")) + "\n" +
                '{"record":"header","record":"header"}\n', encoding="utf-8")
            report = diagnose_paths(*paths)
        self.assertEqual(report["status"], "invalid_capture")
        self.assertIn("duplicate JSON key", report["error"])


if __name__ == "__main__":
    unittest.main()
