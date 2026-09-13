"""Focused strict tests for the retail match-timer sidecar."""

from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import retail_timer_validation as VALIDATION


def setup_hex(*, timer=True, counts_up=False, time_limit=480):
    raw = bytearray(0x138)
    raw[0] = (2 if timer else 0) | (1 if counts_up else 0)
    raw[2] = 0x80
    raw[0x10:0x14] = time_limit.to_bytes(4, "big")
    return raw.hex()


def sidecar(frame_count=3, *, setup=None):
    if setup is None:
        setup = setup_hex()
    rows = [{
        "record": "header", "schema": VALIDATION.SCHEMA,
        "version": VALIDATION.VERSION, "frames_requested": frame_count,
        "setup_hex": setup, "phase": VALIDATION.PHASE,
    }, {
        "record": "initial", "match_frame": 0, "seconds": 479,
        "subframe": 0, "outcome": 0, "end_state": 0,
    }]
    for index in range(frame_count):
        rows.append({
            "record": "frame", "index": index, "match_frame": index,
            "seconds": 479 - (index // 60), "subframe": index % 60,
            "outcome": 0, "end_state": 0,
        })
    rows.append({"record": "end", "frames": frame_count, "status": "captured"})
    return rows


def write_rows(path, rows):
    path.write_text("".join(json.dumps(row, sort_keys=True) + "\n" for row in rows),
                    encoding="utf-8")


class RetailTimerValidationTests(unittest.TestCase):
    def test_valid_sidecar_and_exact_full_comparison(self):
        rows = sidecar()
        capture = VALIDATION.validate_rows(rows)
        self.assertEqual(len(capture.frames), 3)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("a", "b", "port"):
                write_rows(root / name, rows)
            report = VALIDATION.compare_paths(
                root / "a", root / "b", root / "port",
                setup_hex=setup_hex())
        self.assertEqual(report["status"], "pass")
        self.assertEqual(report["reference"]["status"], "repeatable")
        self.assertEqual(report["port"]["status"], "match")
        self.assertEqual(report["setup"]["time_limit"], 480)
        self.assertRegex(report["captures"]["a"]["sha256"], r"^[0-9a-f]{64}$")

    def test_first_exact_field_mismatch_is_reported_after_reference_pair(self):
        rows = sidecar()
        changed = deepcopy(rows)
        changed[3]["seconds"] = 478
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_rows(root / "a", rows)
            write_rows(root / "b", rows)
            write_rows(root / "port", changed)
            report = VALIDATION.compare_paths(
                root / "a", root / "b", root / "port", setup_hex=setup_hex())
        self.assertEqual(report["status"], "port_diverged")
        self.assertEqual(report["reference"]["status"], "repeatable")
        self.assertEqual(report["port"]["first_divergence"]["field"],
                         "records[3].seconds")

    def test_nonrepeatable_reference_is_reported_before_port(self):
        rows = sidecar()
        changed = deepcopy(rows)
        changed[2]["subframe"] = 1
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_rows(root / "a", rows)
            write_rows(root / "b", changed)
            write_rows(root / "port", rows)
            report = VALIDATION.compare_paths(
                root / "a", root / "b", root / "port", setup_hex=setup_hex())
        self.assertEqual(report["status"], "reference_nonrepeatable")
        self.assertEqual(report["reference"]["first_divergence"]["field"],
                         "records[2].subframe")
        self.assertEqual(report["port"]["status"], "not_run")

    def test_incomplete_missing_frame_and_oversized_sequences_rejected(self):
        rows = sidecar()
        with self.assertRaises(VALIDATION.TimerValidationError):
            VALIDATION.validate_rows(rows[:-2])
        oversized = sidecar(frame_count=1)
        oversized.insert(-1, {
            "record": "frame", "index": 1, "match_frame": 1,
            "seconds": 479, "subframe": 1, "outcome": 0, "end_state": 0,
        })
        with self.assertRaises(VALIDATION.TimerValidationError):
            VALIDATION.validate_rows(oversized)

    def test_oversized_file_is_rejected_before_json_parsing(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "oversized.jsonl"
            with path.open("wb") as stream:
                stream.truncate(VALIDATION.MAX_BYTES + 1)
            with self.assertRaisesRegex(VALIDATION.TimerValidationError, "exceeds"):
                VALIDATION.load_capture(path)

    def test_match_frame_is_unsigned_u32(self):
        accepted = sidecar()
        accepted[1]["match_frame"] = 0xFFFFFFFF
        accepted[2]["match_frame"] = 0xFFFFFFFF
        VALIDATION.validate_rows(accepted)
        for value in (-1, 0x100000000):
            rejected = sidecar()
            rejected[1]["match_frame"] = value
            with self.assertRaises(VALIDATION.TimerValidationError):
                VALIDATION.validate_rows(rejected)

    def test_header_binding_and_timer_rules_are_strict(self):
        rows = sidecar()
        for bad_setup in (
            setup_hex(timer=False), setup_hex(counts_up=True), setup_hex(time_limit=0),
            setup_hex().upper(),
        ):
            bad = deepcopy(rows)
            bad[0]["setup_hex"] = bad_setup
            with self.assertRaises(VALIDATION.TimerValidationError):
                VALIDATION.validate_rows(bad)
        bad = deepcopy(rows)
        bad[0]["setup_hex"] = setup_hex(time_limit=600)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_rows(root / "a", rows)
            write_rows(root / "b", rows)
            write_rows(root / "port", bad)
            report = VALIDATION.compare_paths(
                root / "a", root / "b", root / "port", setup_hex=setup_hex())
        self.assertEqual(report["status"], "invalid_capture")
        self.assertEqual(report["invalid_capture"]["capture"], "port")

    def test_setup_matches_native_timer_admission_profile(self):
        for time_limit in (60, 480, 99 * 60):
            VALIDATION.validate_rows(sidecar(setup=setup_hex(time_limit=time_limit)))
        for time_limit in (1, 61, 6000):
            with self.subTest(time_limit=time_limit):
                with self.assertRaisesRegex(VALIDATION.TimerValidationError, "time_limit"):
                    VALIDATION.validate_rows(sidecar(setup=setup_hex(time_limit=time_limit)))
        for mutation, message in ((lambda raw: raw.__setitem__(1, 2), "timer_shows_hours"),
                                  (lambda raw: raw.__setitem__(0x14, 1), "x14")):
            raw = bytearray.fromhex(setup_hex())
            mutation(raw)
            with self.subTest(message=message):
                with self.assertRaisesRegex(VALIDATION.TimerValidationError, message):
                    VALIDATION.validate_rows(sidecar(setup=raw.hex()))

    def test_reference_capture_binds_count_and_every_match_frame(self):
        rows = sidecar()
        reference = SimpleNamespace(
            match_enter={"start_melee_hex": setup_hex()},
            header={"frames_requested": 3},
            initial={"match_frame": 0},
            frames=tuple({"match_frame": index} for index in range(3)),
            sha256="ee" * 32,
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("a", "b", "port", "reference"):
                write_rows(root / name, rows)
            with patch("retail_replay_validation.load_capture", return_value=reference):
                report = VALIDATION.compare_paths(
                    root / "a", root / "b", root / "port",
                    reference_capture=root / "reference")
            self.assertEqual(report["status"], "pass")
            self.assertEqual(report["reference_binding"]["match_frames"], "exact")

            shifted = deepcopy(rows)
            shifted[2]["match_frame"] = 1
            write_rows(root / "port", shifted)
            with patch("retail_replay_validation.load_capture", return_value=reference):
                report = VALIDATION.compare_paths(
                    root / "a", root / "b", root / "port",
                    reference_capture=root / "reference")
            self.assertEqual(report["status"], "invalid_capture")
            self.assertEqual(report["invalid_capture"]["capture"], "port")
            self.assertIn("frame[0].match_frame", report["invalid_capture"]["error"])

            short = sidecar(frame_count=2)
            write_rows(root / "port", short)
            with patch("retail_replay_validation.load_capture", return_value=reference):
                report = VALIDATION.compare_paths(
                    root / "a", root / "b", root / "port",
                    reference_capture=root / "reference")
            self.assertEqual(report["status"], "invalid_capture")
            self.assertEqual(report["invalid_capture"]["capture"], "port")
            self.assertIn("frames_requested", report["invalid_capture"]["error"])

    def test_unknown_duplicate_and_reordered_records_rejected(self):
        rows = sidecar()
        unknown = deepcopy(rows)
        unknown[2]["pause"] = False
        with self.assertRaises(VALIDATION.TimerValidationError):
            VALIDATION.validate_rows(unknown)
        reordered = deepcopy(rows)
        reordered[2], reordered[3] = reordered[3], reordered[2]
        with self.assertRaises(VALIDATION.TimerValidationError):
            VALIDATION.validate_rows(reordered)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "duplicate.jsonl"
            path.write_text('{"record":"header","record":"header"}\n',
                            encoding="utf-8")
            with self.assertRaises(VALIDATION.TimerValidationError):
                VALIDATION.load_capture(path)

    def test_cli_requires_binding_and_rejects_output_alias(self):
        rows = sidecar()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("a", "b", "port"):
                write_rows(root / name, rows)
            script = ROOT / "scripts" / "compare_retail_timers.py"
            missing = subprocess.run(
                [sys.executable, str(script), str(root / "a"), str(root / "b"),
                 str(root / "port")], capture_output=True, text=True)
            self.assertEqual(missing.returncode, 2)
            alias = subprocess.run(
                [sys.executable, str(script), str(root / "a"), str(root / "b"),
                 str(root / "port"), "--setup-hex", setup_hex(),
                 "--output", str(root / "a")], capture_output=True, text=True)
            self.assertEqual(alias.returncode, 2)
            self.assertIn("overwrite", alias.stderr)


if __name__ == "__main__":
    unittest.main()
