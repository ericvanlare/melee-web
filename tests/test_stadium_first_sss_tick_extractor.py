"""Exact retained original SSS PAD/source-tick extraction controls."""
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from stadium_go_prefix import (  # noqa: E402
    FIRST_CSS_STREAM_SHA256,
    FIRST_SSS_CONSUMED_PAD_SEQUENCE,
    FIRST_SSS_SCHEDULER_END_SEQUENCE,
    FIRST_SSS_TICK_INPUT_BYTES,
    FIRST_SSS_TICK_INPUT_MAGIC,
    FIRST_SSS_TICK_INPUT_VERSION,
    StadiumGoPrefixError,
    _first_sss_scheduler_end_rows,
    extract_stadium_first_sss_consumed_pad_tick,
)
from reference_observer_stream import iter_records  # noqa: E402


class FirstSssConsumedPadTickExtractorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stream = os.environ.get("MELEE_WEB_STADIUM_SSS_TICK_RAW")
        cls.status = os.environ.get("MELEE_WEB_STADIUM_SSS_TICK_STATUS")
        if cls.stream is not None or cls.status is not None:
            if not cls.stream or not cls.status:
                raise AssertionError(
                    "Configured retained SSS fixture requires both nonempty "
                    "MELEE_WEB_STADIUM_SSS_TICK_RAW and "
                    "MELEE_WEB_STADIUM_SSS_TICK_STATUS")

    def test_retained_row1604_and_scheduler_end1608_are_exact(self):
        if not self.stream or not self.status:
            self.skipTest("retained SSS original stream/status were not configured")
        result = extract_stadium_first_sss_consumed_pad_tick(self.stream, self.status)
        self.assertEqual(result["schema"],
                         "melee-web-stadium-first-sss-consumed-pad-tick-diagnostic")
        self.assertEqual(result["provenance"]["observer_sha256"],
                         FIRST_CSS_STREAM_SHA256)
        self.assertEqual(result["provenance"]["consumed_pad_sequence"],
                         FIRST_SSS_CONSUMED_PAD_SEQUENCE)
        self.assertEqual(result["provenance"]["scheduler_end_sequence"],
                         FIRST_SSS_SCHEDULER_END_SEQUENCE)
        self.assertEqual(result["source_scheduler_end"]["expected"]["scene_frame"], 0)
        self.assertEqual(result["source_scheduler_end"]["expected"]["scene_kind"], 9)
        self.assertEqual(result["source_scheduler_end"]["expected"]["random_seed_hex"],
                         "3bb84c53")
        self.assertEqual(result["source_scheduler_end"]["expected"]["scene_routing_getters"],
                         {"current_game_mode": 2, "previous_game_mode": 1,
                          "current_scene_index": 1, "previous_scene_index": 0})
        self.assertEqual(len(result["source_scheduler_end"]["expected"]["pad_state_hex"]), 1644)
        self.assertFalse(result["source_scheduler_end"]["setup_profile_verified_by_observer"])
        self.assertEqual(len(result["input_bundle_bytes"]), FIRST_SSS_TICK_INPUT_BYTES)
        self.assertEqual(result["input_bundle_bytes"][:8], FIRST_SSS_TICK_INPUT_MAGIC)
        self.assertEqual(int.from_bytes(result["input_bundle_bytes"][8:12], "big"),
                         FIRST_SSS_TICK_INPUT_VERSION)
        self.assertEqual(result["input_bundle"]["port_status_hex"], [
            "0000000000000000000000", "0000000000000000000000",
            "00000000000000000000ff", "00000000000000000000ff",
        ])

    def test_outer_payload_counter_mismatch_and_missing_queue_slice_refuse(self):
        if not self.stream or not self.status:
            self.skipTest("retained SSS original stream/status were not configured")
        rows = list(iter_records(self.stream))
        consume = next(row for row in rows if row.get("seq") == FIRST_SSS_CONSUMED_PAD_SEQUENCE)
        tick = next(row for row in rows if row.get("seq") == FIRST_SSS_SCHEDULER_END_SEQUENCE)
        digest = FIRST_CSS_STREAM_SHA256
        malformed_counter = dict(tick)
        malformed_counter["draw_ordinal"] = tick["draw_ordinal"] + 1
        with self.assertRaisesRegex(StadiumGoPrefixError, "outer/payload counters"):
            _first_sss_scheduler_end_rows(consume, malformed_counter, digest)
        bad_inventory = dict(tick)
        bad_payload = dict(tick["payload"])
        bad_payload["slices"] = [item for item in bad_payload["slices"]
                                 if item.get("tag") != 2]
        bad_inventory["payload"] = bad_payload
        with self.assertRaisesRegex(StadiumGoPrefixError, "required source tag=2 is missing"):
            _first_sss_scheduler_end_rows(consume, bad_inventory, digest)


if __name__ == "__main__":
    unittest.main()
