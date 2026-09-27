"""Checks for the cold-boot retail menu-route diagnostic boundary."""

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT))

from capture_allocation_history import (gdb_script, verify_menu_route,
                                        verify_menu_route_commands)  # noqa: E402
from tools import retail_allocation_menu  # noqa: E402


class CaptureAllocationMenuRouteTests(unittest.TestCase):
    def test_route_gdb_script_runs_route_driver_without_match_replay_commands(self):
        paths = {name: Path("/tmp") / name for name in
                 ("socket", "allocation_collector", "menu_driver", "helper", "collector")}
        source = gdb_script(paths, menu_round_trip=True)
        self.assertIn(f"source {paths['menu_driver']}", source)
        self.assertIn("MENU_ROUTE_TRACE.close()", source)
        self.assertIn("allocation_finish('captured', 'menu_round_trip_complete')", source)
        self.assertNotIn("retail-replay-arm", source)
        self.assertNotIn("retail-step", source)

    def test_route_verifier_requires_complete_ordered_source_markers_and_ticks(self):
        rows = [
            {"event": "first_scheduler_return", "scene_kind": 0x2A, "game_mode": 0,
             "sequence": 0, "pad_copy_status_hex": "00"},
            {"event": "scheduler_return", "scene_kind": 1, "game_mode": 1,
             "sequence": 0, "pad_copy_status_hex": "00", "menu_state": {},
             "current_hps_hex": "", "hps_voice_word": "0x0"},
            {"event": "scheduler_return", "scene_kind": 8, "game_mode": 2,
             "sequence": 1, "pad_copy_status_hex": "00", "css_data_hex": "00",
             "css_cursors": [], "current_hps_hex": "", "hps_voice_word": "0x0"},
            {"event": "scheduler_return", "scene_kind": 0x1C, "game_mode": 0,
             "sequence": 2, "pad_copy_status_hex": "00",
             "current_hps_hex": "", "hps_voice_word": "0x0"},
            {"event": "cold_css_ready", "scene_kind": 8, "game_mode": 2,
             "sequence": 1, "pad_copy_status_hex": "00"},
            {"event": "root_main_menu_ready", "scene_kind": 1, "game_mode": 1,
             "sequence": 2, "pad_copy_status_hex": "00"},
            {"event": "title_ready", "scene_kind": 0, "game_mode": 0,
             "sequence": 3, "pad_copy_status_hex": "00"},
            {"event": "round_trip_css_ready", "scene_kind": 8, "game_mode": 2,
             "sequence": 4, "pad_copy_status_hex": "00"},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / retail_allocation_menu.MENU_ROUTE_TRACE_NAME
            path.write_text("".join(json.dumps(row) + "\n" for row in rows))
            result = verify_menu_route(path)
            self.assertEqual(result["source_scheduler_frames"], 3)
            self.assertEqual([row[0] for row in result["markers"]], [
                "first_scheduler_return", "cold_css_ready", "root_main_menu_ready",
                "title_ready", "round_trip_css_ready"])

            rows[5]["scene_kind"] = 8
            path.write_text("".join(json.dumps(row) + "\n" for row in rows))
            with self.assertRaisesRegex(RuntimeError, "marker root_main_menu_ready"):
                verify_menu_route(path)

    def test_input_verifier_binds_original_controls_to_their_source_scenes(self):
        commands = [
            {"event": "pad_command", "scene_kind": 8, "game_mode": 2, "command": "PRESS L"},
            {"event": "pad_command", "scene_kind": 8, "game_mode": 2, "command": "PRESS R"},
            {"event": "pad_command", "scene_kind": 8, "game_mode": 2, "command": "PRESS START"},
            {"event": "pad_command", "scene_kind": 1, "game_mode": 1, "command": "PRESS B"},
            {"event": "pad_command", "scene_kind": 0, "game_mode": 0, "command": "PRESS START"},
            {"event": "pad_command", "scene_kind": 1, "game_mode": 1, "command": "PRESS A"},
            {"event": "pad_command", "scene_kind": 1, "game_mode": 1, "command": "PRESS A"},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "commands.jsonl"
            path.write_text("".join(json.dumps(row) + "\n" for row in commands))
            self.assertEqual(verify_menu_route_commands(path)["commands"], 7)
            commands[3]["scene_kind"] = 8
            path.write_text("".join(json.dumps(row) + "\n" for row in commands))
            with self.assertRaisesRegex(RuntimeError, "root-menu Back"):
                verify_menu_route_commands(path)

            commands[3]["scene_kind"] = 1
            commands[0], commands[2] = commands[2], commands[0]
            path.write_text("".join(json.dumps(row) + "\n" for row in commands))
            with self.assertRaisesRegex(RuntimeError, "ordered CSS"):
                verify_menu_route_commands(path)


if __name__ == "__main__":
    unittest.main()
