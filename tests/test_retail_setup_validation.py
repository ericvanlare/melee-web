"""Focused tests for the exact development setup gate."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import retail_setup_validation as VALIDATION
from retail_input_plan import DISCONNECTED_PAD


def setup_bytes():
    raw = bytearray(0x138)
    raw[0] = (1 << 5) | (1 << 1)
    raw[4] = 1 << 6  # gmVsMelee_EnterVs sets is_vs on the source handoff.
    raw[2] = (1 << 7) | (1 << 3)
    raw[0x0E:0x10] = (31).to_bytes(2, "big")
    raw[0x10:0x14] = (480).to_bytes(4, "big")
    raw[0x2C:0x30] = bytes.fromhex("3f800000")
    raw[0x0B] = 0xFF
    raw[0x20:0x28] = b"\xff" * 8
    raw[0x30:0x34] = bytes.fromhex("3f800000")
    raw[0x34:0x38] = bytes.fromhex("3f800000")
    for index, (kind, costume, rumble) in enumerate(((2, 2, True), (20, 2, False))):
        base = 0x60 + index * 0x24
        raw[base] = kind
        raw[base + 1] = 0
        raw[base + 2] = 4
        raw[base + 3] = costume
        raw[base + 4] = 0
        raw[base + 12] = 0x80 if rumble else 0
    for index in range(2, 6):
        raw[0x60 + index * 0x24 + 1] = 3
    return raw


def expected_setup():
    return {
        "players": [
            {"port": 1, "character_kind": 2, "costume": 2, "stocks": 4,
             "player_type": 0, "rumble_enabled": True},
            {"port": 2, "character_kind": 20, "costume": 2, "stocks": 4,
             "player_type": 0, "rumble_enabled": False},
        ],
        "stage": 31, "match_kind": 1, "timer_enabled": True,
        "timer_counts_up": False, "time_limit_seconds": 480, "is_stock": True,
        "disable_pausing": True, "is_teams": False, "item_frequency": -1,
        "item_mask_hex": "ffffffffffffffff", "damage_ratio_bits": "3f800000",
        "game_speed_bits": "3f800000",
    }


def write_inputs(root: Path):
    source = root / "source.slp"
    source_bytes = b"pinned donor bytes"
    source.write_bytes(source_bytes)
    source_sha = hashlib.sha256(source_bytes).hexdigest()
    input_plan = {
        "schema": "melee-web-retail-input-plan", "version": 1,
        "policy": "dolphin-pipe-raw-v2", "source_sha256": source_sha,
        "first_frame": -123, "source_stage": 31, "source_characters": [2, 20],
        "frames": [["00" * 11, "00" * 11] for _ in range(3)],
    }
    plan = root / "input-plan.json"
    plan.write_text(json.dumps(input_plan), encoding="utf-8")
    plan_sha = hashlib.sha256(plan.read_bytes()).hexdigest()
    entry = {
        "name": "dev-a", "source": str(source.relative_to(root)),
        "source_sha256": source_sha, "plan": str(plan.relative_to(root)),
        "plan_sha256": plan_sha, "role": "development", "expected_setup": expected_setup(),
    }
    execution = {
        "schema": VALIDATION.EXECUTION_SCHEMA, "version": 1,
        "candidate_manifest_sha256": "d" * 64,
        "selected_before_reference_execution": [entry],
    }
    execution_path = root / "execution-plan-v3.json"
    execution_path.write_text(json.dumps(execution), encoding="utf-8")
    prefix = dict(input_plan)
    prefix["frames"] = prefix["frames"][:1]
    prefix_sha = hashlib.sha256(
        (json.dumps(prefix, sort_keys=True, separators=(",", ":")) + "\n").encode()).hexdigest()
    capture = SimpleNamespace(
        header={"provenance": {"input_plan_sha256": prefix_sha}},
        match_enter={"start_melee_hex": setup_bytes().hex()},
        frames=[{"consumed_inputs": [["00" * 11, "00" * 11, DISCONNECTED_PAD, DISCONNECTED_PAD]]}],
        sha256="c" * 64,
    )
    return execution_path, source, plan, entry, capture


class RetailSetupValidationTests(unittest.TestCase):
    def test_unsupported_source_profile_fields_are_rejected(self):
        mutations = (
            ("is_vs", lambda raw: raw.__setitem__(4, raw[4] & ~0x40),
             "ordinary VS is_vs"),
            ("timer_shows_hours", lambda raw: raw.__setitem__(1, raw[1] | 0x02),
             "timer_shows_hours"),
            ("friendly_fire", lambda raw: raw.__setitem__(1, raw[1] | 0x01),
             "friendly_fire"),
            ("timer_subframe", lambda raw: raw.__setitem__(0x14, 1),
             "timer subframe"),
            ("camera_scale", lambda raw: raw.__setitem__(slice(0x2C, 0x30),
                                                           bytes.fromhex("3f000000")),
             "camera scale"),
            ("callback_pointer", lambda raw: raw.__setitem__(0x38, 1),
             "callback/data pointer"),
        )
        for name, mutate, message in mutations:
            with self.subTest(name=name):
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    execution, source, plan, entry, capture = write_inputs(root)
                    changed = bytearray(setup_bytes())
                    mutate(changed)
                    capture.match_enter["start_melee_hex"] = changed.hex()
                    with patch.object(VALIDATION, "load_capture", return_value=capture):
                        with self.assertRaisesRegex(VALIDATION.SetupValidationError, message):
                            VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl",
                                                      repo_root=root)

    def test_named_development_setup_matches_and_binds_all_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            execution, source, plan, entry, capture = write_inputs(root)
            with patch.object(VALIDATION, "load_capture", return_value=capture):
                result = VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl",
                                                   repo_root=root)
            self.assertEqual(result["status"], "pass")
            self.assertEqual(result["source_sha256"], entry["source_sha256"])
            self.assertEqual(result["input_plan_sha256"], entry["plan_sha256"])
            self.assertNotEqual(result["capture_input_plan_sha256"], result["input_plan_sha256"])
            self.assertEqual(result["execution_plan_sha256"],
                             hashlib.sha256(execution.read_bytes()).hexdigest())
            self.assertEqual(result["capture_sha256"], "c" * 64)
            self.assertEqual(result["setup"]["actual"], result["setup"]["expected"])

    def test_first_setup_mismatch_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            execution, source, plan, entry, capture = write_inputs(root)
            changed = bytearray(setup_bytes()); changed[0x0E:0x10] = (8).to_bytes(2, "big")
            capture.match_enter["start_melee_hex"] = changed.hex()
            with patch.object(VALIDATION, "load_capture", return_value=capture):
                with self.assertRaisesRegex(VALIDATION.SetupValidationError, "setup metadata"):
                    VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)

    def test_unknown_expected_field_rejects_before_capture_read(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            execution, source, plan, entry, capture = write_inputs(root)
            execution_value = json.loads(execution.read_text())
            execution_value["selected_before_reference_execution"][0]["expected_setup"]["unexpected"] = 1
            execution.write_text(json.dumps(execution_value), encoding="utf-8")
            with patch.object(VALIDATION, "load_capture") as load:
                with self.assertRaisesRegex(VALIDATION.SetupValidationError, "unrecognized"):
                    VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)
            load.assert_not_called()

    def test_source_hash_mismatch_rejects_before_capture_read(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            execution, source, plan, entry, capture = write_inputs(root)
            source.write_bytes(b"changed donor bytes")
            with patch.object(VALIDATION, "load_capture") as load:
                with self.assertRaisesRegex(VALIDATION.SetupValidationError, "donor source SHA-256"):
                    VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)
            load.assert_not_called()

    def test_mutated_capture_prefix_input_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            execution, source, plan, entry, capture = write_inputs(root)
            capture.frames[0]["consumed_inputs"][0][0] = "01" + "00" * 10
            with patch.object(VALIDATION, "load_capture", return_value=capture):
                with self.assertRaisesRegex(VALIDATION.SetupValidationError, "prefix mismatch"):
                    VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)

    def test_capture_prefix_metadata_and_hash_must_match(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            execution, source, plan, entry, capture = write_inputs(root)
            changed = bytearray(setup_bytes()); changed[0x0E:0x10] = (8).to_bytes(2, "big")
            capture.match_enter["start_melee_hex"] = changed.hex()
            with patch.object(VALIDATION, "load_capture", return_value=capture):
                with self.assertRaisesRegex(VALIDATION.SetupValidationError, "characters/stage"):
                    VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)
            capture.match_enter["start_melee_hex"] = setup_bytes().hex()
            capture.header["provenance"]["input_plan_sha256"] = "e" * 64
            with patch.object(VALIDATION, "load_capture", return_value=capture):
                with self.assertRaisesRegex(VALIDATION.SetupValidationError, "exact frozen-plan prefix"):
                    VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)

    def test_oversized_execution_plan_is_rejected_before_capture_read(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            execution, source, plan, entry, capture = write_inputs(root)
            with execution.open("ab") as stream:
                stream.write(b" " * (VALIDATION.MAX_EXECUTION_PLAN_BYTES + 1))
            with patch.object(VALIDATION, "load_capture") as load:
                with self.assertRaisesRegex(VALIDATION.SetupValidationError, "exceeds"):
                    VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)
            load.assert_not_called()

    def test_non_development_role_and_capture_plan_binding_reject(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            execution, source, plan, entry, capture = write_inputs(root)
            execution_value = json.loads(execution.read_text())
            execution_value["selected_before_reference_execution"][0]["role"] = "heldout"
            execution.write_text(json.dumps(execution_value), encoding="utf-8")
            with patch.object(VALIDATION, "load_capture") as load:
                with self.assertRaisesRegex(VALIDATION.SetupValidationError, "not a development"):
                    VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)
            load.assert_not_called()

            execution_value["selected_before_reference_execution"][0]["role"] = "development"
            execution.write_text(json.dumps(execution_value), encoding="utf-8")
            capture.header["provenance"]["input_plan_sha256"] = "e" * 64
            with patch.object(VALIDATION, "load_capture", return_value=capture):
                with self.assertRaisesRegex(VALIDATION.SetupValidationError, "exact frozen-plan prefix"):
                    VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)


if __name__ == "__main__":
    unittest.main()
