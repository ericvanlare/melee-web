"""Checks for the cold-boot retail menu-route diagnostic boundary."""

from __future__ import annotations

import json
import hashlib
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT))

from capture_allocation_history import (gdb_script, resolve_gdb_executable,
                                        retain_route_screenshots,
                                        retail as capture_retail,
                                        validate_reference_build_manifest,
                                        validate_setup_receipt,
                                        verify_menu_route, verify_menu_route_commands,
                                        verify_vs_rules_items_route,
                                        verify_vs_rules_items_route_commands,
                                        visual_capture_options)  # noqa: E402
from tools import retail_allocation_menu  # noqa: E402
from tools import retail_replay_validation as retail  # noqa: E402


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

    def test_vs_rules_items_gdb_script_stops_at_the_declared_source_route(self):
        paths = {name: Path("/tmp") / name for name in
                 ("socket", "allocation_collector", "menu_driver", "helper", "collector")}
        source = gdb_script(paths, vs_rules_items_round_trip=True)
        self.assertIn(f"source {paths['menu_driver']}", source)
        self.assertIn("MENU_ROUTE_TRACE.close()", source)
        self.assertIn("allocation_finish('captured', 'vs_rules_items_round_trip_complete')", source)
        self.assertNotIn("retail-replay-arm", source)
        self.assertNotIn("retail-step", source)

    def test_vs_rules_items_verifier_requires_commit_reentry_and_css_retention(self):
        rules = {"stock_count": 4, "item_frequency": 3, "item_mask": 0x07}
        changed = {**rules, "item_mask": 0x0F}
        committed = {**changed, "item_frequency": 0xFF}
        expected = [
            ("first_scheduler_return", 0x2A, 0, None, None),
            ("cold_css_ready", 8, 2, None, None),
            ("versus_submenu_after_css_parent", 1, 1, (2, 0), rules),
            ("root_menu_after_vs_back", 1, 1, (0, 1), rules),
            ("versus_submenu_for_rules", 1, 1, (2, 0), rules),
            ("vs_rules_first_entry", 1, 1, (13, 0), rules),
            ("vs_items_entry", 1, 1, (16, 0), rules),
            ("vs_items_one_bit_toggled", 1, 1, (16, 0), changed),
            ("vs_items_frequency_none", 1, 1, (16, 31), changed),
            ("vs_items_back_committed", 1, 1, (13, 5), committed),
            ("vs_rules_back_to_versus", 1, 1, (2, 3), committed),
            ("versus_back_to_main", 1, 1, (0, 1), committed),
            ("vs_rules_reentry_retained_items", 1, 1, (13, 0), committed),
            ("vs_rules_stock_three_selected", 1, 1, (13, 1), committed),
            ("css_after_rules_start_retained", 8, 2, None,
             {**committed, "stock_count": 3}),
            ("sss_after_rules_start", 9, 2, None, committed),
            ("sss_final_destination_selected", 9, 2, None, committed),
            ("vs_match_entered", 2, 2, None, committed),
            ("vs_match_after_180_ticks", 2, 2, None, committed),
            ("vs_no_contest_chord_sent", 2, 2, None, committed),
            ("results_no_contest", 5, 2, None, committed),
            ("css_after_results_retained", 8, 2, None,
             {**committed, "stock_count": 3}),
        ]
        rows = [{"event": "scheduler_return", "sequence": 0,
                 "scene_kind": 1, "game_mode": 1,
                 "menu_state": {"cur": 16}, "rules_state": committed,
                 "pad_copy_status_hex": "00", "current_hps_hex": "",
                 "hps_voice_word": "0x0"},
                {"event": "scheduler_return", "sequence": 1,
                 "scene_kind": 2, "game_mode": 2,
                 "match_start_data": {"stage": 0x20, "item_frequency": -1,
                                       "item_mask_hex": "000000000000000f",
                                       "players": [{"slot_type": 0, "stocks": 3}]},
                 "pad_copy_status_hex": "00", "current_hps_hex": "",
                 "hps_voice_word": "0x0"},
                {"event": "scheduler_return", "sequence": 2,
                 "scene_kind": 5, "game_mode": 2, "results_outcome": 7,
                 "pad_copy_status_hex": "00", "current_hps_hex": "",
                 "hps_voice_word": "0x0"}]
        for name, scene, mode, menu, state in expected:
            row = {"event": name, "scene_kind": scene, "game_mode": mode,
                   "sequence": 0, "pad_copy_status_hex": "00"}
            if menu is not None:
                row["menu_state"] = {"cur": menu[0], "hovered": menu[1],
                                     "confirmed": 3 if name == "vs_rules_stock_three_selected" else 0}
            if state is not None:
                row["rules_state"] = state
            if name == "sss_final_destination_selected":
                row["selected_stage_kind"] = 0x20
            if name == "vs_match_entered":
                row["match_start_data"] = {
                    "stage": 0x20, "item_frequency": -1,
                    "item_mask_hex": "000000000000000f",
                    "players": [{"slot_type": 0, "stocks": 3}]}
            if name == "results_no_contest":
                row["results_outcome"] = 7
            rows.append(row)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "route.jsonl"
            path.write_text("".join(json.dumps(row) + "\n" for row in rows))
            result = verify_vs_rules_items_route(path)
            self.assertEqual(result["stock_count_after_results_css_return"], 3)
            self.assertEqual(result["committed_item_mask"], "000000000000000f")

            rows[-1]["rules_state"]["stock_count"] = 4
            path.write_text("".join(json.dumps(row) + "\n" for row in rows))
            with self.assertRaisesRegex(RuntimeError, "Results/CSS return"):
                verify_vs_rules_items_route(path)

    def test_vs_rules_items_input_verifier_binds_original_menu_choices(self):
        commands = []

        def add(scene, mode, command, cur=None, hovered=None):
            row = {"event": "pad_command", "scene_kind": scene,
                   "game_mode": mode, "command": command}
            if cur is not None:
                row["menu_state"] = {"cur": cur, "hovered": hovered}
            commands.append(row)

        add(8, 2, "PRESS L")
        add(8, 2, "PRESS R")
        add(8, 2, "PRESS START")
        add(1, 1, "PRESS B", 2, 0)
        add(1, 1, "PRESS A", 0, 1)
        add(1, 1, "PRESS D_UP", 2, 0)
        add(1, 1, "PRESS A", 2, 3)
        add(1, 1, "PRESS D_UP", 13, 0)
        add(1, 1, "PRESS A", 13, 5)
        add(1, 1, "PRESS A", 16, 0)
        add(1, 1, "PRESS D_LEFT", 16, 0)
        add(1, 1, "PRESS D_UP", 16, 31)
        add(1, 1, "PRESS B", 16, 31)
        add(1, 1, "PRESS B", 13, 5)
        add(1, 1, "PRESS B", 2, 3)
        add(1, 1, "PRESS A", 0, 1)
        add(1, 1, "PRESS D_UP", 2, 0)
        add(1, 1, "PRESS A", 2, 3)
        add(1, 1, "PRESS D_DOWN", 13, 0)
        add(1, 1, "PRESS START", 13, 1)
        add(8, 2, "PRESS START")
        add(9, 2, "PRESS A")
        for button in ("L", "R", "A", "START"):
            add(2, 2, "PRESS " + button)
            commands[-1]["port"] = 1
            commands[-1]["source_sequence"] = 42
        add(5, 2, "PRESS START")
        commands[-1]["port"] = 1
        commands[12]["menu_state"]["confirmed"] = 0
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "commands.jsonl"
            path.write_text("".join(json.dumps(row) + "\n" for row in commands))
            self.assertEqual(verify_vs_rules_items_route_commands(path)["commands"], 27)
            commands[12]["menu_state"]["confirmed"] = 1
            path.write_text("".join(json.dumps(row) + "\n" for row in commands))
            with self.assertRaisesRegex(RuntimeError, "Items B"):
                verify_vs_rules_items_route_commands(path)
            commands[12]["menu_state"]["confirmed"] = 0
            commands[-1]["port"] = 2
            path.write_text("".join(json.dumps(row) + "\n" for row in commands))
            with self.assertRaisesRegex(RuntimeError, "original P1 Start"):
                verify_vs_rules_items_route_commands(path)
            commands.pop()
            path.write_text("".join(json.dumps(row) + "\n" for row in commands))
            with self.assertRaisesRegex(RuntimeError, "did not exit"):
                verify_vs_rules_items_route_commands(path)

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
            {"event": "css_before_b_back_probe", "scene_kind": 8, "game_mode": 2,
             "sequence": 1, "pad_copy_status_hex": "00"},
            {"event": "css_b_back_probe_remained_css", "scene_kind": 8, "game_mode": 2,
             "sequence": 1, "pad_copy_status_hex": "00"},
            {"event": "versus_submenu_ready_after_css", "scene_kind": 1, "game_mode": 1,
             "sequence": 2, "pad_copy_status_hex": "00", "menu_state": {"cur": 2, "hovered": 0}},
            {"event": "root_main_menu_ready", "scene_kind": 1, "game_mode": 1,
             "sequence": 2, "pad_copy_status_hex": "00", "menu_state": {"cur": 0, "hovered": 1}},
            {"event": "title_ready", "scene_kind": 0, "game_mode": 0,
             "sequence": 3, "pad_copy_status_hex": "00"},
            {"event": "root_main_menu_ready_after_title", "scene_kind": 1, "game_mode": 1,
             "sequence": 3, "pad_copy_status_hex": "00", "menu_state": {"cur": 0, "hovered": 0}},
            {"event": "versus_submenu_ready_after_title", "scene_kind": 1, "game_mode": 1,
             "sequence": 3, "pad_copy_status_hex": "00", "menu_state": {"cur": 2, "hovered": 0}},
            {"event": "round_trip_css_ready", "scene_kind": 8, "game_mode": 2,
             "sequence": 4, "pad_copy_status_hex": "00"},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / retail_allocation_menu.MENU_ROUTE_TRACE_NAME
            path.write_text("".join(json.dumps(row) + "\n" for row in rows))
            result = verify_menu_route(path)
            self.assertEqual(result["source_scheduler_frames"], 3)
            self.assertEqual([row[0] for row in result["markers"]], [
                "first_scheduler_return", "cold_css_ready", "css_before_b_back_probe",
                "css_b_back_probe_remained_css", "versus_submenu_ready_after_css",
                "root_main_menu_ready", "title_ready", "root_main_menu_ready_after_title",
                "versus_submenu_ready_after_title", "round_trip_css_ready"])

            rows[8]["menu_state"]["cur"] = 2
            path.write_text("".join(json.dumps(row) + "\n" for row in rows))
            with self.assertRaisesRegex(RuntimeError, "marker root_main_menu_ready"):
                verify_menu_route(path)

    def test_input_verifier_binds_original_controls_to_their_source_scenes(self):
        commands = [
            {"event": "pad_command", "scene_kind": 8, "game_mode": 2, "command": "PRESS B"},
            {"event": "pad_command", "scene_kind": 8, "game_mode": 2, "command": "PRESS L"},
            {"event": "pad_command", "scene_kind": 8, "game_mode": 2, "command": "PRESS R"},
            {"event": "pad_command", "scene_kind": 8, "game_mode": 2, "command": "PRESS START"},
            {"event": "pad_command", "scene_kind": 1, "game_mode": 1, "command": "PRESS B",
             "menu_state": {"cur": 2, "hovered": 0}},
            {"event": "pad_command", "scene_kind": 1, "game_mode": 1, "command": "PRESS B",
             "menu_state": {"cur": 0, "hovered": 1}},
            {"event": "pad_command", "scene_kind": 0, "game_mode": 0, "command": "PRESS START"},
            {"event": "pad_command", "scene_kind": 1, "game_mode": 1, "command": "PRESS DOWN",
             "menu_state": {"cur": 0, "hovered": 0}},
            {"event": "pad_command", "scene_kind": 1, "game_mode": 1, "command": "PRESS A",
             "menu_state": {"cur": 0, "hovered": 1}},
            {"event": "pad_command", "scene_kind": 1, "game_mode": 1, "command": "PRESS A",
             "menu_state": {"cur": 2, "hovered": 0}},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "commands.jsonl"
            path.write_text("".join(json.dumps(row) + "\n" for row in commands))
            self.assertEqual(verify_menu_route_commands(path)["commands"], 10)
            commands[4]["scene_kind"] = 8
            path.write_text("".join(json.dumps(row) + "\n" for row in commands))
            with self.assertRaisesRegex(RuntimeError, "ordered VS-submenu and root-menu Back"):
                verify_menu_route_commands(path)

            commands[4]["scene_kind"] = 1
            commands[1], commands[3] = commands[3], commands[1]
            path.write_text("".join(json.dumps(row) + "\n" for row in commands))
            with self.assertRaisesRegex(RuntimeError, "CSS B probe"):
                verify_menu_route_commands(path)

    def test_reference_build_manifest_binds_the_binary_and_game_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "dolphin-emu-nogui"
            binary.write_bytes(b"pinned test executable")
            manifest = root / "reference-dolphin-build.json"
            manifest.write_text(json.dumps({
                "schema": "melee-web-reference-dolphin-build",
                "version": 1,
                "target": "dolphin-nogui",
                "dolphin_commit": retail.EXPECTED_PROVENANCE["dolphin_commit"],
                "binary": str(binary),
                "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                "writes_guest_memory": False,
                "observer_identity": {
                    "game_revision": "GALE01r2",
                    "dol_sha1": retail.EXPECTED_PROVENANCE["dol_sha1"],
                    "dol_sha256": "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646",
                    "writes_guest_memory": False,
                    "cpu": "JITARM64",
                },
            }))
            result = validate_reference_build_manifest(manifest, binary)
            self.assertEqual(result["binary_sha256"], hashlib.sha256(binary.read_bytes()).hexdigest())
            self.assertEqual(result["build_cpu"], "JITARM64")

            manifest_data = json.loads(manifest.read_text())
            manifest_data["binary_sha256"] = "0" * 64
            manifest.write_text(json.dumps(manifest_data))
            with self.assertRaisesRegex(ValueError, "executable hash"):
                validate_reference_build_manifest(manifest, binary)

    def test_owned_input_receipt_binds_disc_dol_build_and_empty_card_baseline(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            disc = root / "disc.ciso"
            dol = root / "main.dol"
            dolphin = root / "dolphin-emu-nogui"
            disc.write_bytes(b"owned CISO")
            dol.write_bytes(b"owned DOL")
            dolphin.write_bytes(b"pinned executable")
            template = root / "template-user"
            (template / "Config").mkdir(parents=True)
            (template / "Config/Dolphin.ini").write_bytes(b"private profile")
            (template / "Config/GCPadNew.ini").write_bytes(b"four raw pads")
            checkpoint = root / "checkpoint-gc"
            card = checkpoint / "USA/Card A"
            card.mkdir(parents=True)
            (checkpoint / "SRAM.raw").write_bytes(b"source SRAM")
            manifest_path = root / "dolphin-build.json"
            manifest_path.write_text("{}")
            manifest_hash = capture_retail._sha256(manifest_path)
            receipt = {
                "schema": "melee-web-original-menu-reference-inputs",
                "version": 1,
                "disc": {"sha256": capture_retail._sha256(disc), "game_id": "GALE01r2",
                         "dol_sha1_verified": capture_retail._sha1(dol)},
                "dol": {"sha1": capture_retail._sha1(dol),
                        "sha256": capture_retail._sha256(dol)},
                "dolphin": {"binary_sha256": capture_retail._sha256(dolphin),
                            "dolphin_commit": retail.EXPECTED_PROVENANCE["dolphin_commit"],
                            "target": "dolphin-nogui", "sha256": manifest_hash},
                "template_source": {"selected_language": 0, "cpu_thread": False,
                    "cheats": False, "custom_rtc": 1704067200,
                    "capture_dolphin_ini_sha256": capture_retail._sha256(template / "Config/Dolphin.ini"),
                    "capture_gcpad_ini_sha256": capture_retail._sha256(template / "Config/GCPadNew.ini")},
                "initial_card": {"file_count": 0, "format": "empty GCI folder"},
                "external_save_hashes": {"SRAM.raw": capture_retail._sha256(checkpoint / "SRAM.raw")},
            }
            receipt_path = root / "setup-receipt.json"
            receipt_path.write_text(json.dumps(receipt))
            args = SimpleNamespace(disc=disc, dol=dol, dolphin=dolphin,
                template_user=template, checkpoint_gc=checkpoint)
            result = validate_setup_receipt(receipt_path, args,
                {"sha256": manifest_hash})
            self.assertEqual(result["card_baseline"], "empty GCI folder")

            (card / "unexpected.gci").write_bytes(b"not the declared baseline")
            with self.assertRaisesRegex(ValueError, "empty GCI folder baseline"):
                validate_setup_receipt(receipt_path, args, {"sha256": manifest_hash})

    def test_headless_visual_options_use_the_pinned_image_frame_dumper(self):
        command = visual_capture_options(["dolphin", "-u", "/owned/user"])
        self.assertEqual(command[-10:], [
            "-p", "headless", "-v", "OGL",
            "-C", "Dolphin.Movie.DumpFrames=True",
            "-C", "Dolphin.Movie.DumpFramesSilent=True",
            "-C", "Dolphin.GFX.Settings.DumpFramesAsImages=True",
        ])

    def test_private_debugger_path_can_be_pinned_outside_path(self):
        with tempfile.TemporaryDirectory() as directory:
            debugger = Path(directory) / "gdb"
            debugger.write_text("#!/bin/sh\nexit 0\n")
            debugger.chmod(0o700)
            self.assertEqual(resolve_gdb_executable(debugger), debugger.resolve())
            with self.assertRaisesRegex(ValueError, "GDB executable is unavailable"):
                resolve_gdb_executable(Path(directory) / "missing-gdb")

    def test_route_screenshots_are_hash_bound_to_source_markers(self):
        marker_names = (
            "cold_css_ready", "css_before_b_back_probe", "css_b_back_probe_remained_css",
            "versus_submenu_ready_after_css", "root_main_menu_ready", "title_ready",
            "root_main_menu_ready_after_title", "versus_submenu_ready_after_title",
            "round_trip_css_ready")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            frames = root / "frames"
            frames.mkdir()
            rows = []
            for sequence, name in enumerate(marker_names):
                rows.append({"event": "scheduler_return", "sequence": sequence})
                rows.append({"event": name, "sequence": sequence,
                             "scene_kind": 8, "game_mode": 2})
                (frames / f"framedump_{sequence + 1}.png").write_bytes(
                    b"test png " + str(sequence).encode())
            trace = root / "route.jsonl"
            trace.write_text("".join(json.dumps(row) + "\n" for row in rows))
            result = retain_route_screenshots(frames, trace, root)
            self.assertEqual(result["status"], "retained")
            self.assertEqual(result["screenshots"], len(marker_names))
            mapping = json.loads((root / "screenshots/mapping.json").read_text())
            self.assertEqual(mapping["frame_offset"], 0)
            self.assertIn("not pixel or timing equivalence", mapping["alignment_scope"])

    def test_vs_rules_items_screenshots_map_each_source_route_marker(self):
        marker_names = (
            "cold_css_ready", "versus_submenu_after_css_parent", "root_menu_after_vs_back",
            "versus_submenu_for_rules", "vs_rules_first_entry", "vs_items_entry",
            "vs_items_one_bit_toggled", "vs_items_frequency_none", "vs_items_back_committed",
            "vs_rules_back_to_versus", "versus_back_to_main",
            "vs_rules_reentry_retained_items", "vs_rules_stock_three_selected",
            "css_after_rules_start_retained", "sss_after_rules_start",
            "sss_final_destination_selected", "vs_match_entered",
            "vs_match_after_180_ticks", "vs_no_contest_chord_sent",
            "results_no_contest", "css_after_results_retained")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            frames = root / "frames"
            frames.mkdir()
            rows = []
            for sequence, name in enumerate(marker_names):
                rows.append({"event": "scheduler_return", "sequence": sequence})
                rows.append({"event": name, "sequence": sequence,
                             "scene_kind": 8, "game_mode": 2})
                (frames / f"framedump_{sequence + 1}.png").write_bytes(
                    b"rules items png " + str(sequence).encode())
            trace = root / "route.jsonl"
            trace.write_text("".join(json.dumps(row) + "\n" for row in rows))
            result = retain_route_screenshots(
                frames, trace, root, route="vs_rules_items")
            self.assertEqual(result["status"], "retained")
            self.assertEqual(result["screenshots"], len(marker_names))
            mapping = json.loads((root / "screenshots/mapping.json").read_text())
            self.assertEqual(
                [row["event"] for row in mapping["screenshots"]], list(marker_names))


if __name__ == "__main__":
    unittest.main()
