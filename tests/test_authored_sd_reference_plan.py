"""Declared original experiment contracts; no Dolphin, browser or oracle run."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "scripts"))
from authored_sd_reference_plan import canonical, make_input_plan, recipe
from retail_input_plan import (AUTHORED_PLAN_VERSION, NEUTRAL_PAD, DISCONNECTED_PAD,
                               load_plan, validate_plan, verify_entry, verify_tick, verify_capture)
from retail_input_bootstrap import (BootstrapCalibrationError, calibration_record,
                                    validate_calibration)
from retail_match_discovery import DiscoveryError, _validate_rows
import capture_retail_replay as runner
from cpu_reference_scenarios import get_scenario, make_input_plan as cpu_input_plan


def setup_bytes():
    # Source StartMeleeData layout, independent of generated plan bytes.
    raw = bytearray(0x138)
    raw[0] = 0x22  # stock match, countdown timer enabled
    raw[2] = 0x88  # stock + pause disabled
    raw[4] = 0x40  # ordinary VS
    raw[0x0e:0x10] = (32).to_bytes(2, "big")
    raw[0x10:0x14] = (60).to_bytes(4, "big")
    raw[0x0b] = 0xff
    raw[0x20:0x28] = b"\xff" * 8
    for offset in (0x2c, 0x30, 0x34):
        raw[offset:offset + 4] = bytes.fromhex("3f800000")
    for slot in range(6):
        base = 0x60 + slot * 0x24
        if slot < 2:
            raw[base:base + 5] = bytes((8, 0, 4, slot, slot + 1))
        else:
            raw[base + 1] = 3
    return raw


class AuthoredSdReferencePlanTests(unittest.TestCase):
    def test_fixed_complete_samples_and_explicit_recipe_provenance(self):
        plan = make_input_plan()
        self.assertEqual(plan["version"], AUTHORED_PLAN_VERSION)
        self.assertEqual(plan["provenance"], "authored-development-workload")
        self.assertNotIn("source_sha256", plan)
        self.assertEqual(plan["authored_recipe_sha256"],
                         hashlib.sha256(canonical(recipe())).hexdigest())
        self.assertEqual(len(plan["frames"]), 4323)
        self.assertEqual(plan["first_frame"] + len(plan["frames"]) - 1, 4199)
        self.assertEqual(plan["controlled_ports"], [1, 2])
        expected = [NEUTRAL_PAD] * 2 + [DISCONNECTED_PAD] * 2
        self.assertTrue(all(frame == expected for frame in plan["frames"]))
        # Each sample and each declaration is owned; mutation cannot extend
        # or silently alter neighboring samples or a later generated plan.
        plan["frames"][0][0] = DISCONNECTED_PAD
        self.assertEqual(plan["frames"][1], expected)
        self.assertEqual(make_input_plan()["frames"][0], expected)

    def test_reject_changed_recipe_even_when_rehashed(self):
        for key, value in (("time_limit_seconds", 120), ("stage", 37),
                           ("timer_enabled", 1), ("is_teams", True)):
            with self.subTest(key=key):
                plan = make_input_plan()
                plan["authored_recipe"]["expected_setup"][key] = value
                plan["authored_recipe_sha256"] = hashlib.sha256(
                    canonical(plan["authored_recipe"])).hexdigest()
                with self.assertRaisesRegex(ValueError, "changed authored"):
                    validate_plan(plan)
        plan = make_input_plan()
        plan["authored_recipe_sha256"] = "a" * 64
        with self.assertRaisesRegex(ValueError, "hash differs"):
            validate_plan(plan)

    def test_reject_truncated_appended_or_missing_port_samples(self):
        for mutation in (lambda p: p["frames"].pop(),
                         lambda p: p["frames"].append(list(p["frames"][-1])),
                         lambda p: p["frames"][123].pop(),
                         lambda p: p["frames"][123].__setitem__(1, DISCONNECTED_PAD),
                         lambda p: p["frames"][123].__setitem__(2, NEUTRAL_PAD)):
            with self.subTest(mutation=mutation):
                plan = make_input_plan()
                mutation(plan)
                with self.assertRaises(ValueError):
                    validate_plan(plan)

    def test_reject_identity_policy_and_state_fields(self):
        for key, value in (("controlled_ports", [1]), ("controlled_ports", [1, True]),
                           ("source_player_types", [0, 1]), ("source_characters", [False, 8]),
                           ("source_stage", 37), ("active_player_count", True),
                           ("provenance", "Slippi"), ("policy", "dolphin-pipe-raw-v1"),
                           ("source_sha256", "a" * 64), ("expected_state", {})):
            with self.subTest(key=key, value=value):
                plan = make_input_plan()
                plan[key] = value
                with self.assertRaises(ValueError):
                    validate_plan(plan)

    def test_explicit_static_load_and_default_capture_rejection(self):
        plan = make_input_plan()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "plan.json"
            raw = canonical(plan)
            path.write_bytes(raw)
            # Legacy standalone collector packages need not import the new
            # builder just to refuse its unsupported contract.
            with mock.patch.dict(sys.modules, {"authored_sd_reference_plan": None}):
                with self.assertRaisesRegex(ValueError, "declaration-only"):
                    load_plan(path)
            loaded, digest = load_plan(path, allow_authored=True)
            self.assertEqual(loaded, plan)
            self.assertEqual(digest, hashlib.sha256(raw).hexdigest())
            # The actual runner rejects before touching any disc, snapshot,
            # user directory, configuration, or process.
            with mock.patch.object(runner, "prepare_run") as prepare, \
                    mock.patch.object(runner, "_regular_file") as read, \
                    mock.patch.object(runner.subprocess, "Popen") as launch:
                with self.assertRaisesRegex(runner.CaptureRunnerError, "declaration-only"):
                    runner.capture_replay(
                        dolphin="missing", disc="missing", dol="missing",
                        template_user="missing", snapshot="missing", checkpoint_gc="missing",
                        provenance="missing", output=Path(directory) / "capture",
                        frames=4323, input_plan=path)
                prepare.assert_not_called()
                read.assert_not_called()
                launch.assert_not_called()
            path.write_text(json.dumps(plan)[:-1] + ',"version":4}')
            with self.assertRaisesRegex(ValueError, "Duplicate"):
                load_plan(path, allow_authored=True)

    def test_all_four_inputs_and_fixed_exhaustion_boundary(self):
        plan = make_input_plan()
        expected = [NEUTRAL_PAD] * 2 + [DISCONNECTED_PAD] * 2
        verify_tick(plan, 0, expected)
        verify_tick(plan, 4322, expected)
        for index in (-1, 4323, True):
            with self.subTest(index=index), self.assertRaisesRegex(ValueError, "cap exhausted"):
                verify_tick(plan, index, expected)
        for inputs in (expected[:2], [NEUTRAL_PAD] * 4,
                       [NEUTRAL_PAD, DISCONNECTED_PAD, DISCONNECTED_PAD, DISCONNECTED_PAD]):
            with self.assertRaisesRegex(ValueError, "Input intent mismatch"):
                verify_tick(plan, 123, inputs)

    def test_entry_binds_rules_human_slots_and_inactive_records(self):
        plan = make_input_plan()
        raw = setup_bytes()
        verify_entry(plan, raw.hex())
        for offset, value in ((0x11, 1), (0x0b, 0), (8, 1), (0x62, 3),
                              (0x85, 1), (0x87, 0), (0xa9, 2), (0xf1, 0),
                              (0x30, 0), (0x34, 0), (0x64, 4)):
            with self.subTest(offset=offset):
                changed = bytearray(raw)
                changed[offset] = value
                with self.assertRaises(ValueError):
                    verify_entry(plan, changed.hex())

    def test_direct_legacy_receivers_reject_authored_contract(self):
        plan = make_input_plan()
        with self.assertRaisesRegex(ValueError, "declaration-only"):
            verify_capture(plan, None)
        with self.assertRaisesRegex(BootstrapCalibrationError, "no supported bootstrap"):
            validate_calibration({}, plan=plan, plan_sha256="a" * 64, runtime={})
        with self.assertRaisesRegex(BootstrapCalibrationError, "no supported bootstrap"):
            calibration_record(plan=plan, plan_sha256="a" * 64, provenance={},
                               collector_sha256="b" * 64, construction_pad_reads=1,
                               last_construction_pad_read={})
        with self.assertRaisesRegex(DiscoveryError, "no supported match discovery"):
            _validate_rows([], "test", plan=plan, sha256=None, raw=None)

    def test_cpu_v3_still_rejects_human_p2_and_port2_control(self):
        plan = cpu_input_plan(get_scenario("mario-human-vs-fox-cpu1-final-destination"), ticks=1)
        self.assertIs(validate_plan(plan), plan)
        human = deepcopy(plan)
        human["source_player_types"][1] = 0
        with self.assertRaisesRegex(ValueError, "human P1 and CPU"):
            validate_plan(human)
        control = deepcopy(plan)
        control["controlled_ports"] = [1, 2]
        with self.assertRaisesRegex(ValueError, "human P1 only"):
            validate_plan(control)


if __name__ == "__main__":
    unittest.main()
