import copy
import json
from unittest import mock
import sys
import struct
import tempfile
import unittest
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.character_state_compare import (  # noqa: E402
    ComparisonError,
    _read_port,
    _source_prefix,
    _source_state,
    compare_decoded_prefix,
    compare_states,
)
from reference_observer_stream import (  # noqa: E402
    BOUNDARY,
    HEADER,
    MAGIC_U32,
    SCHEMA_VERSION,
    WHOLE_METADATA,
    WHOLE_SESSION_FLAG,
)


PAD = "0000002d00000008001e0000000000007f0000ff0000ff007fffff000000" + "00" * (12 * 66)


def pad(fill):
    return PAD[:60] + fill * (12 * 66)


def fighter(slot, kind, *, motion=2):
    return {
        "slot": slot,
        "kind": kind,
        "motion": motion,
        "animation": 3,
        "ground_air": 0,
        "facing_bits": "3f800000",
        "position_bits": ["00000000", "00000000", "00000000"],
        "velocity_bits": ["00000000", "00000000", "00000000"],
        "knockback_bits": ["00000000", "00000000", "00000000"],
        "animation_frame_bits": "00000000",
        "animation_speed_bits": "3f800000",
        "damage_bits": "00000000",
        "shield_bits": "00000000",
        "stocks": 4,
        "input_hex": "00" * 0x6C,
    }


def state(*, pad=PAD, entity_difference=False):
    primary = [fighter(slot, 10 + slot) for slot in range(4)]
    entities = [{**item, "entity_index": 0} for item in primary]
    nana = {**fighter(1, 99), "entity_index": 1}
    if entity_difference:
        nana["damage_bits"] = "3f800000"
    entities.append(nana)
    entities.sort(key=lambda item: (item["slot"], item["entity_index"]))
    return {
        "rng": 7,
        "match_frame": 12,
        "pad_state_hex": pad,
        "fighters": primary,
        "fighter_entities": entities,
    }


def mwro_record(event, sequence, source_tick, payload):
    header = HEADER.pack(
        MAGIC_U32, SCHEMA_VERSION, event, sequence, 0, 0, source_tick, 0,
        len(payload), zlib.crc32(payload) & 0xFFFFFFFF)
    return header + payload


def mwro_boundary(kind, sequence, source_tick, match_index=0):
    payload = (BOUNDARY.pack(kind, WHOLE_SESSION_FLAG, 0, 32, 0, 0) +
               struct.pack("<32I", *([0] * 32)) +
               WHOLE_METADATA.pack(match_index, kind, 0))
    return mwro_record(3, sequence, source_tick, payload)


def write_source_prefix(path):
    start = json.dumps({
        "whole_session": True,
        "source_revision": "GALE01r2",
        "match_count": 3,
    }, separators=(",", ":")).encode()
    path.write_bytes(
        mwro_record(2, 0, 0, start) +
        mwro_boundary(5, 1, 0) +
        mwro_boundary(6, 2, 0) +
        b"malformed trailing record that must not be read")


def typed_slice(name, flags=0):
    sizes = {"fighter_head": 0x100, "fighter_input_anim": 0x280,
             "fighter_damage_shield": 0x16C}
    return {"name": name, "flags": flags, "size": sizes[name], "address": 0x80580000}


def write_port_prefix(path, value=None):
    value = value or state()
    header = {
        "record": "header", "schema": "melee-web-port-session-diagnostic", "version": 2,
        "fighter_entities": "all_player_entity_slots", "frames_requested": 2,
        "comparison": "not_run", "cpu_observations": "not_captured", "draw_state": "not_captured",
    }
    setup = {"record": "session_match_enter_complete", **copy.deepcopy(value)}
    frame = {
        "record": "session_frame", "scene": 3, "index": 0,
        "supplied_inputs": ["00" * 11] * 4, **copy.deepcopy(value),
    }
    path.write_text("\n".join(json.dumps(row, separators=(",", ":"))
                            for row in (header, setup, frame)) + "\n")


