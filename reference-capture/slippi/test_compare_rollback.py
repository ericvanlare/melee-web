# SPDX-License-Identifier: MIT
"""Sensitivity controls for exact finalized rollback timeline comparison."""
from dataclasses import replace
from pathlib import Path
import sys
import tempfile
import json
import unittest

from compare_rollback import compare_timelines, main
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.slippi_format import SlippiHeader, SlippiPreFrame, SlippiPostFrame, SlippiFrame, SlippiTimeline


def timeline():
    header = SlippiHeader(1, 1, (3, 16, 0, 0), bytes(0x138), 0x1234,
                          ({"port": 1, "character_id": 8, "stocks": 4},
                           {"port": 2, "character_id": 8, "stocks": 4}),
                          False, False, 2, 2, {0x37: 63, 0x38: 80})
    frames = []
    for index in range(3):
        inputs = tuple(SlippiPreFrame(index, port, False, 0x4321, 14, (0, 0), 0,
                                     (0, 0), (0, 0), 0, 0, 0, (0, 0), (0, 0), (0, 0))
                       for port in (1, 2))
        states = tuple(SlippiPostFrame(index, port, False, 8, 14, (0, 0), 0, 0, 0,
                                     0 if index == 2 and port == 1 else 4,
                                     (0, 0, 0, 0, 0), 0, 1, 2, 0) for port in (1, 2))
        frames.append(SlippiFrame(index, 0x6543, index, inputs, states))
    return SlippiTimeline(header, tuple(frames), 2, 0, 3, -1)


def change_frame(base, index, **fields):
    frames = list(base.frames)
    frames[index] = replace(frames[index], **fields)
    return replace(base, frames=tuple(frames))


class FinalizedRollbackComparisonTests(unittest.TestCase):
    def test_speculative_revision_counts_do_not_change_finalized_state(self):
        base = timeline()
        result = compare_timelines(base, replace(base, duplicate_updates=12))
        self.assertEqual(result["result"], "passed")
        self.assertEqual(result["compared_frames"], 3)

    def test_one_float_bit_fails_at_first_changed_frame_and_field(self):
        base = timeline()
        expected = list(base.frames[1].expected)
        expected[0] = replace(expected[0], position_bits=(1, 0))
        result = compare_timelines(base, change_frame(base, 1, expected=tuple(expected)))
        self.assertEqual(result["compared_frames"], 1)
        self.assertEqual(result["first_divergence"]["expected_frame"], 1)
        self.assertEqual(result["first_divergence"]["field"], "frame.expected[0].position_bits[0]")

    def test_input_rng_clock_initial_rules_and_outcome_are_sensitive(self):
        base = timeline()
        inputs = list(base.frames[1].inputs)
        inputs[1] = replace(inputs[1], processed_buttons=1)
        cases = [
            (change_frame(base, 1, inputs=tuple(inputs)), "frame.inputs[1].processed.buttons"),
            (change_frame(base, 1, start_random_seed=7), "frame.start_random_seed"),
            (change_frame(base, 1, scene_frame_counter=7), "frame.scene_frame_counter"),
            (replace(base, header=replace(base.header, random_seed=7)), "initial_match.random_seed"),
            (replace(base, game_end_method=2), "outcome.game_end_method"),
        ]
        for changed, field in cases:
            with self.subTest(field=field):
                result = compare_timelines(base, changed)
                self.assertEqual(result["result"], "failed")
                self.assertEqual(result["first_divergence"]["field"], field)

    def test_pre_step_position_and_facing_bits_are_compared(self):
        base = timeline()
        for field, value in (("position_bits", (1, 0)), ("facing_bits", 1)):
            inputs = list(base.frames[1].inputs)
            inputs[0] = replace(inputs[0], **{field: value})
            result = compare_timelines(base, change_frame(base, 1, inputs=tuple(inputs)))
            self.assertEqual(result["compared_frames"], 1)
            expected = "frame.inputs[0].pre_position_bits[0]" if field == "position_bits" else "frame.inputs[0].pre_facing_bits"
            self.assertEqual(result["first_divergence"]["field"], expected)

    def test_missing_and_unfinalized_frames_are_refused(self):
        base = timeline()
        for changed in (replace(base, finalized_through=1),
                        replace(base, frames=(base.frames[0], base.frames[2])),
                        change_frame(base, 1, inputs=base.frames[1].inputs[:1]),
                        change_frame(base, 1, start_random_seed=None),
                        replace(base, game_end_method=None)):
            with self.subTest(changed=changed):
                with self.assertRaises(ValueError):
                    compare_timelines(base, changed)

    def test_identically_missing_raw_input_is_not_complete_evidence(self):
        base = timeline()
        inputs = tuple(replace(value, physical_buttons=None) for value in base.frames[1].inputs)
        incomplete = change_frame(base, 1, inputs=inputs)
        with self.assertRaises(ValueError):
            compare_timelines(incomplete, incomplete)

    def test_cli_retains_malformed_capture_failure_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            capture = root / "malformed.slp"
            capture.write_bytes(b"invalid replay")
            output = root / "failure.json"
            arguments = ["--expected", str(capture), "--actual", str(capture), "--out", str(output)]
            self.assertEqual(main(arguments), 1)
            report = json.loads(output.read_text())
            self.assertIn("SlippiFormatError", report["failure"])
            with self.assertRaises(FileExistsError):
                main(arguments)


if __name__ == "__main__":
    unittest.main()
