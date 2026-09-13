"""Focused tests for the bounded retail match-completion sidecar."""

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from retail_replay_validation import EXPECTED_PROVENANCE, load_capture
from retail_match_completion import (
    EXIT_PHASE,
    FINAL_EXIT_CALLER,
    CaptureError,
    load_match_completion,
)


def _fighter(slot: int, stocks: int) -> dict:
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
        "stocks": stocks,
        "input_hex": "00" * 0x6C,
    }


def _state(scene_frame: int, match_frame: int, stocks=(4, 4), *, rng=1234) -> dict:
    return {
        "rng": rng,
        "scene_frame": scene_frame,
        "match_frame": match_frame,
        "fighters": [_fighter(0, stocks[0]), _fighter(1, stocks[1])],
    }


def _capture_rows(frame_count=3, final_stocks=(0, 2)) -> list[dict]:
    rows = [{
        "record": "header",
        "schema": "melee-web-retail-replay-candidate",
        "version": 1,
        "phase": "HSD_GObj_80390CFC_return",
        "input_phase": "HSD_PadRenewMasterStatus_entry_queue",
        "initial_phase": "gm_Scene_Vs_OnEnter_entry",
        "game_revision": "GALE01r2",
        "frames_requested": frame_count,
        "provenance": dict(EXPECTED_PROVENANCE),
        "collector_sha256": "dd" * 32,
        "writes_game_state": False,
        "capture_id": "00000000000040008000000000000001",
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
        stocks = final_stocks if index == frame_count - 1 else (4, 4)
        rows.append({
            "record": "frame",
            "index": index,
            "consumed_inputs": [["10" * 11, "20" * 11, "30" * 11, "40" * 11]],
            **_state(index, 0 if index < 2 else 1, stocks, rng=1234 + index + 1),
        })
    rows.append({"record": "end", "frames": frame_count, "status": "captured"})
    return rows


def _write_capture(directory: Path, *, final_stocks=(0, 2), frame_count=3):
    path = directory / "capture.jsonl"
    with path.open("w", encoding="utf-8", newline="") as stream:
        for row in _capture_rows(frame_count, final_stocks):
            stream.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")
    return path, load_capture(path)


def _write_timeout_capture(directory: Path, *, final_stocks=(2, 2)):
    rows = _capture_rows(frame_count=3, final_stocks=final_stocks)
    setup = bytearray.fromhex(rows[1]["start_melee_hex"])
    setup[0] = 0x22  # stock match with a one-minute countdown timer
    setup[0x10:0x14] = (60).to_bytes(4, "big")
    rows[1]["start_melee_hex"] = setup.hex()
    path = directory / "timeout-capture.jsonl"
    with path.open("w", encoding="utf-8", newline="") as stream:
        for row in rows:
            stream.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")
    return path, load_capture(path)


def _sidecar(capture, **overrides) -> dict:
    value = {
        "schema": "melee-web-retail-match-completion",
        "version": 1,
        "capture_sha256": capture.sha256,
        "frames": len(capture.frames),
        "phase": "after_final_source_draw",
        "scene_request": 1,
        "match_end_state": 3,
        "match_result": 2,
        "final_draw_source_index": len(capture.frames) - 1,
        "exit_observation": {
            "phase": EXIT_PHASE,
            "index": len(capture.frames) - 1,
            "caller": FINAL_EXIT_CALLER,
            "scene_request": 1,
        },
    }
    value.update(overrides)
    return value


def _write_sidecar(path: Path, value: dict, *, suffix="") -> Path:
    target = path.with_name(path.stem + suffix + path.suffix)
    target.write_text(json.dumps(value, sort_keys=True, separators=(",", ":")), encoding="utf-8")
    return target


class RetailMatchCompletionTests(unittest.TestCase):
    def test_complete_elimination_is_strictly_gated_and_hash_bound(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            _, capture = _write_capture(directory)
            path = _write_sidecar(directory / "completion.json", _sidecar(capture))
            report = load_match_completion(capture, path, require_complete=True)
            self.assertEqual(report["status"], "source_match_complete")
            self.assertEqual(report["capture_sha256"], capture.sha256)
            self.assertEqual(report["completion_sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertFalse(report["gold_admitted"])
            self.assertFalse(report["content_admitted"])
            self.assertEqual(report["port_equivalence"], "not_evaluated")
            self.assertEqual(report["performance"], "not_evaluated")
            self.assertIn("no port equivalence", report["scope"])

    def test_scheduler_observation_is_a_bounded_prefix(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            _, capture = _write_capture(directory)
            value = _sidecar(capture, phase="after_final_scheduler",
                             final_draw_source_index=None, exit_observation=None)
            path = _write_sidecar(directory / "completion.json", value)
            report = load_match_completion(capture, path)
            self.assertEqual(report["status"], "bounded_prefix")
            with self.assertRaises(CaptureError):
                load_match_completion(capture, path, require_complete=True)

    def test_exit_after_final_draw_retains_one_past_scheduler_index(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            _, capture = _write_capture(directory)
            exit_observation = {
                "phase": EXIT_PHASE,
                "index": len(capture.frames),
                "caller": FINAL_EXIT_CALLER,
                "scene_request": 1,
            }
            path = _write_sidecar(directory / "completion.json",
                                  _sidecar(capture, exit_observation=exit_observation))
            report = load_match_completion(capture, path, require_complete=True)
            self.assertEqual(report["status"], "source_match_complete")
            self.assertEqual(report["exit_observation"]["index"], len(capture.frames))

    def test_one_past_exit_index_is_only_valid_after_a_final_draw(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            _, capture = _write_capture(directory)
            value = _sidecar(
                capture,
                phase="after_final_scheduler",
                final_draw_source_index=None,
                exit_observation={
                    "phase": EXIT_PHASE,
                    "index": len(capture.frames),
                    "caller": FINAL_EXIT_CALLER,
                    "scene_request": 1,
                },
            )
            path = _write_sidecar(directory / "completion.json", value)
            with self.assertRaises(CaptureError):
                load_match_completion(capture, path)

    def test_source_timeout_is_complete_even_when_stocks_are_tied(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            _, capture = _write_timeout_capture(directory)
            value = _sidecar(capture, match_result=1)
            path = _write_sidecar(directory / "completion.json", value)
            report = load_match_completion(capture, path, require_complete=True)
            self.assertEqual(report["status"], "source_match_complete")
            self.assertEqual(report["ending"]["mode"], "timeout")

    def test_timeout_result_without_source_timer_is_not_complete(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            _, capture = _write_capture(directory, final_stocks=(2, 2))
            path = _write_sidecar(directory / "completion.json",
                                  _sidecar(capture, match_result=1))
            report = load_match_completion(capture, path)
            self.assertEqual(report["status"], "bounded_prefix")

    def test_capture_hash_and_frame_count_bind_the_sidecar(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            _, capture = _write_capture(directory)
            for replacement in (
                {"capture_sha256": "0" * 64},
                {"frames": len(capture.frames) + 1},
            ):
                path = _write_sidecar(directory / "completion.json", _sidecar(capture, **replacement),
                                       suffix="-bad" + str(len(replacement)))
                with self.assertRaises(CaptureError):
                    load_match_completion(capture, path)

    def test_exact_keys_duplicate_keys_and_strict_types_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            _, capture = _write_capture(directory)
            base = _sidecar(capture)
            malformed = [
                (dict(base, extra=True), "unknown key"),
                ({key: value for key, value in base.items() if key != "phase"}, "missing key"),
                (dict(base, version=True), "boolean version"),
                (dict(base, scene_request=True), "boolean request"),
                (dict(base, match_end_state=256), "end-state byte"),
                (dict(base, final_draw_source_index=len(capture.frames)), "draw index"),
                (dict(base, exit_observation={**base["exit_observation"],
                                               "index": len(capture.frames) + 1}), "exit index"),
            ]
            for index, (value, _label) in enumerate(malformed):
                path = _write_sidecar(directory / "completion.json", value, suffix=f"-{index}")
                with self.assertRaises(CaptureError):
                    load_match_completion(capture, path)

            duplicate = json.dumps({
                **base,
                "exit_observation": base["exit_observation"],
            }, sort_keys=True, separators=(",", ":"))
            duplicate = duplicate[:-1] + ',"schema":"melee-web-retail-match-completion"}'
            duplicate_path = directory / "duplicate.json"
            duplicate_path.write_text(duplicate, encoding="utf-8")
            with self.assertRaises(CaptureError):
                load_match_completion(capture, duplicate_path)

            nested = json.dumps(base, sort_keys=True, separators=(",", ":"))
            nested = nested.replace(
                '"scene_request":1},',
                '"scene_request":1,"scene_request":1},',
            )
            nested_path = directory / "nested-duplicate.json"
            nested_path.write_text(nested, encoding="utf-8")
            with self.assertRaises(CaptureError):
                load_match_completion(capture, nested_path)

    def test_final_stock_shape_is_required_for_completion(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for stocks in ((1, 2), (0, 0), (-1, 2)):
                _, capture = _write_capture(directory, final_stocks=stocks)
                path = _write_sidecar(directory / "completion.json", _sidecar(capture),
                                       suffix=f"-{stocks[0]}-{stocks[1]}")
                report = load_match_completion(capture, path)
                self.assertEqual(report["status"], "bounded_prefix")

    def test_sidecar_hash_uses_exact_loaded_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            _, capture = _write_capture(directory)
            value = _sidecar(capture)
            path = _write_sidecar(directory / "completion.json", value)
            first = load_match_completion(capture, path)
            with path.open("ab") as stream:
                stream.write(b"\n")
            second = load_match_completion(capture, path)
            self.assertNotEqual(first["completion_sha256"], second["completion_sha256"])
            self.assertEqual(second["status"], "source_match_complete")


if __name__ == "__main__":
    unittest.main()
