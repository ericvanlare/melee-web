"""Focused tests for the exact development setup gate."""

import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from contextlib import redirect_stderr

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
    def _holdout_inputs(self, root, mutate=None):
        execution, source, plan, entry, capture = write_inputs(root)
        manifest = {
            "schema": "melee-web-vanilla-candidate-split-v2",
            "status": "frozen_before_runtime",
            "selection": {"held_out_reserved_sha256": [entry["source_sha256"]],
                          "development_sha256": []},
            "records": [{"source_sha256": entry["source_sha256"],
                         "role": "held_out_reserved",
                         "plan_receipt": {"source_sha256": entry["source_sha256"],
                                          "sha256": entry["plan_sha256"]}}],
        }
        if mutate:
            mutate(manifest)
        reservation = root / "reservation.json"
        reservation.write_text(json.dumps(manifest))
        value = json.loads(execution.read_text())
        value["schema"] = VALIDATION.HOLDOUT_EXECUTION_SCHEMA
        value["selected_before_reference_execution"][0]["role"] = "holdout"
        value["candidate_manifest_path"] = str(reservation.relative_to(root))
        value["candidate_manifest_sha256"] = hashlib.sha256(reservation.read_bytes()).hexdigest()
        execution.write_text(json.dumps(value))
        return execution, capture, reservation

    def test_reserved_holdout_keeps_role_and_exact_setup_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            execution, capture, _ = self._holdout_inputs(root)
            with patch.object(VALIDATION, "load_capture", return_value=capture):
                result = VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)
            self.assertEqual(result["entry"]["role"], "holdout")
            self.assertEqual(result["setup"]["actual"], expected_setup())
            changed = bytearray(setup_bytes()); changed[0x63] = 3
            capture.match_enter["start_melee_hex"] = changed.hex()
            with patch.object(VALIDATION, "load_capture", return_value=capture):
                with self.assertRaisesRegex(VALIDATION.SetupValidationError, "costume"):
                    VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)

    def test_holdout_cannot_relabel_a_development_or_different_input(self):
        changes = [
            (lambda m: m["selection"]["development_sha256"].extend(m["selection"]["held_out_reserved_sha256"]), "exclusively reserved"),
            (lambda m: m["records"][0].update(role="development"), "reservation record"),
            (lambda m: m["records"][0]["plan_receipt"].update(sha256="f" * 64), "reserved identity"),
            (lambda m: m["selection"].update(held_out_reserved_sha256=m["selection"]["held_out_reserved_sha256"][0]), "exclusively reserved"),
        ]
        for mutate, error in changes:
            with self.subTest(error=error), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                execution, _, _ = self._holdout_inputs(root, mutate)
                with patch.object(VALIDATION, "load_capture") as load:
                    with self.assertRaisesRegex(VALIDATION.SetupValidationError, error):
                        VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)
                load.assert_not_called()

    def test_holdout_rejects_changed_reservation_before_capture_read(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            execution, _, reservation = self._holdout_inputs(root)
            reservation.write_text(reservation.read_text() + "\n")
            with patch.object(VALIDATION, "load_capture") as load:
                with self.assertRaisesRegex(VALIDATION.SetupValidationError, "manifest SHA-256"):
                    VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)
            load.assert_not_called()

    def test_holdout_rejects_mixed_roles_and_duplicate_names_before_capture(self):
        for extra, error in (([{"name": "dev-b", "role": "development"}], "only holdout"),
                             ([{"name": "b", "role": "holdout"}] * 2, "unique")):
            with self.subTest(error=error), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                execution, _, _ = self._holdout_inputs(root)
                value = json.loads(execution.read_text())
                value["selected_before_reference_execution"].extend(extra)
                execution.write_text(json.dumps(value))
                with patch.object(VALIDATION, "load_capture") as load:
                    with self.assertRaisesRegex(VALIDATION.SetupValidationError, error):
                        VALIDATION.validate_setup(execution, "dev-a", "capture.jsonl", repo_root=root)
                load.assert_not_called()

    def test_setup_cli_cannot_overwrite_holdout_reservation(self):
        spec = importlib.util.spec_from_file_location(
            "holdout_setup_cli", ROOT / "scripts/check_retail_setup.py")
        cli = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cli)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            execution, _, reservation = self._holdout_inputs(root)
            original = reservation.read_bytes()
            with patch.object(cli, "validate_setup", return_value={"status": "pass"}), redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as stopped:
                    cli.main(["--plan", str(execution), "--name", "dev-a",
                              "--capture", str(root / "capture.jsonl"),
                              "--repo-root", str(root), "--output", str(reservation)])
            self.assertEqual(stopped.exception.code, 2)
            self.assertEqual(reservation.read_bytes(), original)

    def test_decode_accepts_ordinary_vs_cpu_and_keeps_human_shape(self):
        raw = setup_bytes()
        raw[0x85] = 1
        raw[0x92] = 4
        raw[0x93] = 9
        actual = VALIDATION._decode_setup(raw.hex())
        self.assertEqual(actual["players"][0], expected_setup()["players"][0])
        self.assertEqual(actual["players"][1], {
            "port": 2, "character_kind": 20, "costume": 2, "stocks": 4,
            "player_type": 1, "rumble_enabled": False,
            "cpu_kind": 4, "cpu_level": 9,
        })

        expected = expected_setup()
        expected["players"][1].update({"player_type": 1, "cpu_kind": 4, "cpu_level": 9})
        self.assertEqual(VALIDATION._validate_expected_setup(expected)["players"][1],
                         actual["players"][1])

    def test_decode_rejects_non_ordinary_cpu_kind_or_level(self):
        for offset, value, message in ((0x92, 0, "ordinary-VS CPU"),
                                        (0x93, 10, "ordinary-VS CPU"),
                                        (0x90, 0x80, "CPU rumble disabled")):
            with self.subTest(offset=hex(offset)):
                raw = setup_bytes()
                raw[0x85] = 1
                raw[0x92] = 4
                raw[0x93] = 9
                raw[offset] = value
                with self.assertRaisesRegex(VALIDATION.SetupValidationError, message):
                    VALIDATION._decode_setup(raw.hex())

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
                with self.assertRaisesRegex(VALIDATION.SetupValidationError, "only development"):
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
