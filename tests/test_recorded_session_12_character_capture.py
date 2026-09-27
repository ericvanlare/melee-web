import unittest

from tools.recorded_session_12_character_capture import (
    CaptureFailure,
    _consume_row,
)


class RecordedSession12CharacterCaptureTests(unittest.TestCase):
    def test_boundary_requires_whole_session_marker(self):
        with self.assertRaisesRegex(CaptureFailure, "unmarked boundary"):
            _consume_row(
                {
                    "event": "boundary",
                    "seq": 5,
                    "payload": {"boundary": "sss_enter", "match_index": 0},
                },
                {"boundaries": {}, "expected_match_index": 0},
                {},
            )

    def test_boundary_match_index_must_match_the_expected_setup(self):
        with self.assertRaisesRegex(CaptureFailure, "belongs to match 0; expected match 1"):
            _consume_row(
                {
                    "event": "boundary",
                    "seq": 5,
                    "payload": {
                        "whole_session": True,
                        "boundary": "sss_enter",
                        "match_index": 0,
                    },
                },
                {"boundaries": {}, "expected_match_index": 1},
                {},
            )

    def test_sss_cancel_counts_as_a_css_entry(self):
        latest = {
            "boundaries": {},
            "expected_match_index": 0,
            "css_polls": 100,
        }
        report = {}
        _consume_row(
            {
                "event": "boundary",
                "seq": 12,
                "payload": {
                    "whole_session": True,
                    "boundary": "css_cancel_enter",
                    "match_index": 0,
                    "slices": [],
                },
            },
            latest,
            report,
        )
        self.assertEqual(latest["boundaries"]["css_cancel_enter"], 12)
        self.assertEqual(latest["boundary_match_indices"]["css_cancel_enter"], 0)
        self.assertEqual(latest["css_entry_count"], 1)
        self.assertEqual(latest["css_polls"], 0)
        self.assertEqual(report["css_entries"][0]["payload"]["boundary"], "css_cancel_enter")


if __name__ == "__main__":
    unittest.main()
