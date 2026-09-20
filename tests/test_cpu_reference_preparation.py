import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts import prepare_cpu_reference as preparation  # noqa: E402
from tools.cpu_reference_scenarios import (  # noqa: E402
    ACTIVE_THREE_PLAYER_SCENARIO_ID,
    HISTORICAL_THREE_PLAYER_SCENARIO_ID,
    LONG_CAPTURE_TICKS,
    LONG_MATCH_TICKS,
    canonical,
    catalog_summary,
    get_scenario,
    make_input_plan,
    scenario_ids,
    scenario_target,
    validate_input_plan,
    validate_scenario,
)
from tools.retail_cpu_menu_prepare import render_source_driver  # noqa: E402


RAW_GCPAD = """[GCPad1]
Device = Pipe/0/pad1
Buttons/A = `Button A`
[GCPad2]
Device = Pipe/0/pad2
Buttons/A = `Button A`
"""


class CpuReferencePreparationTests(unittest.TestCase):
    def test_source_driver_keeps_proven_route_and_read_only_team(self):
        driver = render_source_driver()
        compile(driver, "retail_cpu_menu_prepare.py", "exec")
        self.assertNotIn("print('CHORD'", driver)
        self.assertNotIn("write_memory(", driver)
        self.assertIn("return_to_css()", driver)
        self.assertIn("command(0,'PRESS START')", driver)
        self.assertIn("'team':raw[9]", driver)
        self.assertIn("EXPECTED_TEAMS", driver)
        self.assertNotIn("main-window Save State", driver)

    def test_all_scenarios_emit_native_v3_four_port_plans(self):
        for scenario_id in (
            "mario-human-vs-fox-cpu1-final-destination",
            "ganondorf-human-vs-mario-cpu1-final-destination",
            ACTIVE_THREE_PLAYER_SCENARIO_ID,
            "fox-human-vs-mario3-marth6-falco8-dream-land",
        ):
            scenario = get_scenario(scenario_id)
            plan = make_input_plan(scenario, ticks=8)
            validate_input_plan(plan)
            self.assertEqual(plan["version"], 3)
            self.assertEqual(plan["active_player_count"], len(scenario["players"]))
            self.assertTrue(all(len(row) == 4 for row in plan["frames"]))
            self.assertEqual(plan["controlled_ports"], [1])

    def test_catalog_preserves_two_four_player_hashes_and_retains_old_three_player(self):
        self.assertEqual(
            scenario_ids(),
            [
                "fox-human-vs-mario3-marth6-falco8-dream-land",
                "ganondorf-human-vs-mario-cpu1-final-destination",
                "mario-human-vs-fox-cpu1-final-destination",
                ACTIVE_THREE_PLAYER_SCENARIO_ID,
            ],
        )
        summary = catalog_summary()
        self.assertEqual(summary["player_counts"], [2, 3, 4])
        self.assertEqual(summary["distinct_cpu_levels"], [1, 3, 5, 6, 8, 9])
        self.assertEqual(summary["characters"], ["Falco", "Fox", "Ganondorf", "Mario", "Marth"])
        self.assertEqual(len(summary["scenario_ids"]), 4)

        expected_hashes = {
            "mario-human-vs-fox-cpu1-final-destination":
                "47eea34486f2b42ace1c7fa006e5632ba4ab50cc5f25b34e33a323c586c2f7f8",
            "fox-human-vs-mario3-marth6-falco8-dream-land":
                "ce662f38032aebc6c4d72ea7d7e020b1907b354cd5e25840e0d1aaede5452f6f",
        }
        for scenario_id, expected in expected_hashes.items():
            digest = hashlib.sha256(canonical(get_scenario(scenario_id))).hexdigest()
            self.assertEqual(digest, expected)

        historical = get_scenario(HISTORICAL_THREE_PLAYER_SCENARIO_ID)
        self.assertEqual(historical["scenario_id"], HISTORICAL_THREE_PLAYER_SCENARIO_ID)
        self.assertEqual(historical["rules"]["time_limit_seconds"], 60)
        self.assertEqual(historical["human_workload"]["match_ticks"], 3600)
        self.assertEqual(historical["human_workload"]["capture_cap_ticks"], 4200)
        self.assertEqual(
            hashlib.sha256(canonical(historical)).hexdigest(),
            "bd42c323923ac116197e4e26e9c3c4ec4bd074c97302dfa56e3d4f238aff9d40",
        )

    def test_three_player_120_second_plan_uses_full_authored_horizon(self):
        scenario = get_scenario(ACTIVE_THREE_PLAYER_SCENARIO_ID)
        workload = scenario["human_workload"]
        self.assertEqual(workload["match_ticks"], LONG_MATCH_TICKS)
        self.assertEqual(workload["capture_cap_ticks"], LONG_CAPTURE_TICKS)
        self.assertEqual(workload["phase_offset"], 37)
        self.assertIn("Time, 2:00", scenario["source_menu_route"][0])

        plan = make_input_plan(scenario)
        self.assertEqual(len(plan["frames"]), 123 + LONG_CAPTURE_TICKS)
        self.assertEqual(plan["first_frame"], -123)
        self.assertEqual(plan["source_stage"], 8)
        self.assertEqual(plan["source_characters"], [9, 20, 8])
        validate_input_plan(plan)
        with self.assertRaises(ValueError):
            make_input_plan(scenario, ticks=LONG_CAPTURE_TICKS + 1)
        with self.assertRaises(ValueError):
            validate_input_plan(plan, LONG_CAPTURE_TICKS - 1)

        target = scenario_target(scenario)
        setup = target["expected_setup"]
        self.assertTrue(setup["timer_enabled"])
        self.assertEqual(scenario["rules"]["time_limit_seconds"], 120)
        self.assertEqual(setup["time_limit_seconds"], 120)
        self.assertEqual(setup["stage"], 8)

        mismatched_timer = get_scenario(ACTIVE_THREE_PLAYER_SCENARIO_ID)
        mismatched_timer["rules"]["time_limit_seconds"] = 60
        with self.assertRaises(ValueError):
            validate_scenario(mismatched_timer)

    def _owned_inputs(self, root):
        donor = root / "donor"
        raw = donor / "raw-template-user"
        (raw / "Config").mkdir(parents=True)
        (raw / "Pipes").mkdir()
        (raw / "Config" / "GCPadNew.ini").write_text(RAW_GCPAD)
        (raw / "Config" / "Dolphin.ini").write_text("[Core]\n")
        snapshot = donor / "base.sav"
        snapshot.write_bytes(b"owned snapshot")
        gc = donor / "checkpoint-gc"
        gc.mkdir()
        (gc / "SRAM.raw").write_bytes(b"owned card")
        provenance = donor / "base-provenance.json"
        provenance.write_text(json.dumps({"dol_sha1": "0" * 40}))
        return preparation.OwnedInputs(
            donor_root=donor,
            raw_template_user=raw,
            snapshot=snapshot,
            checkpoint_gc=gc,
            provenance=provenance,
        )

    def test_bundle_is_owned_inline_and_has_no_final_checkpoint_dependency(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self._owned_inputs(root)
            output = root / "bundles"
            bundle = preparation.prepare_one(
                "mario-human-vs-fox-cpu1-final-destination", output, inputs, ticks=8)
            self.assertTrue((bundle / "owned-donor.sav").is_file())
            self.assertTrue((bundle / "template-user" / "Pipes").is_dir())
            collector = (bundle / "collector" / "reference_replay_capture.py").read_text()
            compile(collector, "inline_collector.py", "exec")
            command = (bundle / "capture-command.txt").read_text()
            self.assertNotIn("final-checkpoint", command)
            self.assertIn("--until-match-end", command)
            self.assertIn("--draw-audit", command)
            manifest = json.loads((bundle / "preparation.json").read_text())
            self.assertFalse(manifest["capture"]["final_checkpoint_required"])
            self.assertEqual(manifest["source_team_values"], [0, 0])

    def test_existing_bundle_is_rejected_and_bad_raw_calibration_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self._owned_inputs(root)
            output = root / "bundles"
            preparation.prepare_one(
                "mario-human-vs-fox-cpu1-final-destination", output, inputs, ticks=4)
            with self.assertRaises(RuntimeError):
                preparation.prepare_one(
                    "mario-human-vs-fox-cpu1-final-destination", output, inputs, ticks=4)
            bad = root / "bad.ini"
            bad.write_text(
                RAW_GCPAD.replace(
                    "Buttons/A = `Button A`",
                    "Main Stick/Calibration = 100 141 100 141 100 141 100 141\n"
                    "Buttons/A = `Button A`", 1))
            with self.assertRaises(RuntimeError):
                preparation.validate_raw_controller_config(bad, 2)

    def test_raw_template_clones_active_ports_without_calibration(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            (source / "Config").mkdir(parents=True)
            (source / "Config" / "GCPadNew.ini").write_text(RAW_GCPAD)
            destination = root / "owned"
            config = preparation.copy_raw_template(source, destination, 4)
            text = config.read_text()
            self.assertIn("[GCPad3]", text)
            self.assertIn("Pipe/0/pad4", text)
            preparation.validate_raw_controller_config(config, 4)
            self.assertNotIn("Calibration", text)
            # A previously expanded owned template is a valid subsequent input.
            second = preparation.copy_raw_template(destination, root / "second", 4)
            preparation.validate_raw_controller_config(second, 4)
            self.assertEqual(second.read_text().count("[GCPad3]"), 1)
            self.assertEqual(second.read_text().count("[GCPad4]"), 1)


if __name__ == "__main__":
    unittest.main()
