"""Bounded production receiver/replay controls; no native process is launched."""
from __future__ import annotations
import copy
import importlib.util
import json
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = Path(os.environ.get("MELEE_WEB_TEST_SOURCE_ROOT", ROOT))
sys.path[:0] = [str(SOURCE_ROOT / "tools"), str(SOURCE_ROOT / "scripts"),
               str(SOURCE_ROOT / "tests"), str(SOURCE_ROOT / "reference-capture/dolphin")]
from owned_test_workspace import OwnedWorkspaceTests
from reference_observer_stream import iter_records
from reference_input_stream import InputStreamError
from retail_input_plan import NEUTRAL_PAD
from reference_versus_sequence_capture import raw_pad
spec = importlib.util.spec_from_file_location("recorded_stadium_driver", ROOT / "scripts/capture_sd_reference_prefix.py")
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)

V6 = Path(os.environ["MELEE_STADIUM_REPLAY_V6"]) if os.environ.get("MELEE_STADIUM_REPLAY_V6") else None
RECIPE = Path(os.environ["MELEE_STADIUM_REPLAY_RECIPE"]) if os.environ.get("MELEE_STADIUM_REPLAY_RECIPE") else None

@unittest.skipUnless(V6 is not None and RECIPE is not None, "retained v6 controls require explicit evidence paths")
class StadiumRecordedReplayTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.workspace = cls.new_workspace(SOURCE_ROOT, "stadium-recorded-replay-controls-")
        cls.menus, _ = driver.menu_actions(RECIPE)
        cls.rows = list(iter_records(V6 / "observer.bin", max_bytes=64*1024*1024, max_records=16384))
        cls.expected = json.loads((V6 / "report.json").read_text())["driver"]
        cls.receipt = driver.stadium_replay_preflight(V6 / "inputs.mwri", cls.workspace / "absent-capture", cls.workspace / "manifest.json")

    def receiver(self):
        return driver.StadiumGoPrefixReceiver(self.menus, recorded_input=True)

    def consume(self, mutate=None):
        receiver = self.receiver()
        for source in self.rows:
            row = copy.deepcopy(source) if mutate else source
            if mutate:
                mutate(row)
            receiver.accept(row)
        return receiver.finish()

    def test_whole_v6_passive_receiver_preserves_exact_route(self):
        with patch.object(driver.DualPipeController, "__init__", side_effect=AssertionError("controller forbidden")), patch.object(driver, "drive_authored_css_sss", side_effect=AssertionError("adaptive driver forbidden")):
            result = self.consume()
        self.assertEqual(result["menu_owner_sequences"], self.expected["menu_owner_sequences"])
        self.assertEqual(result["consumed_source_pads"], self.expected["consumed_source_pads"])
        self.assertEqual(result["progress_rows"], self.expected["progress_rows"])
        self.assertEqual(result["source_tick_rows"], self.expected["source_tick_rows"])
        self.assertEqual(result["draw_return_rows"], self.expected["draw_return_rows"])
        proof = result["recorded_input_observation"]
        self.assertFalse(proof["driver_intentions_used"])
        self.assertEqual(proof["target_neutral_release"], 1876)
        self.assertGreaterEqual(proof["target_neutral_polls_before_confirm"], self.menus["sss"]["target_stable_neutral_polls"])
        (self.workspace / "passive-v6-receipt.json").write_text(json.dumps(result, sort_keys=True))

    def test_missing_consumed_neutral_release_refuses(self):
        def mutate(row):
            if row["seq"] in (1868, 1876, 1884):
                slot = next(item for item in row["payload"]["slices"] if item["tag"] == 3)
                slot["hex"] = raw_pad(x=35) + slot["hex"][22:]
        with self.assertRaises(driver.SdDiagnosticError):
            self.consume(mutate)

    def test_duplicate_confirm_after_release_refuses(self):
        def mutate(row):
            if row["seq"] == 1916:
                slot = next(item for item in row["payload"]["slices"] if item["tag"] == 3)
                slot["hex"] = raw_pad(buttons=["A"]) + slot["hex"][22:]
        with self.assertRaisesRegex(driver.SdDiagnosticError, "confirm repeated"):
            self.consume(mutate)

    def test_wrong_target_context_cannot_prove_release(self):
        def mutate(row):
            if 1850 < row["seq"] < 1892 and row["payload"].get("boundary") == "pad_poll":
                for item in row["payload"]["slices"]:
                    if item["tag"] == 41:
                        item["hex"] = "00"
                    elif item["tag"] == 42:
                        item["hex"] = "04"
                        item["address"] = 0x803F06D0 + 0xB
        with self.assertRaises(driver.SdDiagnosticError):
            self.consume(mutate)

    def test_actual_v6_source_preflight_is_bound_and_read_only(self):
        self.assertEqual(self.receipt["summary"]["events"], 4706)
        self.assertEqual(self.receipt["summary"]["sha256"], driver.STADIUM_REPLAY_INPUT_SHA256)
        self.assertFalse((self.workspace / "absent-capture").exists())

    def test_manifest_inside_absent_output_refuses_before_creation(self):
        output = self.workspace / "absent-manifest-capture"
        with self.assertRaisesRegex(driver.SdDiagnosticError, "manifest.*outside"):
            driver.stadium_replay_preflight(V6 / "inputs.mwri", output, output / "relocated.json")
        self.assertFalse(output.exists())

    def test_existing_output_collision_refuses(self):
        with self.assertRaisesRegex(driver.SdDiagnosticError, "output already exists"):
            driver.stadium_replay_preflight(V6 / "inputs.mwri", self.workspace, self.workspace / "manifest.json")

    def status(self, **changes):
        value = dict(version=1, mode="replay", events=4706, complete=True, invalid=False, error=None)
        value.update(changes)
        path = self.workspace / "replay-status.json"
        path.write_text(json.dumps(value))
        return path

    def test_complete_replay_status_needs_all_controller_samples_not_footer_poll(self):
        result = driver.stadium_replay_complete(self.receipt, self.status())
        self.assertEqual(result["events"], 4706)
        self.assertEqual(result["mode"], "replay")

    def test_record_mode_wrong_count_incomplete_and_invalid_status_refuse(self):
        for changes in (dict(mode="record"), dict(events=4707), dict(events=4705), dict(complete=False), dict(invalid=True), dict(error="input poll port or emulated tick diverged")):
            with self.subTest(changes=changes), self.assertRaises(InputStreamError):
                driver.stadium_replay_complete(self.receipt, self.status(**changes))

    def test_waiter_checks_replay_mode_and_rejects_unknown_mode(self):
        observer = self.workspace / "observer-status.json"
        primary = dict(state="completed", completed=True, invalid=False, error=None, event_count=2935, last_seq=2934)
        # read_status owns its full observer schema; isolate this test to the
        # production waiter's mode selection and real native status validator.
        with patch.object(driver, "read_status", return_value=primary):
            observer.write_text("{}")
            _, status = driver.wait_transform_terminal_statuses(observer, self.status(), driver.time.monotonic()+1, input_mode="replay")
            self.assertEqual(status["mode"], "replay")
            with self.assertRaises(InputStreamError):
                driver.wait_transform_terminal_statuses(observer, self.status(mode="record"), driver.time.monotonic()+1, input_mode="replay")
        with self.assertRaises(driver.SdDiagnosticError):
            driver.wait_transform_terminal_statuses(observer, self.status(), 0, input_mode="other")

    def test_replay_refuses_before_launch_without_comparator(self):
        with patch.object(driver, "validate_reference_build_manifest", return_value={}), patch.object(driver, "menu_actions", return_value=(self.menus, "hash")), patch.object(driver.subprocess, "Popen", side_effect=AssertionError("native launch forbidden")):
            with self.assertRaisesRegex(driver.SdDiagnosticError, "comparator is not integrated"):
                driver._run(dolphin="none", disc="none", profile="none", menu_recipe=RECIPE, output=self.workspace, build_manifest="none", timeout=1, replay_receipt=self.receipt)


class StadiumReplayPortableTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.workspace = cls.new_workspace(SOURCE_ROOT, "stadium-replay-portable-")

    def test_absent_output_manifest_collision_refuses_before_source_access(self):
        output = self.workspace / "absent-output"
        with self.assertRaisesRegex(driver.SdDiagnosticError, "manifest.*outside"):
            driver.stadium_replay_preflight(self.workspace / "missing.mwri", output, output / "manifest.json")
        self.assertFalse(output.exists())

    def test_existing_output_refuses_before_source_access(self):
        with self.assertRaisesRegex(driver.SdDiagnosticError, "output already exists"):
            driver.stadium_replay_preflight(self.workspace / "missing.mwri", self.workspace, self.workspace / "manifest.json")

    def test_cli_wires_strict_comparator_for_replay(self):
        from stadium_recorded_prefix_compare import compare_stadium_common_wire
        arguments = []
        for name in ("dolphin", "disc", "profile", "menu-recipe", "output", "build-manifest", "input-replay"):
            arguments.extend(["--" + name, "test-unused-" + name])
        with patch.object(driver, "run", return_value={}) as run, patch("builtins.print"):
            self.assertEqual(driver.main(arguments), 0)
        self.assertIs(run.call_args.kwargs["common_wire_comparator"], compare_stadium_common_wire)

    def test_unknown_input_mode_refuses(self):
        with self.assertRaises(driver.SdDiagnosticError):
            driver.wait_transform_terminal_statuses(self.workspace / "observer.json", self.workspace / "input.json", 0, input_mode="other")

    def test_prelaunch_failure_preserves_structured_comparison_receipt(self):
        error = ValueError("wire differs")
        error.receipt = {"decision": "REFUSE", "first_difference": {"seq": 1}}
        output = self.workspace / "structured-refusal"
        with patch.object(driver, "menu_actions", return_value=({"scope": "stadium_go_prefix"}, "hash")), patch.object(driver, "_run", side_effect=error):
            with self.assertRaises(ValueError):
                driver.run(dolphin="none", disc="none", profile="none", menu_recipe="none", output=output, build_manifest="none")
        self.assertEqual(json.loads((output / "failure.json").read_text())["comparison_receipt"], error.receipt)

if __name__ == "__main__":
    unittest.main()
