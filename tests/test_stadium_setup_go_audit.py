"""Synthetic byte faults exercise the existing strict Setup comparison."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import stadium_setup_go_audit as AUDIT


def fixture():
    expected = {
        "players": [
            {"port": port, "character_kind": 8, "costume": 2 - port,
             "stocks": 4, "player_type": 0, "rumble_enabled": True}
            for port in (1, 2)
        ],
        "stage": 3, "match_kind": 1, "timer_enabled": False,
        "timer_counts_up": False, "time_limit_seconds": 0, "is_stock": True,
        "disable_pausing": False, "is_teams": False, "item_frequency": -1,
        "item_mask_hex": "ffffffffffffffff", "damage_ratio_bits": "3f800000",
        "game_speed_bits": "3f800000",
    }
    raw = bytearray(0x138)
    raw[0], raw[2], raw[4], raw[0x0B], raw[0x0F] = 0x20, 0x80, 0x40, 0xFF, 3
    raw[0x20:0x28] = b"\xff" * 8
    for offset in (0x2C, 0x30, 0x34):
        raw[offset:offset + 4] = bytes.fromhex("3f800000")
    for index in range(6):
        base = 0x60 + index * 0x24
        raw[base + 1] = 3
        if index < 2:
            raw[base:base + 5] = bytes((8, 0, 4, 1 - index, 0))
            raw[base + 12] = 0x80
    return expected, raw


class StadiumSetupGoAuditTests(unittest.TestCase):
    def test_declared_fields_match_without_mutating_expected(self):
        expected, raw = fixture()
        before = copy.deepcopy(expected)
        result = AUDIT.compare_setup_fields(expected, raw.hex())
        self.assertTrue(result["declared_fields_match"])
        self.assertEqual(result["expected_setup_from_source_spec"], result["decoded_setup_fields"])
        self.assertEqual(expected, before)

    def test_missing_expected_field_and_duplicate_expected_port_refused(self):
        for mutation in ("missing", "port"):
            expected, raw = fixture()
            if mutation == "missing": expected.pop("stage")
            else: expected["players"][1]["port"] = 1
            with self.subTest(mutation=mutation), self.assertRaises(AUDIT.StadiumSetupAuditError) as caught:
                AUDIT.compare_setup_fields(expected, raw.hex())
            self.assertEqual(caught.exception.code, "invalid_expected_setup")

    def test_first_mismatch_retains_expected_and_observed_values(self):
        expected, raw = fixture()
        raw[0x0F] = 5
        raw[0x0B] = 0
        with self.assertRaises(AUDIT.StadiumSetupAuditError) as caught:
            AUDIT.compare_setup_fields(expected, raw.hex())
        self.assertEqual(caught.exception.code, "declared_setup_field_mismatch")
        self.assertEqual(caught.exception.details, {"path": "stage", "expected": 3, "actual": 5})

    def test_size_and_pointer_guards_remain_active(self):
        for mutation in ("short", "long", "pointer"):
            expected, raw = fixture()
            if mutation == "short": raw = raw[:-1]
            if mutation == "long": raw += b"\0"
            if mutation == "pointer": raw[0x38] = 0x80
            with self.subTest(mutation=mutation), self.assertRaises(AUDIT.StadiumSetupAuditError) as caught:
                AUDIT.compare_setup_fields(expected, raw.hex())
            self.assertEqual(caught.exception.code, "existing_setup_decoder_guard_failed")

    def test_ordinary_vs_and_port_guards_remain_active(self):
        for offset, value in ((4, 0), (1, 2), (0x14, 1), (0x64, 4), (0x85, 3), (0xF1, 0)):
            expected, raw = fixture()
            raw[offset] = value
            with self.subTest(offset=offset), self.assertRaises(AUDIT.StadiumSetupAuditError) as caught:
                AUDIT.compare_setup_fields(expected, raw.hex())
            self.assertEqual(caught.exception.code, "existing_setup_decoder_guard_failed")


if __name__ == "__main__":
    unittest.main()
