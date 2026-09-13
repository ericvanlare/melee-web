"""Focused tests for the explicit two-pass retail input bootstrap gate."""

from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from retail_input_bootstrap import (  # noqa: E402
    BootstrapCalibrationError,
    calibration_record,
    load_calibration,
    load_runtime_binding,
    runtime_binding_record,
    write_calibration,
    write_runtime_binding,
)


def plan():
    return {
        "schema": "melee-web-retail-input-plan",
        "version": 1,
        "policy": "dolphin-pipe-processed-v2",
        "source_sha256": "a" * 64,
        "first_frame": -123,
        "source_stage": 32,
        "source_characters": [2, 20],
        "frames": [
            ["0000b30000000000000000", "0000000000000002000000"],
            ["0000bed400000000000000", "0000000000000002000000"],
        ],
    }


def runtime():
    return {
        "dol_sha1": "b" * 40,
        "dolphin_binary_sha256": "c" * 64,
        "source_revision": "revision",
        "cpu": "JITARM64",
        "cpu_thread": False,
        "cheats": False,
        "background_input": True,
        "fixed_rtc": 1704067200,
        "setup_snapshot_sha256": "d" * 64,
        "dolphin_ini_canonical_sha256": "e" * 64,
        "gcpad_ini_sha256": "f" * 64,
        "external_save_hashes": {"SRAM.raw": "a" * 64},
    }


def valid_value():
    source = plan()
    r = runtime()
    identity = {
        "ordinal": 19,
        "scene_frame": 221,
        "retrace_count": 500,
        "source_vi_count": 499,
        "caller": 0x80376A28,
        "stack": 0x80002000,
        "queue_hex": "05020205000000008046b108",
        "raw_hex": "0000b300000000000000009c00000000000000020000000000000000000000000000ff0000000000000000000000ff48",
    }
    return calibration_record(
        plan=source, plan_sha256=hashlib.sha256(b"plan").hexdigest(),
        provenance=r, collector_sha256="e" * 64,
        construction_pad_reads=20, last_construction_pad_read=identity)


class RetailInputBootstrapTests(unittest.TestCase):
    def test_valid_sidecar_binds_plan_runtime_and_final_read(self):
        source = plan()
        plan_hash = hashlib.sha256(b"plan").hexdigest()
        value = valid_value()
        self.assertIs(load_calibration_value(value, source, plan_hash), value)

    def test_first_input_mutation_is_rejected(self):
        source = plan()
        value = valid_value()
        value["first_input"] = copy.deepcopy(source["frames"][1])
        with self.assertRaisesRegex(BootstrapCalibrationError, "first input"):
            load_calibration_value(value, source, hashlib.sha256(b"plan").hexdigest())

    def test_final_read_identity_mutation_is_rejected(self):
        source = plan()
        value = valid_value()
        value["last_construction_pad_read"]["queue_hex"] = "00"
        with self.assertRaisesRegex(BootstrapCalibrationError, "queue_hex"):
            load_calibration_value(value, source, hashlib.sha256(b"plan").hexdigest())

    def test_file_reader_is_bounded_and_hash_is_preserved(self):
        source = plan()
        value = valid_value()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bootstrap.json"
            write_calibration(path, value)
            loaded, digest = load_calibration(
                path, plan=source, plan_sha256=hashlib.sha256(b"plan").hexdigest(),
                runtime=runtime(), collector_sha256="e" * 64)
            self.assertEqual(loaded, value)
            self.assertEqual(digest, hashlib.sha256(path.read_bytes()).hexdigest())

            oversized = Path(directory) / "oversized.json"
            oversized.write_bytes(b"{" + b" " * (1024 * 1024) + b"}")
            with self.assertRaisesRegex(BootstrapCalibrationError, "byte limit"):
                load_calibration(
                    oversized, plan=source,
                    plan_sha256=hashlib.sha256(b"plan").hexdigest(),
                    runtime=runtime(), collector_sha256="e" * 64)

    def test_duplicate_json_and_out_of_range_read_count_are_rejected(self):
        source = plan()
        value = valid_value()
        value["construction_pad_reads"] = 4097
        with self.assertRaisesRegex(BootstrapCalibrationError, "bounded range"):
            load_calibration_value(value, source, hashlib.sha256(b"plan").hexdigest())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "duplicate.json"
            path.write_text('{"schema":"x","schema":"y"}', encoding="utf-8")
            with self.assertRaisesRegex(BootstrapCalibrationError, "duplicate JSON key"):
                load_calibration(path, plan=source,
                                 plan_sha256=hashlib.sha256(b"plan").hexdigest(),
                                 runtime=runtime(), collector_sha256="e" * 64)

    def test_runtime_binding_is_strict_and_round_trips(self):
        value = runtime_binding_record(
            dolphin_ini_canonical_sha256="e" * 64,
            gcpad_ini_sha256="f" * 64,
            external_save_hashes={"SRAM.raw": "a" * 64})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "runtime.json"
            write_runtime_binding(path, value)
            loaded, digest = load_runtime_binding(path)
            self.assertEqual(loaded, value)
            self.assertEqual(digest, hashlib.sha256(path.read_bytes()).hexdigest())
        changed = dict(value)
        changed["unexpected"] = True
        with self.assertRaisesRegex(BootstrapCalibrationError, "missing or unexpected"):
            __import__("retail_input_bootstrap").validate_runtime_binding(changed)


def load_calibration_value(value, source, plan_hash):
    return __import__("retail_input_bootstrap").validate_calibration(
        value, plan=source, plan_sha256=plan_hash,
        runtime=runtime(), collector_sha256="e" * 64)


if __name__ == "__main__":
    unittest.main()
