"""Focused validation and boundary tests for source-length discovery."""

from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tools"), str(ROOT / "scripts")]

from retail_replay_validation import EXPECTED_PROVENANCE  # noqa: E402
from retail_match_discovery import (  # noqa: E402
    EXIT_PHASE,
    FINAL_EXIT_CALLER,
    SCHEMA,
    DiscoveryError,
    load_discovery,
)
from retail_input_plan import DISCONNECTED_PAD  # noqa: E402


def _pad_state() -> str:
    config = bytes.fromhex(
        "0000002d00000008001e0000000000007f0000ff0000ff007fffff000000")
    return (config + bytes(12 * 66)).hex()


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


def _state(scene_frame: int, stocks=(4, 4)) -> dict:
    return {
        "rng": 100 + scene_frame,
        "scene_frame": scene_frame,
        "match_frame": scene_frame,
        "fighters": [_fighter(0, stocks[0]), _fighter(1, stocks[1])],
        "pad_state_hex": _pad_state(),
    }


def _plan(frame_count: int) -> tuple[dict, str]:
    pads = [["00" * 11, "00" * 11] for _ in range(frame_count)]
    value = {
        "source_stage": 32,
        "source_characters": [8, 8],
        "frames": pads,
    }
    return value, "aa" * 32


def _rows(frame_count=2, cap=4, *, stop_reason="match_end", status="complete",
          exit_observation=None) -> tuple[list[dict], dict, str]:
    plan, plan_hash = _plan(cap)
    start = bytearray(0x138)
    start[14:16] = (32).to_bytes(2, "big")
    start[0x60] = start[0x84] = 8
    provenance = dict(EXPECTED_PROVENANCE)
    provenance["input_plan_sha256"] = plan_hash
    rows = [{
        "record": "header",
        "schema": SCHEMA,
        "version": 1,
        "capture_id": "00000000000040008000000000000001",
        "phase": "HSD_GObj_80390CFC_return",
        "input_phase": "HSD_PadRenewMasterStatus_dequeued_slot",
        "initial_phase": "gm_Scene_Vs_OnEnter_entry",
        "game_revision": "GALE01r2",
        "frames_cap": cap,
        "input_plan_frames": cap,
        "input_plan_sha256": plan_hash,
        "provenance": provenance,
        "collector_sha256": "bb" * 32,
        "writes_game_state": False,
    }, {
        "record": "match_enter",
        "rng": 7,
        "start_melee_hex": start.hex(),
        "pad_state_hex": _pad_state(),
    }, {
        "record": "match_enter_complete",
        **_state(999),
    }]
    for index in range(frame_count):
        stocks = (0, 2) if index == frame_count - 1 and stop_reason == "match_end" else (4, 4)
        rows.append({
            "record": "frame",
            "index": index,
            "consumed_inputs": [plan["frames"][index] + [DISCONNECTED_PAD, DISCONNECTED_PAD]],
            **_state(index, stocks),
        })
    rows.append({
        "record": "end",
        "frames": frame_count,
        "stop_reason": stop_reason,
        "status": status,
        "scene_request": 1 if stop_reason == "match_end" else 0,
        "match_end_state": 3 if stop_reason == "match_end" else 0,
        "match_result": 2 if stop_reason == "match_end" else 0,
        "final_draw_source_index": frame_count - 1,
        "exit_observation": exit_observation if exit_observation is not None else ({
            "phase": EXIT_PHASE,
            "frame_index": frame_count - 1,
            "source_scene_frame": frame_count - 1,
            "caller": FINAL_EXIT_CALLER,
            "scene_request": 1,
        } if stop_reason == "match_end" else None),
    })
    return rows, plan, plan_hash


def _write(path: Path, rows: list[dict]) -> None:
    path.write_text("\n".join(json.dumps(row, sort_keys=True, separators=(",", ":"))
                                 for row in rows) + "\n", encoding="utf-8")


