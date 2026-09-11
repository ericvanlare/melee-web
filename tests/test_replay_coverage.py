"""Focused, self-contained tests for measured vanilla replay coverage."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import shutil
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from replay_coverage import (  # noqa: E402
    CoverageError,
    _parse_enum,
    analyze_capture,
    build_report,
    load_source_mapping,
    select_coverage,
)
from retail_input_plan import DISCONNECTED_PAD  # noqa: E402
from retail_replay_validation import (  # noqa: E402
    EXPECTED_PROVENANCE,
    GAME_REVISION,
    INPUT_PHASE,
    INITIAL_PHASE,
    PHASE,
    SCHEMA as CAPTURE_SCHEMA,
    VERSION as CAPTURE_VERSION,
)


CONNECTED_PAD = "00" * 11
SOURCE_SHA = "a" * 64


def _fighter(slot, *, kind, motion, stocks=4, damage_bits="00000000"):
    return {
        "slot": slot,
        "kind": kind,
        "motion": motion,
        "animation": 1,
        "facing_bits": "3f800000",
        "position_bits": ["00000000", "3f800000", "00000000"],
        "velocity_bits": ["00000000", "00000000", "00000000"],
        "knockback_bits": ["00000000", "00000000", "00000000"],
        "ground_air": 0,
        "animation_frame_bits": "00000000",
        "animation_speed_bits": "3f800000",
        "damage_bits": damage_bits,
        "shield_bits": "00000000",
        "stocks": stocks,
        "input_hex": "00" * 0x6C,
    }


def _state(scene_frame, match_frame, motion0, motion1, *, stocks=(4, 4),
           damage=("00000000", "00000000")):
    return {
        "rng": 1234 + scene_frame,
        "scene_frame": scene_frame,
        "match_frame": match_frame,
        "fighters": [
            _fighter(0, kind=1, motion=motion0, stocks=stocks[0],
                     damage_bits=damage[0]),
            _fighter(1, kind=22, motion=motion1, stocks=stocks[1],
                     damage_bits=damage[1]),
        ],
    }


def _plan(frame_count, *, source_sha=SOURCE_SHA):
    return {
        "schema": "melee-web-retail-input-plan",
        "version": 1,
        "policy": "dolphin-pipe-processed-v2",
        "source_sha256": source_sha,
        "first_frame": -123,
        "source_stage": 31,
        "source_characters": [2, 20],
        "frames": [[CONNECTED_PAD, CONNECTED_PAD] for _ in range(frame_count)],
    }


def _capture_rows(plan_sha, frame_count, *, motions0=None, motions1=None,
                  stocks0=None, stocks1=None, damage0=None, damage1=None,
                  capture_number=0):
    motions0 = list(motions0 or [14] * frame_count)
    motions1 = list(motions1 or [14] * frame_count)
    stocks0 = list(stocks0 or [4] * frame_count)
    stocks1 = list(stocks1 or [4] * frame_count)
    damage0 = list(damage0 or ["00000000"] * frame_count)
    damage1 = list(damage1 or ["00000000"] * frame_count)
    start = bytearray(b"\x01" * 0x138)
    start[14:16] = (31).to_bytes(2, "big")
    start[0x60] = 2
    start[0x84] = 20
    provenance = dict(EXPECTED_PROVENANCE)
    provenance["input_plan_sha256"] = plan_sha
    rows = [{
        "record": "header",
        "schema": CAPTURE_SCHEMA,
        "version": CAPTURE_VERSION,
        "phase": PHASE,
        "input_phase": INPUT_PHASE,
        "initial_phase": INITIAL_PHASE,
        "game_revision": GAME_REVISION,
        "frames_requested": frame_count,
        "provenance": provenance,
        "collector_sha256": "dd" * 32,
        "writes_game_state": False,
        "capture_id": "0000000000004000800000000000" + f"{capture_number:04x}",
    }, {
        "record": "match_enter",
        "rng": 1234,
        "start_melee_hex": start.hex(),
        "pad_lib_hex": "02" * 0x20,
        "pad_master_hex": "03" * 0x110,
        "pad_game_hex": "04" * 0x110,
    }, {
        "record": "match_enter_complete",
        **_state(999, 0, motions0[0], motions1[0]),
    }]
    for index in range(frame_count):
        rows.append({
            "record": "frame",
            "index": index,
            "consumed_inputs": [[
                CONNECTED_PAD, CONNECTED_PAD,
                DISCONNECTED_PAD, DISCONNECTED_PAD,
            ]],
            **_state(index, 0 if index < 2 else index - 1, motions0[index],
                     motions1[index], stocks=(stocks0[index], stocks1[index]),
                     damage=(damage0[index], damage1[index])),
        })
    rows.append({"record": "end", "frames": frame_count, "status": "captured"})
    return rows


def _write_fixture(directory, frame_count=3, *, source_sha=SOURCE_SHA,
                   motions0=None, motions1=None, stocks0=None, stocks1=None,
                   damage0=None, damage1=None, capture_number=0):
    directory = Path(directory)
    plan_path = directory / f"plan-{capture_number}.json"
    plan_value = _plan(frame_count, source_sha=source_sha)
    plan_path.write_text(json.dumps(plan_value, sort_keys=True,
                                    separators=(",", ":")) + "\n")
    plan_sha = hashlib.sha256(plan_path.read_bytes()).hexdigest()
    capture_path = directory / f"capture-{capture_number}.jsonl"
    rows = _capture_rows(
        plan_sha, frame_count, motions0=motions0, motions1=motions1,
        stocks0=stocks0, stocks1=stocks1, damage0=damage0, damage1=damage1,
        capture_number=capture_number,
    )
    capture_path.write_text("\n".join(json.dumps(row, sort_keys=True,
                                                 separators=(",", ":"))
                                  for row in rows) + "\n")
    return capture_path, plan_path


class ReplayCoverageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mapping = load_source_mapping()

    def test_complete_synthetic_capture_binds_plan_and_reports_namespaces(self):
        with tempfile.TemporaryDirectory() as directory:
            capture, plan = _write_fixture(directory, frame_count=3)
            result = analyze_capture(capture, input_plan=plan, cpu="Interpreter64",
                                     mapping=self.mapping)
        self.assertEqual(result["frames"], 3)
        self.assertEqual(result["source_stage"], 31)
        self.assertEqual(result["source_characters"], [2, 20])
        self.assertIn("fighter:1", result["features"])
        self.assertIn("fighter:22", result["features"])
        self.assertNotIn("fighter:2", result["features"])
        self.assertNotIn("fighter:20", result["features"])
        self.assertEqual(result["source_sha256"], SOURCE_SHA)

    def test_gapped_scene_or_frame_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            capture, plan = _write_fixture(directory)
            original = [json.loads(line) for line in capture.read_text().splitlines()]
            for field, value, restore in (("index", 2, 1), ("scene_frame", 4, 1)):
                with self.subTest(field=field):
                    rows = json.loads(json.dumps(original))
                    rows[4][field] = value
                    mutated = Path(directory) / f"gap-{field}.jsonl"
                    mutated.write_text("\n".join(json.dumps(row) for row in rows) + "\n")
                    with self.assertRaises(CoverageError):
                        analyze_capture(mutated, input_plan=plan, cpu="Interpreter64",
                                        mapping=self.mapping)

    def test_wrong_actual_pad_port_three_or_four_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            capture, plan = _write_fixture(directory)
            original = [json.loads(line) for line in capture.read_text().splitlines()]
            for port in (2, 3):
                with self.subTest(port=port):
                    rows = json.loads(json.dumps(original))
                    rows[3]["consumed_inputs"][0][port] = CONNECTED_PAD
                    mutated = Path(directory) / f"pad-{port}.jsonl"
                    mutated.write_text("\n".join(json.dumps(row) for row in rows) + "\n")
                    with self.assertRaisesRegex(CoverageError, "Input intent mismatch"):
                        analyze_capture(mutated, input_plan=plan, cpu="Interpreter64",
                                        mapping=self.mapping)

    def test_unknown_motion_id_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            capture, plan = _write_fixture(directory, motions0=[0xDEAD] * 3)
            with self.assertRaisesRegex(CoverageError, "observed motion"):
                analyze_capture(capture, input_plan=plan, cpu="Interpreter64",
                                mapping=self.mapping)

    def test_non_special_fighter_motion_uses_fighter_specific_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            # Fox's appeal is a fighter enum state, but it is not one of the
            # explicitly recognized special families.
            capture, plan = _write_fixture(
                directory, motions0=[370, 370, 370])
            result = analyze_capture(capture, input_plan=plan, cpu="Interpreter64",
                                     mapping=self.mapping)
        appeal = next(item for item in result["players"][0]["motions"]
                      if item["id"] == 370)
        self.assertEqual(appeal["scope"], "fighter_specific")
        self.assertIsNone(appeal["family"])

    def test_mapping_hash_covers_scanned_dependency_inventory(self):
        dependency = "melee/ft/kinds/ftCommon/ftCo_Damage.c"
        self.assertIn(dependency, self.mapping["source_files"])
        self.assertTrue(any(path.endswith(".c") for path in self.mapping["source_files"]))
        source_root = ROOT / ".deps" / "melee" / "src"
        with tempfile.TemporaryDirectory() as directory:
            copied = Path(directory)
            shutil.copytree(source_root / "melee" / "ft", copied / "melee" / "ft")
            before = load_source_mapping(copied)
            for dependency in ("melee/ft/kinds/ftCrazyHand/forward.h",
                               "melee/ft/kinds/ftCommon/ftCo_Damage.c"):
                with self.subTest(dependency=dependency):
                    path = copied / dependency
                    path.write_text(path.read_text() + "\n/* provenance mutation */\n")
                    after = load_source_mapping(copied)
                    self.assertEqual(before["common"], after["common"])
                    self.assertEqual(before["fighters"], after["fighters"])
                    self.assertNotEqual(before["source_sha256"], after["source_sha256"])
                    before = after

    def test_ambiguous_enum_expression_is_rejected(self):
        source = Path("ambiguous-forward.h")
        text = "typedef enum bad_MotionState { bad_MS_A = Unknown } bad_MotionState;"
        with self.assertRaisesRegex(CoverageError, "ambiguous enum expression"):
            _parse_enum(text, "bad_MotionState", source)

    def test_revisiting_motion_counts_entries_and_rebirth_cycle_once(self):
        with tempfile.TemporaryDirectory() as directory:
            capture, plan = _write_fixture(
                directory, frame_count=4, motions0=[14, 350, 14, 12],
                motions1=[14, 14, 13, 14],
            )
            result = analyze_capture(capture, input_plan=plan, cpu="Interpreter64",
                                     mapping=self.mapping)
        fox = result["players"][0]
        wait = next(item for item in fox["motions"] if item["id"] == 14)
        self.assertEqual(wait["entries"], 2)
        falco = result["players"][1]
        self.assertEqual(len(falco["respawns"]), 0)
        self.assertEqual(len(fox["respawns"]), 1)

    def test_marginal_selection_uses_source_then_capture_hash_ties(self):
        captures = [
            {"path": "z-path", "source_sha256": "b" * 64,
             "capture_sha256": "b" * 64, "features": ["shared", "b-only"]},
            {"path": "a-path", "source_sha256": "a" * 64,
             "capture_sha256": "z" * 64, "features": ["shared", "a-only"]},
            {"path": "c-path", "source_sha256": "c" * 64,
             "capture_sha256": "c" * 64, "features": ["c-only"]},
        ]
        selected = select_coverage(captures, 2)
        self.assertEqual([item["path"] for item in selected], ["a-path", "z-path"])
        self.assertEqual(selected[0]["marginal_features"], ["a-only", "shared"])
        self.assertEqual(selected[1]["marginal_features"], ["b-only"])

    def test_prefix_plans_with_same_full_donor_sha_cannot_cross_split(self):
        with tempfile.TemporaryDirectory() as directory:
            candidate, candidate_plan = _write_fixture(
                directory, frame_count=2, source_sha="f" * 64, capture_number=1)
            held_out, held_out_plan = _write_fixture(
                directory, frame_count=3, source_sha="f" * 64, capture_number=2)
            with self.assertRaisesRegex(CoverageError, "share source identity"):
                build_report([candidate], input_plans=[candidate_plan],
                             cpu="Interpreter64", held_out=[held_out],
                             held_out_input_plans=[held_out_plan])

    def test_report_is_explicitly_non_gold(self):
        with tempfile.TemporaryDirectory() as directory:
            capture, plan = _write_fixture(directory)
            report = build_report([capture], input_plans=[plan], cpu="Interpreter64")
        self.assertFalse(report["gold_admitted"])
        self.assertEqual(len(report["selected"]), 1)
        self.assertEqual(report["held_out"], [])

    def test_cli_rejects_output_alias_of_input_capture(self):
        spec = importlib.util.spec_from_file_location(
            "analyze_replay_coverage", ROOT / "scripts" / "analyze_replay_coverage.py")
        script = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(script)
        with tempfile.TemporaryDirectory() as directory:
            capture, plan = _write_fixture(directory)
            alias = Path(directory) / "capture-alias.jsonl"
            alias.symlink_to(capture)
            with mock.patch.object(sys, "argv", [
                    "analyze_replay_coverage.py", "--cpu", "Interpreter64",
                    "--capture", str(capture), "--input-plan", str(plan),
                    "--output", str(alias),
            ]):
                with self.assertRaises(SystemExit) as error:
                    script.main()
            self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
