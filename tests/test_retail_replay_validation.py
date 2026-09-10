"""Strict schema, repeatability, and first-divergence controls for retail candidates."""

from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "retail_replay_validation", ROOT / "tools" / "retail_replay_validation.py")
VALIDATION = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(VALIDATION)

_capture_counter = 0


def _new_capture_id():
    global _capture_counter
    capture_id = "00000000000040008000000000000000"
    capture_id = capture_id[:-4] + f"{_capture_counter:04x}"
    _capture_counter += 1
    return capture_id


def _fighter(slot):
    return {
        "slot": slot,
        "kind": 0,
        "motion": 14,
        "animation": 1,
        "facing_bits": "3f800000",
        "position_bits": ["00000000", "3f800000", "00000000"],
        "velocity_bits": ["00000000", "00000000", "00000000"],
        "knockback_bits": ["00000000", "00000000", "00000000"],
        "ground_air": 0,
        "animation_frame_bits": "00000000",
        "animation_speed_bits": "3f800000",
        "damage_bits": "00000000",
        "shield_bits": "00000000",
        "stocks": 4,
        "input_hex": "00" * 0x6C,
    }


def _state(scene_frame, match_frame, *, rng=1234):
    return {
        "rng": rng,
        "scene_frame": scene_frame,
        "match_frame": match_frame,
        "fighters": [_fighter(0), _fighter(1)],
    }


def candidate(frame_count=3, capture_id=None):
    if capture_id is None:
        capture_id = _new_capture_id()
    provenance = dict(VALIDATION.EXPECTED_PROVENANCE)
    provenance.update({
        "dolphin_binary_sha256": "aa" * 32,
        "Dolphin.ini_sha256": "bb" * 32,
        "GCPadNew.ini_sha256": "cc" * 32,
    })
    rows = [{
        "record": "header",
        "schema": VALIDATION.SCHEMA,
        "version": VALIDATION.VERSION,
        "phase": VALIDATION.PHASE,
        "input_phase": VALIDATION.INPUT_PHASE,
        "initial_phase": VALIDATION.INITIAL_PHASE,
        "game_revision": VALIDATION.GAME_REVISION,
        "frames_requested": frame_count,
        "provenance": provenance,
        "collector_sha256": "dd" * 32,
        "writes_game_state": False,
        "capture_id": capture_id,
    }, {
        "record": "match_enter",
        "rng": 1234,
        "start_melee_hex": "01" * 0x138,
        "pad_lib_hex": "02" * 0x20,
        "pad_master_hex": "03" * 0x110,
        "pad_game_hex": "04" * 0x110,
    }, {
        "record": "match_enter_complete",
        **_state(999, 0),
    }]
    for index in range(frame_count):
        rows.append({
            "record": "frame",
            "index": index,
            "consumed_inputs": [[
                "10" * 11,
                "20" * 11,
                "30" * 11,
                "40" * 11,
            ]],
            **_state(index, 0 if index < 2 else 1, rng=1234 + index + 1),
        })
    rows.append({"record": "end", "frames": frame_count, "status": "captured"})
    return rows