class RetailMatchDiscoveryTests(unittest.TestCase):
    def test_complete_discovery_requires_plan_hash_and_exact_four_ports(self):
        rows, plan, plan_hash = _rows()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "discovery.jsonl"
            _write(path, rows)
            result = load_discovery(path, plan=plan, plan_sha256=plan_hash)
            self.assertTrue(result.complete)
            self.assertEqual(result.report["status"], "source_match_complete")
            self.assertEqual(result.report["frames_actual"], 2)
            self.assertEqual(result.report["final_draw_source_index"], 1)
            self.assertFalse(result.report["gold_admitted"])

    def test_match_end_without_immutable_plan_is_unverified(self):
        rows, _, _ = _rows()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "discovery.jsonl"
            _write(path, rows)
            result = load_discovery(path)
            self.assertFalse(result.complete)
            self.assertEqual(result.report["status"], "unverified_match_end")

    def test_cap_exhaustion_is_valid_diagnostic_but_not_complete(self):
        rows, plan, plan_hash = _rows(frame_count=3, cap=3,
                                      stop_reason="frame_cap", status="cap_exhausted")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "discovery.jsonl"
            _write(path, rows)
            result = load_discovery(path, plan=plan, plan_sha256=plan_hash)
            self.assertFalse(result.complete)
            self.assertEqual(result.report["status"], "cap_exhausted")
            self.assertEqual(result.report["stop_reason"], "frame_cap")

    def test_changed_input_state_end_and_order_are_rejected(self):
        rows, plan, plan_hash = _rows()
        cases = []
        changed_input = deepcopy(rows)
        changed_input[3]["consumed_inputs"][0][0] = "01" + "00" * 10
        cases.append(changed_input)
        changed_state = deepcopy(rows)
        changed_state[4]["scene_frame"] = 99
        cases.append(changed_state)
        changed_end = deepcopy(rows)
        changed_end[-1]["final_draw_source_index"] = 0
        cases.append(changed_end)
        reordered = deepcopy(rows)
        reordered[3], reordered[4] = reordered[4], reordered[3]
        cases.append(reordered)
        with tempfile.TemporaryDirectory() as directory:
            for index, value in enumerate(cases):
                path = Path(directory) / f"bad-{index}.jsonl"
                _write(path, value)
                with self.subTest(index=index), self.assertRaises(DiscoveryError):
                    load_discovery(path, plan=plan, plan_sha256=plan_hash)

    def test_artifact_hash_is_bound_to_exact_bytes(self):
        rows, plan, plan_hash = _rows()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "discovery.jsonl"
            _write(path, rows)
            first = load_discovery(path, plan=plan, plan_sha256=plan_hash)
            lines = path.read_text(encoding="utf-8").splitlines()
            lines[0] += " "
            path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            second = load_discovery(path, plan=plan, plan_sha256=plan_hash)
            self.assertNotEqual(first.sha256, second.sha256)
            self.assertEqual(first.report["discovery_sha256"], first.sha256)

    def test_runner_rejects_discovery_without_full_plan_before_launch(self):
        import capture_retail_replay as capture
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "discovery.jsonl"
            with self.assertRaisesRegex(capture.CaptureRunnerError, "requires --input-plan"):
                capture.capture_replay(
                    dolphin=root / "Dolphin", disc=root / "disc", dol=root / "dol",
                    template_user=root / "template", snapshot=root / "snapshot",
                    checkpoint_gc=root / "checkpoint", provenance=root / "provenance",
                    output=output, frames=2, until_match_end=True)


class RetailMatchDiscoveryCollectorTests(unittest.TestCase):
    def test_exit_before_final_scheduler_does_not_finish_on_previous_draw(self):
        class Command:
            def __init__(self, *args, **kwargs): pass

        fake = type("FakeGdb", (), {
            "Command": Command, "Breakpoint": Command, "COMMAND_USER": 0,
            "parse_and_eval": staticmethod(lambda name: 0x8016D884 if name == "$lr" else 0),
        })
        spec = importlib.util.spec_from_file_location(
            "discovery_collector_order", ROOT / "tools" / "reference_replay_capture.py")
        collector = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {"gdb": fake}):
            spec.loader.exec_module(collector)
        collector.active = collector.ready = True
        collector.UNTIL_MATCH_END = True
        collector.DRAW_AUDIT = True
        collector.frame_index = 2
        collector.LIMIT = 4
        collector.last_drawn_source_index = 1
        collector.machine_context = lambda: []
        collector.word = lambda address: (2 if address == 0x80479D58 else 1)
        collector.finish_discovery = mock.Mock(return_value=True)
        with tempfile.TemporaryDirectory() as directory:
            collector.ROOT = Path(directory)
            self.assertFalse(collector.exit_requested())
            collector.finish_discovery.assert_not_called()
            self.assertEqual(collector.exit_observation["source_scene_frame"], 2)

            collector.frame_index = 3
            collector.draw_source_index = 2
            collector.draw_before = {"scene_frame": 3}
            collector.state = lambda: {"scene_frame": 3}
            collector.observations.accept = lambda *args: True
            self.assertTrue(collector.draw_return())
            collector.finish_discovery.assert_called_once_with("match_end")


if __name__ == "__main__":
    unittest.main()