class CharacterStateCompareTests(unittest.TestCase):
    def test_secondary_entity_difference_is_not_collapsed(self):
        expected = state()
        actual = state()
        actual["fighter_entities"][2]["damage_bits"] = "3f800000"
        difference = compare_states(expected, actual, reference_seq=41,
                                    reference_source_tick=9, port_index=17)
        self.assertEqual(difference["field"],
                         "fighter_entities[slot=1,entity_index=1].damage_bits")
        self.assertEqual(difference["reference_seq"], 41)
        self.assertEqual(difference["reference_source_tick"], 9)
        self.assertEqual(difference["port_index"], 17)
        self.assertIn("fighter_entities", difference["reference_state"])

    def test_entity_identity_switch_is_reported(self):
        expected = state()
        actual = state()
        for value in (expected, actual):
            value["fighter_entities"].append({**fighter(2, 98), "entity_index": 1})
            value["fighter_entities"].sort(key=lambda item: (item["slot"], item["entity_index"]))
        secondary_one = actual["fighter_entities"][2]
        secondary_two = actual["fighter_entities"][4]
        secondary_one["kind"], secondary_two["kind"] = secondary_two["kind"], secondary_one["kind"]
        difference = compare_states(expected, actual)
        self.assertEqual(difference["field"], "fighter_entities[slot=1,entity_index=1].kind")

    def test_missing_entity_field_is_rejected(self):
        expected = state()
        actual = state()
        del actual["fighter_entities"][-1]["input_hex"]
        with self.assertRaises(ComparisonError):
            compare_states(expected, actual)

    def test_earliest_pad_difference_wins_over_later_fighter_difference(self):
        expected_setup = state()
        actual_setup = copy.deepcopy(expected_setup)
        expected_ticks = [state(pad=pad("11")), state(pad=pad("22"))]
        actual_ticks = [copy.deepcopy(expected_ticks[0]), copy.deepcopy(expected_ticks[1])]
        actual_ticks[1]["pad_state_hex"] = pad("33")
        actual_ticks[1]["fighter_entities"][2]["damage_bits"] = "3f800000"
        report = compare_decoded_prefix(
            expected_setup, expected_ticks, actual_setup, actual_ticks,
            reference_setup_seq=3,
            reference_tick_metadata=[
                {"reference_seq": 4, "reference_source_tick": 0},
                {"reference_seq": 5, "reference_source_tick": 1},
            ])
        self.assertEqual(report["result"], "diverged")
        self.assertEqual(report["first_difference"]["field"], "pad_state_hex")
        self.assertEqual(report["first_difference"]["reference_source_tick"], 1)

    def test_short_port_prefix_is_unverified_not_a_gameplay_difference(self):
        expected_setup = state()
        actual_setup = copy.deepcopy(expected_setup)
        expected_ticks = [state(pad=pad("11")), state(pad=pad("22"))]
        report = compare_decoded_prefix(
            expected_setup, expected_ticks, actual_setup,
            [copy.deepcopy(expected_ticks[0])])
        self.assertEqual(report["result"], "prefix_unverified")
        self.assertFalse(report["complete"])
        self.assertIsNone(report["first_difference"])
        self.assertTrue(report["unverified_remainder"])

    def test_path_source_setup_aligns_tick_zero_and_stops_before_trailing_record(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.mwro"
            write_source_prefix(path)
            with mock.patch("tools.character_state_compare._source_state",
                            side_effect=lambda payload, context: (copy.deepcopy(state()),
                                                                  payload["source_tick"])):
                result = _source_prefix(path, 0, 1)
        self.assertEqual(result["setup"]["seq"], 1)
        self.assertEqual(result["ticks"][0]["seq"], 2)
        self.assertEqual(result["ticks"][0]["source_tick"], 0)
        self.assertTrue(result["prefix_complete"])

    def test_path_port_parser_retains_secondary_entity_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "retail-port.jsonl"
            write_port_prefix(path)
            result = _read_port(path, 0, None)
        self.assertEqual(result["setup"][0], 2)
        entities = result["frames"][0][1]["fighter_entities"]
        self.assertIn((1, 1), [(row["slot"], row["entity_index"]) for row in entities])
        self.assertEqual(result["scene3_total"], 1)
        self.assertFalse(result["trace_complete"])

    def test_source_duplicate_and_missing_companion_slices_are_rejected(self):
        duplicate = [typed_slice("fighter_head"), typed_slice("fighter_head")]
        with self.assertRaisesRegex(ComparisonError, "duplicate fighter_head"):
            _source_state({"slices": duplicate}, "duplicate")
        missing = [typed_slice("fighter_head")]
        with self.assertRaisesRegex(ComparisonError, "lacks fighter_input_anim"):
            _source_state({"slices": missing}, "missing")

    def test_primary_and_entity_zero_fields_must_match(self):
        actual = state()
        actual["fighter_entities"][1]["motion"] += 1
        with self.assertRaisesRegex(ComparisonError, "entity_index 0"):
            compare_states(state(), actual)


if __name__ == "__main__":
    unittest.main()
