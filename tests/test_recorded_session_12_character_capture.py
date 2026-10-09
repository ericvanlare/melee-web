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


class EntityPrefixMenuCaptureTests(unittest.TestCase):
    """Synthetic source controls; no original menu acceptance claim."""
    def test_actual_setup_validator_accepts_only_declared_single_cpu9_roster(self):
        import copy
        from test_retail_entity_prefix import synthetic_prefix
        from tools.recorded_session_12_character_capture import _validate_setup
        candidate, _ = synthetic_prefix()
        raw = candidate["setup_hex"]
        self.assertEqual([p["character_kind"] for p in
                          _validate_setup(raw, 0, entity_prefix=True)["players"]],
                         [15, 14, 8, 2])
        with self.assertRaises(CaptureFailure):
            _validate_setup(raw, 0)
        with self.assertRaises(CaptureFailure):
            _validate_setup(raw, 1, entity_prefix=True)
        for offset, value in ((0x60, 8), (0x61, 0), (0x62, 3),
                              (0x63, 1), (0x64, 2), (0x6E, 3), (0x6F, 8)):
            broken = bytearray.fromhex(raw)
            broken[offset] = value
            with self.subTest(offset=offset), self.assertRaises(CaptureFailure):
                _validate_setup(broken.hex(), 0, entity_prefix=True)

    def test_actual_cpu_pickup_gate_rejects_unselected_foreign_and_held_owner(self):
        import copy
        from unittest.mock import Mock
        base = {"players": [{"kind": 1}] * 4,
                "doors": [{"kind": 1, "icon": 1}] * 4,
                "models": {2: {"state": 0, "source_slot": 2, "x": -20.0, "y": 14.0}},
                "cursors": {0: {"port": 0, "state": 0}}}
        for field, value in (("icon", 25), ("model_owner", 1),
                             ("model_slot", 3), ("cursor_port", 1), ("cursor_held", 1)):
            state = copy.deepcopy(base)
            if field == "icon": state["doors"][2]["icon"] = value
            if field == "model_owner": state["models"][2]["state"] = value
            if field == "model_slot": state["models"][2]["source_slot"] = value
            if field == "cursor_port": state["cursors"][0]["port"] = value
            if field == "cursor_held": state["cursors"][0]["state"] = value
            driver = Driver(Mock(), state, threading.Event(), readiness_only=False, entity_prefix=True)
            driver.move = Mock()
            with self.subTest(field=field), self.assertRaisesRegex(CaptureFailure, "source-owned"):
                driver.select_cpu(2, "MARIO")
            driver.move.assert_not_called()

    def test_actual_driver_named_branch_requires_exit_setup_and_never_results(self):
        from unittest.mock import Mock
        state = {"boundaries": {"sss_enter": 10, "sss_exit": 12, "setup": 13},
                 "setup_records": [{}], "entity_prefix_complete": True}
        driver = Driver(Mock(), state, threading.Event(), readiness_only=False, entity_prefix=True)
        driver._boot_menus = Mock()
        driver.configure_lineup = Mock()
        driver.enter_fd = Mock()
        driver.state = Mock()
        driver._play_match = Mock(side_effect=AssertionError("prefix cannot enter Results"))
        driver.boot_and_drive()
        self.assertTrue(state["driver_complete"])
        driver.configure_lineup.assert_called_once_with(0, initial=True)
        driver.enter_fd.assert_called_once_with(choose_stage=True, before=10)
        driver._play_match.assert_not_called()
        state.pop("driver_complete"); state["boundaries"].pop("sss_exit")
        driver.wait = lambda predicate, label, **kw: predicate()
        # Use the actual wait with a declared already-stopped owner: missing exit
        # must fail rather than a fixed delay being treated as source acceptance.
        driver.wait = Driver.wait.__get__(driver)
        driver.stop.set()
        driver.boot_and_drive()
        self.assertIn("driver_error", state)
        self.assertNotIn("driver_complete", state)

    def test_actual_prefix_finalizer_preserves_context_and_rejects_bad_footer_batch(self):
        import copy
        from test_retail_entity_prefix import synthetic_prefix
        from tools.recorded_session_12_character_capture import _finish_entity_prefix
        candidate, rows = synthetic_prefix()
        latest = {"setup_records": [{}]}
        result = _finish_entity_prefix(rows, latest)
        self.assertTrue(result["diagnostic_prefix_complete"])
        self.assertFalse(result["complete"])
        self.assertFalse(result["whole_session_equivalent"])
        self.assertEqual(result["prefix_interval"]["source_observations"], 60)
        for change in ("footer", "extra_setup", "draw_gap"):
            broken = copy.deepcopy(rows); state = copy.deepcopy(latest)
            if change == "footer": broken[-1]["payload"]["natural"] = True
            if change == "extra_setup": state["setup_records"].append({})
            if change == "draw_gap":
                next(row for row in broken if row.get("payload", {}).get("boundary") == "draw_return")["draw_ordinal"] += 1
            with self.subTest(change=change), self.assertRaises((CaptureFailure, ValueError)):
                _finish_entity_prefix(broken, state)

    def test_prefix_is_mutually_exclusive_and_source_roster_icons_are_distinct(self):
        from tools.recorded_session_12_character_capture import LINEUPS, ENTITY_PREFIX_LINEUP, ROSTER
        self.assertEqual(len(LINEUPS), 3)
        self.assertEqual(ENTITY_PREFIX_LINEUP, ("JIGGLYPUFF", "ICE_CLIMBERS", "MARIO", "FOX"))
        self.assertEqual((ROSTER_ICON["JIGGLYPUFF"], ROSTER_ICON["ICE_CLIMBERS"]), (20, 12))
        self.assertEqual((ROSTER["JIGGLYPUFF"][0], ROSTER["ICE_CLIMBERS"][0]), (15, 14))
        for option in ({"readiness_only": True}, {"readiness_only": False, "team_route_only": True}):
            with self.assertRaisesRegex(CaptureFailure, "separate"):
                Driver(None, {}, threading.Event(), entity_prefix=True, **option)


    def test_actual_held_puck_owner_predicate_accepts_source_pair_and_rejects_aliases(self):
        import copy
        base = {"cursors": {0: {"port": 0, "state": 1, "held": 2}},
                "models": {2: {"source_slot": 2, "state": 1}}}
        Driver(None, base, threading.Event(), readiness_only=False,
               entity_prefix=True)._require_prefix_held_puck(2, 0)
        for group, key, value in (("cursors", "port", 1), ("cursors", "state", 0),
                                  ("cursors", "held", 3), ("models", "source_slot", 3),
                                  ("models", "state", 2)):
            state = copy.deepcopy(base)
            state[group][0 if group == "cursors" else 2][key] = value
            with self.subTest(group=group, key=key), self.assertRaisesRegex(CaptureFailure, "foreign"):
                Driver(None, state, threading.Event(), readiness_only=False,
                       entity_prefix=True)._require_prefix_held_puck(2, 0)
        # Historical mode does not acquire a new predicate or metadata requirement.
        Driver(None, {}, threading.Event(), readiness_only=False)._require_prefix_held_puck(2, 0)


    @staticmethod
    def _actual_owned_try():
        import ast
        from pathlib import Path
        import tools.recorded_session_12_character_capture as implementation
        tree = ast.parse(Path(implementation.__file__).read_text())
        main = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "main")
        return next(n for n in main.body if isinstance(n, ast.Try) and n.finalbody)

    def _owned_context(self, output):
        from types import SimpleNamespace
        from unittest.mock import Mock
        import tools.recorded_session_12_character_capture as implementation
        context = dict(implementation.__dict__)
        process = Mock(pid=424242, returncode=0)
        process.poll.return_value = 0
        process.wait.return_value = 0
        absent = Mock(); absent.exists.return_value = False
        context.update(args=SimpleNamespace(entity_prefix=True), proc=process, stop=Mock(),
                       log=Mock(), out=output, report={"result": "diagnostic_entity_prefix_complete"},
                       stream=absent, input_stream=absent, status=absent, input_status=absent,
                       report_path=output / "report.json", latest={"setup_records": []},
                       driver=None, prefix_records=[])
        return context

    def test_actual_finally_rejects_live_driver_and_preserves_first_failure(self):
        import ast
        from pathlib import Path
        from unittest.mock import Mock
        from test_retail_entity_prefix import owned_scratch
        owned = self._actual_owned_try()
        block = ast.fix_missing_locations(ast.Module(body=owned.finalbody, type_ignores=[]))
        for primary in (None, "retained source mismatch"):
            with owned_scratch("prefix-owned-close-") as output:
                context = self._owned_context(output)
                thread = Mock(); thread.is_alive.return_value = True
                context["thread"] = thread
                if primary: context["report"].update(result="fail", error=primary)
                exec(compile(block, "actual-main-finally", "exec"), context)
                report = context["report"]
                self.assertEqual(report["result"], "fail")
                self.assertFalse(report["diagnostic_prefix_complete"])
                self.assertFalse(report["recipe_artifacts_admitted"])
                if primary: self.assertEqual(report["error"], primary)
                context["log"].close.assert_called_once()
                context["proc"].wait.assert_called_once_with(timeout=5)
                thread.join.assert_called_once_with(2)
                self.assertFalse((output / "entity-prefix.mwrc").exists())

    def test_actual_protected_setup_failure_retires_direct_child_without_recipe(self):
        import ast
        from types import SimpleNamespace
        from unittest.mock import Mock
        from test_retail_entity_prefix import owned_scratch
        owned = self._actual_owned_try()
        block = ast.fix_missing_locations(ast.Module(body=[owned], type_ignores=[]))
        with owned_scratch("prefix-setup-failure-") as output:
            context = self._owned_context(output)
            process = context["proc"]
            context.update(proc=None, command=["synthetic-owned-child"], env={}, p1=None, p2=None)
            context["subprocess"] = SimpleNamespace(Popen=Mock(return_value=process), STDOUT=-2)
            context["capture"] = SimpleNamespace(DualPipeController=Mock(side_effect=RuntimeError("controller setup rejected")))
            exec(compile(block, "actual-main-try", "exec"), context)
            self.assertEqual(context["report"]["error"], "controller setup rejected")
            self.assertEqual(context["report"]["result"], "fail")
            self.assertFalse(context["report"]["recipe_artifacts_admitted"])
            process.wait.assert_called_once_with(timeout=5)
            context["log"].close.assert_called_once()
            self.assertFalse((output / "entity-prefix.mwrc").exists())

    def test_actual_driver_error_path_never_publishes_candidate(self):
        import ast
        from unittest.mock import Mock
        from test_retail_entity_prefix import owned_scratch
        owned = self._actual_owned_try()
        gate = next(n for n in owned.body if isinstance(n, ast.If) and
                    "driver_error" in ast.unparse(n.test))
        reduced = ast.Try(body=[gate], handlers=owned.handlers, orelse=[], finalbody=owned.finalbody)
        block = ast.fix_missing_locations(ast.Module(body=[reduced], type_ignores=[]))
        with owned_scratch("prefix-driver-failure-") as output:
            context = self._owned_context(output)
            context["latest"]["driver_error"] = "declared driver source failure"
            context["driver"] = Mock(steps=[])
            exec(compile(block, "actual-driver-error-try", "exec"), context)
            self.assertEqual(context["report"]["error"], "declared driver source failure")
            self.assertFalse(context["report"]["recipe_artifacts_admitted"])
            self.assertFalse((output / "entity-prefix.mwrc").exists())

    def test_reused_direct_process_cleanup_timeout_is_bounded_and_retained(self):
        import subprocess
        import json
        from test_retail_entity_prefix import owned_scratch
        from tools.recorded_session_12_character_capture import _close_entity_prefix
        with owned_scratch("prefix-cleanup-timeout-") as output:
            context = self._owned_context(output)
            process = context["proc"]
            process.poll.return_value = None
            process.wait.side_effect = subprocess.TimeoutExpired("synthetic-owned-child", 5)
            context["report"].update(result="fail", error="primary source failure")
            _close_entity_prefix(process, None, context["stop"], context["log"], output, context["report"])
            self.assertEqual([c.kwargs for c in process.wait.call_args_list], [{"timeout": 5}, {"timeout": 5}])
            process.terminate.assert_called_once(); process.kill.assert_called_once()
            self.assertEqual(context["report"]["error"], "primary source failure")
            self.assertEqual(context["report"]["cleanup_errors"][0]["owner"], "native")
            self.assertIsNotNone(json.loads((output / "cleanup.json").read_text())["error"])
            context["log"].close.assert_called_once()

    def test_actual_input_footer_and_independent_sidecar_are_both_required(self):
        import json
        from test_reference_input_stream import event
        from reference_input_stream import HEADER, InputStreamError
        from tools.recorded_session_12_character_capture import _validate_entity_prefix_input
        from test_retail_entity_prefix import owned_scratch
        with owned_scratch("prefix-input-finalization-") as output:
            stream, status = output / "inputs.mwri", output / "inputs.json"
            good = HEADER.pack(b"MWRI", 1, 16, 40, 0) + event(0, 100, 0) + event(1, 100, 0xffffffff, 2)
            schema = {"version": 1, "mode": "record", "events": 1, "complete": True, "invalid": False, "error": None}
            stream.write_bytes(good); status.write_text(json.dumps(schema))
            self.assertTrue(_validate_entity_prefix_input(stream, status)["complete"])
            for patch in ({"complete": False}, {"events": 2}, {"invalid": True}):
                status.write_text(json.dumps(dict(schema, **patch)))
                with self.assertRaises(InputStreamError): _validate_entity_prefix_input(stream, status)
            status.write_text(json.dumps(schema)); stream.write_bytes(good[:-40] + event(1, 100, 0xffffffff, 3))
            with self.assertRaises(InputStreamError): _validate_entity_prefix_input(stream, status)

    def test_actual_final_report_write_failure_preserves_primary_after_close(self):
        import ast
        from unittest.mock import Mock
        from test_retail_entity_prefix import owned_scratch
        owned = self._actual_owned_try()
        block = ast.fix_missing_locations(ast.Module(body=owned.finalbody, type_ignores=[]))
        with owned_scratch("prefix-report-write-") as output:
            context = self._owned_context(output)
            context["report"].update(result="fail", error="first source error")
            context["_write_json"] = Mock(side_effect=OSError("final report write rejected"))
            with self.assertRaisesRegex(CaptureFailure, "first source error"):
                exec(compile(block, "actual-report-write-finally", "exec"), context)
            context["proc"].wait.assert_called_once_with(timeout=5)
            context["log"].close.assert_called_once()
            self.assertFalse(context["report"]["recipe_artifacts_admitted"])
