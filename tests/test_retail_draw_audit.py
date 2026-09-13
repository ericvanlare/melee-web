"""Sparse retail camera-traversal audit coverage."""

from copy import deepcopy
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tools")]

from retail_replay_validation import CaptureError, _validate_capture  # noqa: E402
from retail_draw_audit import validate_draw_rows  # noqa: E402
from test_retail_replay_validation import candidate  # noqa: E402


def _pad_snapshot():
    config = bytes.fromhex(
        "0000002d00000008001e0000000000007f0000ff0000ff007fffff000000")
    return (config + bytes(12 * 66)).hex()


def _capture(frame_count=3):
    rows = candidate(frame_count)
    rows[0]["version"] = 2
    for key in ("pad_lib_hex", "pad_master_hex", "pad_game_hex"):
        del rows[1][key]
    for row in rows[1:-1]:
        row["pad_state_hex"] = _pad_snapshot()
    return _validate_capture(rows, "draw-audit-fixture")


def _draw(capture, ordinal, source_index):
    frame = capture.frames[source_index]
    before = {key: deepcopy(frame[key]) for key in (
        "rng", "match_frame", "scene_frame", "fighters", "pad_state_hex")}
    before["scene_frame"] = (before["scene_frame"] + 1) & 0xFFFFFFFF
    return {
        "record": "draw",
        "index": ordinal,
        "source_index": source_index,
        "before": before,
        "after": deepcopy(before),
    }


class RetailDrawAuditTests(unittest.TestCase):
    def test_sparse_cadence_reports_actual_gap_and_indices(self):
        capture = _capture()
        rows = [_draw(capture, 0, 0), _draw(capture, 1, 2)]
        result = validate_draw_rows(capture, rows)
        self.assertEqual(result["status"], "declared_state_unchanged")
        self.assertEqual(result["traversals_observed"], 2)
        self.assertEqual(result["source_ticks_total"], 3)
        self.assertEqual(result["source_ticks_compared"], 2)
        self.assertEqual(result["source_ticks_not_drawn"], [1])
        self.assertEqual(result["source_indices"], [0, 2])
        self.assertIn("cadence equivalence", result["scope"])

    def test_sparse_change_reports_draw_and_source_indices(self):
        capture = _capture()
        rows = [_draw(capture, 0, 0), _draw(capture, 1, 2)]
        rows[1]["after"]["rng"] ^= 1
        result = validate_draw_rows(capture, rows)
        self.assertEqual(result["status"], "declared_state_changed")
        self.assertEqual(result["first_change"]["index"], 1)
        self.assertEqual(result["first_change"]["source_index"], 2)
        self.assertEqual(result["first_change"]["field"], "rng")

    def test_legacy_rows_remain_one_per_tick(self):
        capture = _capture()
        rows = []
        for index, frame in enumerate(capture.frames):
            row = _draw(capture, index, index)
            del row["source_index"]
            rows.append(row)
        result = validate_draw_rows(capture, rows)
        self.assertEqual(result["status"], "declared_state_unchanged")
        self.assertEqual(result["source_indices"], [0, 1, 2])
        self.assertEqual(result["source_ticks_not_drawn"], [])

    def test_sparse_order_final_and_schema_errors_are_rejected(self):
        capture = _capture()
        valid = [_draw(capture, 0, 0), _draw(capture, 1, 2)]
        cases = []

        missing_final = [_draw(capture, 0, 0), _draw(capture, 1, 1)]
        cases.append(missing_final)
        cases.append([_draw(capture, 0, 0), _draw(capture, 1, 0)])
        cases.append([_draw(capture, 0, 1), _draw(capture, 1, 0)])
        mixed = deepcopy(valid)
        del mixed[1]["source_index"]
        cases.append(mixed)
        out_of_range = deepcopy(valid)
        out_of_range[1]["source_index"] = 3
        cases.append(out_of_range)
        nested = deepcopy(valid)
        nested[0]["before"]["source_index"] = 0
        cases.append(nested)
        for rows in cases:
            with self.subTest(rows=rows):
                with self.assertRaises(CaptureError):
                    validate_draw_rows(capture, rows)

    def test_wrong_source_before_state_is_rejected(self):
        capture = _capture()
        rows = [_draw(capture, 0, 0), _draw(capture, 1, 2)]
        rows[1]["before"]["match_frame"] = capture.frames[1]["match_frame"]
        with self.assertRaisesRegex(CaptureError, "draw entry differs"):
            validate_draw_rows(capture, rows)


if __name__ == "__main__":
    unittest.main()
