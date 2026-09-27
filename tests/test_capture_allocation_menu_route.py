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


if __name__ == "__main__":
    unittest.main()
