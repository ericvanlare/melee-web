"""MWRC v1 export and independent-candidate controls."""

from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


VALIDATION = _load("retail_validation_for_recipe_tests",
                   ROOT / "tools" / "retail_replay_validation.py")
sys.path.insert(0, str(ROOT / "tools"))
RECIPE = _load("retail_replay_recipe", ROOT / "tools" / "retail_replay_recipe.py")

_capture_counter = 0


def _new_capture_id():
    global _capture_counter
    value = "00000000000040008000000000000000"
    value = value[:-4] + f"{_capture_counter:04x}"
    _capture_counter += 1
    return value


def _fighter(slot):
    return {
        "slot": slot, "kind": 0, "motion": 14, "animation": 1,
        "facing_bits": "3f800000",
        "position_bits": ["00000000", "3f800000", "00000000"],
        "velocity_bits": ["00000000", "00000000", "00000000"],
        "knockback_bits": ["00000000", "00000000", "00000000"],
        "ground_air": 0, "animation_frame_bits": "00000000",
        "animation_speed_bits": "3f800000", "damage_bits": "00000000",
        "shield_bits": "00000000", "stocks": 4, "input_hex": "00" * 0x6C,
    }


def _state(scene, match, rng):
    return {"rng": rng, "scene_frame": scene, "match_frame": match,
            "fighters": [_fighter(0), _fighter(1)]}


def candidate(frame_count=3, capture_id=None):
    if capture_id is None:
        capture_id = _new_capture_id()
    rows = [{
        "record": "header", "schema": VALIDATION.SCHEMA,
        "version": VALIDATION.VERSION, "phase": VALIDATION.PHASE,
        "input_phase": VALIDATION.INPUT_PHASE,
        "initial_phase": VALIDATION.INITIAL_PHASE,
        "game_revision": VALIDATION.GAME_REVISION,
        "frames_requested": frame_count,
        "provenance": dict(VALIDATION.EXPECTED_PROVENANCE),
        "collector_sha256": "dd" * 32, "writes_game_state": False,
        "capture_id": capture_id,
    }, {
        "record": "match_enter", "rng": 0x12345678,
        "start_melee_hex": "01" * 0x138,
        "pad_lib_hex": "02" * 0x20,
        "pad_master_hex": "03" * 0x110,
        "pad_game_hex": "04" * 0x110,
    }, {"record": "match_enter_complete", **_state(999, 0, 0x12345678)}]
    for index in range(frame_count):
        rows.append({
            "record": "frame", "index": index,
            "consumed_inputs": [["10" * 11, "20" * 11,
                                  "30" * 11, "40" * 11]],
            **_state(index, 0 if index < 2 else 1, 0x12345679 + index),
        })
    rows.append({"record": "end", "frames": frame_count, "status": "captured"})
    return rows


def write_candidate(path, rows):
    path.write_text("\n".join(json.dumps(row, sort_keys=True,
                                         separators=(",", ":"))
                               for row in rows) + "\n")


class RetailReplayRecipeTests(unittest.TestCase):
    def test_mwrc_header_setup_and_exact_input_payload(self):
        first = candidate(3)
        second = candidate(3)
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            first_path, second_path = directory / "a.jsonl", directory / "b.jsonl"
            output = directory / "recipe.mwrc"
            sidecar = directory / "recipe.sidecar.json"
            write_candidate(first_path, first)
            write_candidate(second_path, second)
            result = RECIPE.export_pair(first_path, second_path, output, sidecar)
            payload = output.read_bytes()
            magic, version, seed, frames = struct.unpack_from(">4sIII", payload)
            self.assertEqual((magic, version, seed, frames),
                             (b"MWRC", 1, 0x12345678, 3))
            self.assertEqual(payload[16:16 + 0x138], bytes.fromhex(
                first[1]["start_melee_hex"]))
            inputs = b"".join(bytes.fromhex(raw)
                             for frame in first[3:-1]
                             for raw in frame["consumed_inputs"][0])
            self.assertEqual(payload[16 + 0x138:], inputs)
            self.assertEqual(len(payload), 16 + 0x138 + 3 * 44)
            self.assertEqual(result["input_sha256"], hashlib.sha256(inputs).hexdigest())
            self.assertEqual(result["transport"]["pad_bytes_per_port"], 11)

    def test_sidecar_contains_capture_hashes_and_no_admission_claim(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            first_path, second_path = directory / "a.jsonl", directory / "b.jsonl"
            output, sidecar = directory / "out", directory / "sidecar"
            write_candidate(first_path, candidate())
            write_candidate(second_path, candidate())
            data = RECIPE.export_pair(first_path, second_path, output, sidecar)
            self.assertEqual(data["captures"]["a"]["sha256"],
                             hashlib.sha256(first_path.read_bytes()).hexdigest())
            self.assertEqual(data["captures"]["b"]["sha256"],
                             hashlib.sha256(second_path.read_bytes()).hexdigest())
            self.assertEqual(data["claims"]["reference_repeatability"], "pass")
            self.assertEqual(data["claims"]["port_equivalence"], "not_claimed")
            self.assertEqual(data["claims"]["performance_acceptance"], "not_claimed")
            self.assertEqual(json.loads(sidecar.read_text())["status"], "exported")

    def test_same_path_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.jsonl"
            write_candidate(path, candidate())
            with self.assertRaisesRegex(RECIPE.RecipeError, "same path"):
                RECIPE.export_pair(path, path, Path(directory) / "out")

    def test_identical_bytes_are_rejected_even_before_comparison(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            first = directory / "a.jsonl"
            second = directory / "b.jsonl"
            write_candidate(first, candidate())
            second.write_bytes(first.read_bytes())
            with self.assertRaisesRegex(RECIPE.RecipeError, "identical file bytes"):
                RECIPE.export_pair(first, second, directory / "out")

    def test_same_capture_id_is_rejected_by_repeatability_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            first = directory / "a.jsonl"
            second = directory / "b.jsonl"
            rows = candidate()
            write_candidate(first, rows)
            duplicate = deepcopy(rows)
            # A distinct file with the same ID demonstrates the validator's
            # explicit execution-identity requirement.
            duplicate[1]["rng"] += 1
            write_candidate(second, duplicate)
            with self.assertRaisesRegex(RECIPE.RecipeError, "same capture_id"):
                RECIPE.export_pair(first, second, directory / "out")

    def test_divergence_is_rejected_at_first_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            first = directory / "a.jsonl"
            second = directory / "b.jsonl"
            write_candidate(first, candidate())
            changed = candidate()
            changed[5]["fighters"][1]["motion"] = 99
            write_candidate(second, changed)
            with self.assertRaisesRegex(RECIPE.RecipeError, "first divergence"):
                # Error detail is machine-readable JSON and contains frame 2;
                # use the full message below to keep the expected location clear.
                RECIPE.export_pair(first, second, directory / "out")

    def test_cli_returns_nonzero_for_nonrepeatable_pair(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            first, second = directory / "a.jsonl", directory / "b.jsonl"
            write_candidate(first, candidate())
            changed = candidate()
            changed[4]["consumed_inputs"][0][2] = "30" * 10 + "31"
            write_candidate(second, changed)
            command = [sys.executable, str(ROOT / "scripts" / "export_retail_replay.py"),
                       str(first), str(second), "--output", str(directory / "out")]
            completed = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(completed.returncode, 2)
            self.assertIn("not repeatable", completed.stderr)


if __name__ == "__main__":
    unittest.main()