class RetailReplayValidationTests(unittest.TestCase):
    def test_schema_types_and_match_entry_origin_are_not_inferred(self):
        for version in (True, 1.0):
            rows = candidate()
            rows[0]["version"] = version
            self.assertEqual(VALIDATION.compare(rows, candidate())["status"], "invalid_capture")
        rows = candidate()
        for row in rows[3:-1]:
            row["scene_frame"] += 100
        result = VALIDATION.compare(rows, candidate())
        self.assertEqual(result["status"], "invalid_capture")
        self.assertIn("first scene_frame must be zero", result["invalid_capture"]["error"])

    def test_control_is_repeatable_and_reports_scope(self):
        result = VALIDATION.compare(candidate(), candidate())
        self.assertEqual(result["status"], "repeatable")
        self.assertTrue(result["repeatable"])
        self.assertEqual(result["frames_compared"], 3)
        self.assertIn("port equivalence", result["scope"])
        self.assertIn("scene_frame", result["schema_note"])

    def test_initial_historical_scene_frame_is_ignored(self):
        left = candidate()
        right = deepcopy(left)
        right[0]["capture_id"] = _new_capture_id()
        right[2]["scene_frame"] = 0x12345678
        self.assertEqual(VALIDATION.compare(left, right)["status"], "repeatable")

    def test_initial_match_frame_and_rng_are_compared(self):
        left = candidate()
        right = deepcopy(left)
        right[0]["capture_id"] = _new_capture_id()
        right[2]["match_frame"] = 1
        result = VALIDATION.compare(left, right)
        self.assertEqual(result["status"], "diverged")
        self.assertEqual(result["first_divergence"]["record"], "match_enter_complete")
        self.assertIn("match_frame", result["first_divergence"]["field"])

    def test_real_control_mismatch_reports_first_frame_and_field(self):
        left = candidate()
        right = deepcopy(left)
        right[0]["capture_id"] = _new_capture_id()
        right[5]["fighters"][1]["motion"] = 44
        result = VALIDATION.compare(left, right)
        self.assertEqual(result["status"], "diverged")
        first = result["first_divergence"]
        self.assertEqual(first["record"], "frame")
        self.assertEqual(first["frame"], 2)
        self.assertIn("fighters[1].motion", first["field"])
        self.assertEqual(first["expected"], 14)
        self.assertEqual(first["actual"], 44)

    def test_consumed_input_mismatch_preserves_stale_error_bytes(self):
        left = candidate()
        right = deepcopy(left)
        right[0]["capture_id"] = _new_capture_id()
        # The last byte is intentionally an error/stale member.  It is compared
        # as captured rather than replaced by a disconnected-port default.
        right[4]["consumed_inputs"][0][2] = ("30" * 10) + "31"
        result = VALIDATION.compare(left, right)
        self.assertEqual(result["status"], "diverged")
        self.assertEqual(result["first_divergence"]["frame"], 1)
        self.assertIn("consumed_inputs[0][2]", result["first_divergence"]["field"])
        self.assertEqual(result["checks"]["consumed_inputs"], "fail")

    def test_missing_field_is_invalid_capture(self):
        rows = candidate()
        del rows[4]["fighters"][0]["damage_bits"]
        result = VALIDATION.compare(rows, candidate())
        self.assertEqual(result["status"], "invalid_capture")
        self.assertEqual(result["invalid_capture"]["capture"], "a")
        self.assertIn("damage_bits", result["invalid_capture"]["error"])

    def test_truncated_capture_is_invalid_capture(self):
        result = VALIDATION.compare(candidate()[:-2], candidate())
        self.assertEqual(result["status"], "invalid_capture")
        self.assertIn("expected header", result["invalid_capture"]["error"])

    def test_reordered_records_are_invalid_capture(self):
        rows = candidate()
        rows[3], rows[4] = rows[4], rows[3]
        result = VALIDATION.compare(rows, candidate())
        self.assertEqual(result["status"], "invalid_capture")
        self.assertIn("expected 0, got 1", result["invalid_capture"]["error"])

    def test_stale_end_count_is_invalid_capture(self):
        rows = candidate()
        rows[-1]["frames"] = 2
        result = VALIDATION.compare(rows, candidate())
        self.assertEqual(result["status"], "invalid_capture")
        self.assertIn("end.frames", result["invalid_capture"]["error"])

    def test_extra_input_vector_is_invalid_capture(self):
        rows = candidate()
        rows[3]["consumed_inputs"].append(rows[3]["consumed_inputs"][0])
        result = VALIDATION.compare(rows, candidate())
        self.assertEqual(result["status"], "invalid_capture")
        self.assertIn("exactly one consumed input", result["invalid_capture"]["error"])

    def test_padding_byte_is_invalid_capture(self):
        rows = candidate()
        rows[3]["consumed_inputs"][0][0] += "00"
        result = VALIDATION.compare(rows, candidate())
        self.assertEqual(result["status"], "invalid_capture")
        self.assertIn("11 bytes", result["invalid_capture"]["error"])

    def test_nonfinite_float_bits_are_invalid_capture(self):
        rows = candidate()
        rows[4]["fighters"][0]["damage_bits"] = "7f800000"
        result = VALIDATION.compare(rows, candidate())
        self.assertEqual(result["status"], "invalid_capture")
        self.assertIn("non-finite", result["invalid_capture"]["error"])

    def test_scene_frame_gap_is_invalid_capture(self):
        rows = candidate()
        rows[5]["scene_frame"] += 2
        result = VALIDATION.compare(rows, candidate())
        self.assertEqual(result["status"], "invalid_capture")
        self.assertIn("scene_frame is not contiguous", result["invalid_capture"]["error"])

    def test_wrong_pinned_provenance_is_invalid_capture(self):
        rows = candidate()
        rows[0]["provenance"]["cheats"] = True
        result = VALIDATION.compare(rows, candidate())
        self.assertEqual(result["status"], "invalid_capture")
        self.assertIn("cheats", result["invalid_capture"]["error"])

    def test_pinned_provenance_types_are_strict(self):
        rows = candidate()
        rows[0]["provenance"]["cheats"] = 0
        result = VALIDATION.compare(rows, candidate())
        self.assertEqual(result["status"], "invalid_capture")
        self.assertIn("cheats", result["invalid_capture"]["error"])

    def test_same_capture_id_is_invalid_independence_evidence(self):
        rows = candidate()
        result = VALIDATION.compare(rows, deepcopy(rows))
        self.assertEqual(result["status"], "invalid_capture")
        self.assertIn("same capture_id", result["invalid_capture"]["error"])

    def test_file_hashes_are_reported_without_affecting_semantic_result(self):
        with tempfile.TemporaryDirectory() as directory:
            first_path = Path(directory) / "first.jsonl"
            second_path = Path(directory) / "second.jsonl"
            first_text = "\n".join(json.dumps(row, sort_keys=True, separators=(",", ":"))
                                 for row in candidate()) + "\n"
            # Whitespace is a file-level difference, while the decoded capture
            # remains the same candidate and therefore remains repeatable.
            second_text = "\n".join(json.dumps(row, sort_keys=True, separators=(", ", ": "))
                                  for row in candidate()) + "\n"
            first_path.write_text(first_text)
            second_path.write_text(second_text)
            result = VALIDATION.compare(first_path, second_path)
            self.assertEqual(result["status"], "repeatable")
            self.assertEqual(result["capture_a_sha256"], hashlib.sha256(first_text.encode()).hexdigest())
            self.assertEqual(result["capture_b_sha256"], hashlib.sha256(second_text.encode()).hexdigest())
            self.assertEqual(result["captures"]["a"]["provenance"]["cheats"], False)

    def test_cli_returns_nonzero_and_writes_machine_report(self):
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "first.jsonl"
            second = Path(directory) / "second.jsonl"
            report = Path(directory) / "report.json"
            first.write_text("\n".join(json.dumps(row) for row in candidate()) + "\n")
            changed = candidate()
            changed[4]["fighters"][0]["motion"] = 99
            second.write_text("\n".join(json.dumps(row) for row in changed) + "\n")
            command = [sys.executable, str(ROOT / "scripts" / "compare_retail_replays.py"),
                       str(first), str(second), "--output", str(report)]
            completed = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(completed.returncode, 1)
            output = json.loads(completed.stdout)
            self.assertEqual(output["status"], "diverged")
            self.assertEqual(json.loads(report.read_text())["status"], "diverged")


if __name__ == "__main__":
    unittest.main()
