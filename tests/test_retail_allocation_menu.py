"""Focused checks for the cold-boot retail menu driver.

These tests compile and inspect the generated GDB/Python source only.  They
do not launch Dolphin or claim a cold-boot gameplay/reference result.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import types
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import retail_allocation_menu as driver  # noqa: E402


class RetailAllocationMenuTests(unittest.TestCase):
    def test_driver_compiles_and_exposes_bounded_cold_route(self):
        source = driver.render_cold_boot_driver()
        compile(source, "cold_boot_menu.py", "exec")
        self.assertIn("cold_boot_to_sss()", source)
        self.assertIn("def cold_boot_to_css()", source)
        self.assertIn("def cold_boot_to_sss()", source)
        self.assertIn("COLD_BOOT_TICK_LIMIT=7200", source)
        self.assertIn("CARD_PROMPT_TICK_LIMIT=1800", source)
        self.assertIn("CARD_PROMPT_PULSE_TICKS=32", source)
        self.assertIn("OPENING_MOVIE_TICK_LIMIT=7200", source)
        self.assertIn("SCHEDULER_RETURN=0x80390eb4", source)
        self.assertEqual(driver.scheduler_breakpoint_command(),
                         "hbreak *0x80390eb4")

    def test_driver_routes_only_through_pinned_source_scenes(self):
        source = driver.render_cold_boot_driver()
        for scene in (
                "SCENE_MEMCARD", "SCENE_OPENING_MOVIE", "SCENE_TITLE",
                "SCENE_MENU", "SCENE_CSS", "SCENE_SSS"):
            self.assertIn(scene, source)
        self.assertIn("_cold_boot_wait_menu(0)", source)
        self.assertIn("move_menu_selection(1)", source)
        self.assertIn("_cold_boot_wait_menu(2)", source)
        self.assertIn("_cold_boot_enter_sss()", source)
        self.assertIn("_cold_boot_wait_sss_ready()", source)
        self.assertIn("SSS_CURSOR_PROC=0x8025A310", source)
        self.assertIn("_cold_boot_check_character_availability()", source)
        self.assertIn("pulse(0,'A',settle=30)", source)
        cold_sss = source.split("def _cold_boot_enter_sss():", 1)[1].split(
            "def cold_boot_to_sss():", 1)[0]
        self.assertNotIn("set_cpu_mode(", cold_sss)
        self.assertNotIn("set_cpu_level(", cold_sss)
        # The final preparation still uses the existing source CSS/SSS body.
        self.assertIn("set_rules_via_original_menu()", source)
        self.assertIn("select_stage(EXPECTED_STAGE)", source)

    def test_cold_sss_readiness_uses_source_cursor_and_cooldown(self):
        source = driver.render_cold_boot_driver()
        readiness = source.split("def _cold_boot_wait_sss_ready():", 1)[1].split(
            "def _cold_boot_check_character_availability():", 1)[0]
        self.assertIn("SSS_CURSOR_PROC=0x8025A310", source)
        self.assertIn("_cold_boot_sss_cursor()", readiness)
        self.assertIn("u32(0x804D6CA4)", readiness)
        self.assertIn("cursor is not None and cooldown==0", readiness)
        self.assertNotIn("mem(0x804D6BC8,2)", readiness)
        self.assertIn("SSS_ENTRY_TICK_LIMIT", readiness)
        self.assertIn("step(1)", readiness)

    def test_sss_readiness_accepts_stale_main_menu_cooldown(self):
        source = driver.render_cold_boot_driver()
        readiness = source.split("def _cold_boot_wait_sss_ready():", 1)[1].split(
            "def _cold_boot_check_character_availability():", 1)[0]
        ticks = []
        menu_reads = []
        observed = {"cursor": 0x804D1234, "sss_cooldown": 0,
                    "menu_cooldown": 5}

        def fake_u32(address):
            self.assertEqual(address, 0x804D6CA4)
            return observed["sss_cooldown"]

        def fake_mem(address, size):
            menu_reads.append((address, size))
            return observed["menu_cooldown"].to_bytes(size, "big")

        namespace = {
            "SCENE_SSS": driver.SCENE_SSS,
            "SSS_ENTRY_TICK_LIMIT": 1,
            "scene_kind": lambda: driver.SCENE_SSS,
            "_cold_boot_scene_name": lambda kind: "GS_SSS",
            "_cold_boot_sss_cursor": lambda: observed["cursor"],
            "u32": fake_u32,
            "mem": fake_mem,
            "step": lambda count: ticks.append(count),
        }
        exec(compile("def _cold_boot_wait_sss_ready():" + readiness,
                      "cold_boot_menu.py", "exec"), namespace)
        namespace["_cold_boot_wait_sss_ready"]()
        self.assertEqual(ticks, [])
        self.assertEqual(menu_reads, [])

    def test_missing_character_is_bounded_source_availability_failure(self):
        source = driver.render_cold_boot_driver()
        check = source.split("def _cold_boot_check_character_availability():", 1)[1].split(
            "def cold_boot_to_css():", 1)[0]
        self.assertIn("0x803F0B24", check)
        self.assertIn("owned card baseline must already contain the unlock state", check)
        self.assertIn("raise RuntimeError", check)

    def test_driver_has_no_game_memory_or_register_writes_or_state_load(self):
        source = driver.render_cold_boot_driver()
        self.assertNotIn("write_memory(", source)
        self.assertNotIn("put_register", source)
        self.assertNotIn("set $", source)
        self.assertNotIn("load_state", source.lower())
        self.assertNotIn("savestate", source.lower())
        # The only write operation in the generated program is the ordinary
        # controller pipe; evidence is written to the host path separately.
        self.assertIn("os.open(PIPES/f'pad{port+1}'", source)
        self.assertNotRegex(source, r"gdb\.execute\([^\n]*write")
        self.assertIn("_check_collector_failure()", source)
        continue_pos = source.index("gdb.execute('continue'")
        self.assertLess(continue_pos,
                        source.index("_check_collector_failure()", continue_pos))

    def test_input_command_log_is_explicit_and_reproducible(self):
        source = driver.render_cold_boot_driver()
        self.assertEqual(driver.COMMAND_LOG_NAME,
                         "cold-boot-input-commands.jsonl")
        self.assertIn("COMMAND_LOG_PATH", source)
        self.assertIn("_record_input_command(port,text)", source)
        self.assertIn("'event':'pad_command'", source)
        self.assertIn("'scene_kind':scene_kind()", source)
        self.assertIn("'scene_frame':u32(0x80479D58)", source)

    def test_generated_command_log_is_real_jsonl_with_newline_records(self):
        source = driver.render_cold_boot_driver()
        prefix = source.split(
            "\n# Fresh-DOL entry is complete only when source CSS/SSS is observed.",
            1,
        )[0]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "target.json"
            target.write_text(json.dumps({
                "expected_setup": {
                    "time_limit_seconds": 60,
                    "players": [{"stocks": 4, "rumble_enabled": True}],
                    "disable_pausing": False,
                    "stage": 32,
                },
            }), encoding="utf-8")
            old_work = os.environ.get("MELEE_REPLAY_REFERENCE_WORK")
            old_target = os.environ.get("MELEE_CHECKPOINT_TARGET_JSON")
            old_gdb = sys.modules.get("gdb")
            sys.modules["gdb"] = types.ModuleType("gdb")
            os.environ["MELEE_REPLAY_REFERENCE_WORK"] = str(root / "evidence")
            os.environ["MELEE_CHECKPOINT_TARGET_JSON"] = str(target)
            namespace = {}
            try:
                exec(compile(prefix, "cold_boot_menu.py", "exec"), namespace)
                namespace["scene_kind"] = lambda: driver.SCENE_MEMCARD
                namespace["u32"] = lambda address: 17
                namespace["_record_input_command"](0, "PRESS A")
                log = root / "evidence" / driver.COMMAND_LOG_NAME
                lines = log.read_text(encoding="utf-8").splitlines()
                self.assertEqual(len(lines), 1)
                self.assertEqual(json.loads(lines[0]), {
                    "event": "pad_command", "port": 1, "command": "PRESS A",
                    "scene_kind": driver.SCENE_MEMCARD, "scene_frame": 17,
                })
            finally:
                if "COMMAND_LOG" in namespace:
                    namespace["COMMAND_LOG"].close()
                if old_work is None:
                    os.environ.pop("MELEE_REPLAY_REFERENCE_WORK", None)
                else:
                    os.environ["MELEE_REPLAY_REFERENCE_WORK"] = old_work
                if old_target is None:
                    os.environ.pop("MELEE_CHECKPOINT_TARGET_JSON", None)
                else:
                    os.environ["MELEE_CHECKPOINT_TARGET_JSON"] = old_target
                if old_gdb is None:
                    sys.modules.pop("gdb", None)
                else:
                    sys.modules["gdb"] = old_gdb

    def test_source_driver_aliases_stay_identical(self):
        self.assertIs(driver.render_driver, driver.render_source_driver)
        self.assertEqual(driver.render_driver(), driver.render_cold_boot_driver())
        self.assertEqual(driver.render_source_driver(), driver.DRIVER_SOURCE)


if __name__ == "__main__":
    unittest.main()
