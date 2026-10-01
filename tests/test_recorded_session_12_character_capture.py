import threading
import unittest

from tools.recorded_session_12_character_capture import (
    CaptureFailure,
    Driver,
    ROSTER_ICON,
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

    def test_return_css_advances_the_expected_source_match_index(self):
        latest = {"boundaries": {}, "expected_match_index": 0}
        report = {}
        _consume_row(
            {
                "event": "boundary",
                "seq": 12,
                "payload": {
                    "whole_session": True,
                    "boundary": "return_css",
                    "match_index": 0,
                    "slices": [],
                },
            },
            latest,
            report,
        )
        self.assertEqual(latest["expected_match_index"], 1)
        _consume_row(
            {
                "event": "boundary",
                "seq": 13,
                "payload": {
                    "whole_session": True,
                    "boundary": "pad_poll",
                    "match_index": 1,
                    "slices": [],
                },
            },
            latest,
            report,
        )
        self.assertEqual(latest["boundaries"]["pad_poll"], 13)

    def test_costume_setting_cycles_from_the_source_observed_color(self):
        latest = {"doors": [{"icon": ROSTER_ICON["ROY"], "costume": 3}]}
        driver = Driver(None, latest, threading.Event(), readiness_only=False)
        driver.wait = lambda predicate, label, seconds=12.0: (
            None if predicate() else self.fail(f"source state was not reached: {label}")
        )

        def tap(_port, button, *, label, second_port_button=None):
            self.assertEqual(button, "X")
            self.assertIsNone(second_port_button)
            latest["doors"][0]["costume"] = (latest["doors"][0]["costume"] + 1) % 4

        driver.tap = tap
        driver.set_costume(0, "ROY", 0)
        self.assertEqual(latest["doors"][0]["costume"], 0)


if __name__ == "__main__":
    unittest.main()
