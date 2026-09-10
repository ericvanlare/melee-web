"""Schema and first-divergence checks for retail transition evidence."""

import importlib.util
import struct
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "transition_compare", ROOT / "tools/compare_transition_trace.py")
COMPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMPARE)
from tools.transition_trace_format import decode_start_melee_data


def selection(stage=32, match_entry=False):
    player = {
        "ckind": 0, "slot_type": 0, "stocks": 4 if match_entry else 0,
        "color": 0, "slot": 0, "spawn": -1, "spawn_direction": 0,
        "sub_color": 0, "handicap": 9, "team": 0, "nametag": 120,
        "flags_c": 192 if match_entry else 64, "flags_d": 0,
        "cpu_kind": 4, "cpu_level": 1, "damage_10": 0, "damage_12": 0,
        "hp": 0, "attack_ratio_bits": "3f800000",
        "defense_ratio_bits": "3f800000", "model_scale_bits": "3f800000",
    }
    return {
        "rules": {
            "match_kind": 1 if match_entry else 0, "hud_layout": 4,
            "timer_enabled": False,
            "timer_counts_up": False, "friendly_fire": False,
            "is_stock": match_entry, "single_button": False,
            "disable_pausing": False, "is_vs": match_entry, "is_teams": 0,
            "item_frequency": -1 if match_entry else 2,
            "stage_kind": stage, "time_limit": 0,
            "item_mask": "ffffffffffffffff", "damage_ratio_bits": "3f800000",
            "game_speed_bits": "3f800000",
        },
        "players": [dict(player) for _ in range(4)],
    }


def rows(producer):
    header = {
        "record": "header", "schema": COMPARE.SCHEMA,
        "version": COMPARE.VERSION, "producer": producer,
        "game_revision": "GALE01r2",
    }
    if producer == "retail":
        header.update({
            "dol_sha1": COMPARE.EXPECTED_DOL_SHA1,
            "emulator_version": COMPARE.EXPECTED_EMULATOR_VERSION,
            "emulator_commit": COMPARE.EXPECTED_EMULATOR_COMMIT,
            "cpu_core": "Interpreter64", "cpu_thread": False,
            "fixed_rtc": 1704067200,
        })
    else:
        header.update({
            "source_revision": "0123456789abcdef0123456789abcdef01234567",
            "build_configuration": "browser-release",
            "input_recipe": "fixture-v1",
        })
    result = [header]
    routes = {3: "css", 7: "match"}
    for index, name in enumerate(COMPARE.EXPECTED_EVENTS):
        event = {
            "record": "event", "run": 0, "index": index, "event": name,
            "audio": {
                "active": True,
                "owner_epoch": 1 if index == 8 else 0,
                "stream": "sp_end.hps" if index == 8 else "menu01.hps",
            },
            "rng": 1234 + index,
        }
        if index in routes:
            event["route"] = routes[index]
        if index in (7, 8):
            event["selection"] = selection(match_entry=index == 8)
        if producer == "retail":
            event["retail_audio_diagnostics"] = {
                "stream_starts": 1 if index == 8 else 0,
                "stream_stops": 1 if index == 8 else 0,
                "driver_reinitializations": 0,
                "language_bank_initializations": 0,
            }
        result.append(event)
    return result


class TransitionTraceComparisonTests(unittest.TestCase):
    def test_big_endian_start_data_decoder(self):
        data = bytearray(0xF0)
        data[0] = (1 << 5) | (4 << 2)
        data[2] = 0x80
        data[4] = 0x40
        data[0xB] = 0xFF
        struct.pack_into(">H", data, 0xE, 32)
        struct.pack_into(">Q", data, 0x20, 0x1234)
        struct.pack_into(">I", data, 0x30, 0x3F800000)
        struct.pack_into(">I", data, 0x34, 0x3F000000)
        player_offset = 0x60
        data[player_offset:player_offset + 16] = bytes([
            8, 0, 4, 1, 0, 0xFF, 1, 2, 9, 0, 120, 0, 0x40, 0, 4, 1])
        struct.pack_into(">HHH", data, player_offset + 0x10, 3, 4, 5)
        struct.pack_into(">III", data, player_offset + 0x18,
                         0x3F800000, 0x40000000, 0x3F000000)
        decoded = decode_start_melee_data(bytes(data))
        self.assertEqual(decoded["rules"]["match_kind"], 1)
        self.assertEqual(decoded["rules"]["hud_layout"], 4)
        self.assertTrue(decoded["rules"]["is_stock"])
        self.assertTrue(decoded["rules"]["is_vs"])
        self.assertEqual(decoded["rules"]["item_frequency"], -1)
        self.assertEqual(decoded["rules"]["item_mask"], "0000000000001234")
        self.assertEqual(decoded["players"][0]["spawn"], -1)
        self.assertEqual(decoded["players"][0]["defense_ratio_bits"], "40000000")

    def test_equal_scoped_transition(self):
        result = COMPARE.compare(rows("retail"), rows("port"), 0)
        self.assertTrue(result["equivalent"])
        self.assertEqual(result["events_compared"], 9)
        self.assertEqual(result["checks"]["audio_continuity"], "pass")

    def test_reports_first_semantic_divergence(self):
        port = rows("port")
        port[-1]["selection"]["rules"]["stage_kind"] = 31
        result = COMPARE.compare(rows("retail"), port, 0)
        self.assertFalse(result["equivalent"])
        self.assertEqual(result["first_divergence"]["index"], 8)
        self.assertEqual(result["first_divergence"]["field"],
                         "$.selection.rules.stage_kind")
        self.assertEqual(result["checks"]["semantic_state"], "fail")

    def test_compares_rng_at_every_boundary(self):
        port = rows("port")
        port[3]["rng"] += 1
        result = COMPARE.compare(rows("retail"), port, 0)
        self.assertFalse(result["equivalent"])
        self.assertEqual(result["first_divergence"]["index"], 2)
        self.assertEqual(result["first_divergence"]["field"], "$.rng")

    def test_rejects_retail_menu_audio_restart(self):
        retail = rows("retail")
        retail[4]["retail_audio_diagnostics"]["stream_starts"] = 1
        with self.assertRaisesRegex(COMPARE.TraceError, "restarted or reinitialized"):
            COMPARE.compare(retail, rows("port"), 0)

    def test_rejects_incomplete_match_audio_handoff(self):
        retail = rows("retail")
        retail[-1]["retail_audio_diagnostics"]["stream_stops"] = 0
        with self.assertRaisesRegex(COMPARE.TraceError, "isolated stream handoff"):
            COMPARE.compare(retail, rows("port"), 0)

    def test_rejects_missing_lifecycle_event(self):
        port = rows("port")
        del port[5]
        with self.assertRaisesRegex(COMPARE.TraceError, "indices|sequence"):
            COMPARE.compare(rows("retail"), port, 0)

    def test_rejects_unpinned_retail_provenance(self):
        retail = rows("retail")
        retail[0]["emulator_version"] = "newer"
        with self.assertRaisesRegex(COMPARE.TraceError, "provenance mismatch"):
            COMPARE.compare(retail, rows("port"), 0)


if __name__ == "__main__":
    unittest.main()
