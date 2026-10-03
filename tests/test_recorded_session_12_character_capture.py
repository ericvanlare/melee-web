import threading
import struct
import unittest

from tools.recorded_session_12_character_capture import (
    CaptureFailure,
    Driver,
    ROSTER_ICON,
    _capture_exit_code,
    _consume_row,
    _decode_css_start_data,
    _decode_team_result,
    _validate_team_setup,
)


class RecordedSession12CharacterCaptureTests(unittest.TestCase):
    @staticmethod
    def team_setup(teams=(0, 1), item_frequency=-1,
                   item_mask_hex="fffffffffffbffff", costumes=(0, 3)):
        raw = bytearray(0x138)
        raw[0] = 0x20  # stock match
        raw[2] = 0x80  # stock mode
        raw[4] = 0x40  # ordinary VS profile
        raw[8] = 1  # Teams
        raw[0x0B] = item_frequency & 0xFF
        struct.pack_into(">H", raw, 0x0E, 0x20)  # Final Destination
        raw[0x20:0x28] = bytes.fromhex(item_mask_hex)
        raw[0x2C:0x30] = bytes.fromhex("3f800000")
        for index, team in enumerate(teams):
            base = 0x60 + index * 0x24
            raw[base:base + 5] = bytes((8, index, 3, costumes[index], index + 1))
            raw[base + 9] = team
        raw[0x60 + 0x24 + 14] = 4
        raw[0x60 + 0x24 + 15] = 1
        for index in range(2, 6):
            raw[0x60 + index * 0x24 + 1] = 3  # inactive source player
        return raw.hex()

    def test_team_setup_requires_human_cpu_and_opposing_source_teams(self):
        setup = _validate_team_setup(self.team_setup(), 0)
        self.assertEqual(_validate_team_setup(self.team_setup(), 1)["player_teams"], [0, 1])
        self.assertEqual(_validate_team_setup(self.team_setup(), 2)["player_teams"], [0, 1])
        self.assertEqual(setup["player_teams"], [0, 1])
        self.assertEqual([player["character_kind"] for player in setup["players"]], [8, 8])
        self.assertEqual([player["player_type"] for player in setup["players"]], [0, 1])
        self.assertEqual(setup["players"][1]["cpu_level"], 1)
        with self.assertRaisesRegex(CaptureFailure, "opposing authored teams"):
            _validate_team_setup(self.team_setup(teams=(1, 1)), 0)

    def test_team_setup_requires_source_derived_mario_team_colors(self):
        with self.assertRaisesRegex(CaptureFailure, "source-derived Mario team colors"):
            _validate_team_setup(self.team_setup(costumes=(0, 1)), 0)
        with self.assertRaisesRegex(CaptureFailure, "source-derived Mario team colors"):
            _validate_team_setup(self.team_setup(costumes=(1, 3)), 0)

    def test_team_setup_rejects_a_frequency_that_was_not_committed_as_none(self):
        with self.assertRaisesRegex(CaptureFailure, "source team rules differ"):
            _validate_team_setup(self.team_setup(item_frequency=2), 0)

    def test_team_setup_requires_the_original_items_row_zero_mask(self):
        with self.assertRaisesRegex(CaptureFailure, "row-zero item mask"):
            _validate_team_setup(
                self.team_setup(item_mask_hex="fffffffeffffffff"), 0)

    def test_team_route_capture_result_is_a_successful_cli_outcome(self):
        self.assertEqual(_capture_exit_code("original_vs_team_results_css_capture_complete"), 0)
        self.assertEqual(_capture_exit_code("fail"), 1)

    def test_team_result_requires_the_source_no_contest_outcome(self):
        raw = bytearray(0x28)
        raw[4] = 7
        raw[0x0D] = 2
        raw[0x10:0x12] = bytes((0, 1))
        self.assertEqual(_decode_team_result(raw.hex()),
                         {"outcome": 7, "winners": [0, 1]})
        raw[4] = 2
        with self.assertRaisesRegex(CaptureFailure, "did not publish No Contest"):
            _decode_team_result(raw.hex())

    def test_repeated_results_process_samples_form_one_result_per_match(self):
        raw = bytearray(0x28)
        raw[4] = 7
        latest = {"team_route": True, "boundaries": {}, "expected_match_index": 0}
        report = {}

        def row(sequence, boundary, match_index):
            payload = {"whole_session": True, "boundary": boundary,
                       "match_index": match_index, "slices": []}
            if boundary == "results_gobj":
                payload["slices"].append({"name": "result", "hex": raw.hex()})
            return {"event": "boundary", "seq": sequence, "payload": payload}

        _consume_row(row(10, "results_gobj", 0), latest, report)
        _consume_row(row(11, "results_gobj", 0), latest, report)
        self.assertEqual(len(latest["team_results"]), 1)
        self.assertEqual(len(latest["team_result_gobj_rows"]), 1)
        _consume_row(row(12, "return_css", 0), latest, report)
        _consume_row(row(13, "results_gobj", 1), latest, report)
        self.assertEqual([result["match_index"] for result in latest["team_results"]], [0, 1])

    def test_css_live_state_decodes_original_team_toggle_and_colors(self):
        css_data = bytearray(0x148)
        css_data[0x18] = 1
        css_data[0x79] = 0
        css_data[0x9D] = 1
        latest = {}
        _consume_row({
            "event": "sample",
            "payload": {"slices": [{"name": "menu_css_live_state", "hex": css_data.hex()}]},
        }, latest, {})
        self.assertEqual(latest["team_state"], {"is_teams": 1, "player_teams": [0, 1]})

    def test_css_context_decodes_pre_entry_retained_start_data(self):
        start = bytearray.fromhex(self.team_setup())
        start[2] = 0x06  # CSS pre-entry copy has not enabled stock mode yet.
        start[4] = 0x83  # source CSSData before gmVsMelee_EnterVs sets is_vs.
        css_data = bytearray(0x148)
        css_data[0x10:] = start
        latest = {}
        _consume_row({
            "event": "boundary",
            "seq": 20,
            "payload": {
                "whole_session": True,
                "boundary": "return_css",
                "match_index": 0,
                "slices": [{"name": "menu_css_context", "hex": css_data.hex()}],
            },
        }, latest, {})
        retained = _decode_css_start_data(latest["css_start_data"])
        self.assertEqual(retained["item_mask_hex"], "fffffffffffbffff")
        self.assertEqual([player["costume"] for player in retained["players"]], [0, 3])
        self.assertFalse(retained["is_stock"])

    def test_css_retained_decoder_rejects_match_entry_profile(self):
        start = bytearray.fromhex(self.team_setup())
        with self.assertRaisesRegex(CaptureFailure, "pre-entry VS profile byte"):
            _decode_css_start_data(start.hex())

    def test_css_door_decoder_keeps_team_and_human_toggle_bounds_distinct(self):
        doors = bytearray(4 * 36)
        p2 = 36
        struct.pack_into(">ff", doors, p2 + 0x14, -19.4, -13.4)
        struct.pack_into(">ff", doors, p2 + 0x1C, -11.4, -6.0)
        latest = {}
        _consume_row({
            "event": "sample",
            "payload": {"slices": [{"name": "menu_css_doors", "hex": doors.hex()}]},
        }, latest, {})
        self.assertAlmostEqual(latest["doors"][1]["left"], -19.4, places=4)
        self.assertAlmostEqual(latest["doors"][1]["right"], -13.4, places=4)
        self.assertAlmostEqual(latest["doors"][1]["team_left"], -11.4, places=4)
        self.assertAlmostEqual(latest["doors"][1]["team_right"], -6.0, places=4)

    def test_source_state_records_are_immutable_snapshots(self):
        latest = {
            "players": [{"kind": 0}], "doors": [{"kind": 0}],
            "cursors": {0: {"x": 1.0, "y": 2.0}},
            "models": {0: {"x": 3.0, "y": 4.0}},
            "sliders": {0: {"dirty": False}},
            "team_state": {"is_teams": 1, "player_teams": [0, 0]},
        }
        driver = Driver(None, latest, threading.Event(), readiness_only=False)
        driver.state("before")
        latest["doors"][0]["kind"] = 3
        latest["cursors"][0]["x"] = 99.0
        latest["team_state"]["player_teams"][1] = 1
        self.assertEqual(driver.steps[0]["doors"][0]["kind"], 0)
        self.assertEqual(driver.steps[0]["cursors"][0]["x"], 1.0)
        self.assertEqual(driver.steps[0]["team_state"]["player_teams"], [0, 0])

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
