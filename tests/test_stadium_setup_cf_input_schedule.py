"""Focused controls for the exact retained matched-Setup C/F input schedule."""

from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest


CANDIDATE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CANDIDATE / "tools"))
from stadium_setup_cf_input_schedule import (  # noqa: E402
    StadiumSetupCfScheduleError,
    _load_api,
    _source_timeline,
    _boundary_rng_observations,
    extract_stadium_setup_cf_input_schedule,
)


def _paths(root: Path) -> dict[str, Path]:
    v6 = root / "original-stadium-go-prefix-7d406f63-feedback-v6"
    replay = root / "original-stadium-recorded-prefix-replay-v2-run1"
    return {
        "original_observer": v6 / "observer.bin",
        "original_observer_status": v6 / "observer-status.json",
        "original_report": v6 / "report.json",
        "original_mwri": v6 / "inputs.mwri",
        "original_mwri_status": v6 / "input-status.json",
        "replay_observer": replay / "observer.bin",
        "replay_observer_status": replay / "observer-status.json",
        "replay_mwri_status": replay / "input-status.json",
        "replay_report": replay / "report.json",
    }


class StadiumSetupCfInputScheduleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        evidence_root = os.environ.get("MELEE_WEB_STADIUM_SETUP_CF_EVIDENCE_ROOT")
        repo_root = os.environ.get("MELEE_WEB_STADIUM_SETUP_CF_REPO_ROOT")
        if not evidence_root or not repo_root:
            raise unittest.SkipTest("retained source evidence/repository root not configured")
        cls.evidence_root = Path(evidence_root)
        cls.repo_root = Path(repo_root)
        cls.paths = _paths(cls.evidence_root)
        cls.packet = extract_stadium_setup_cf_input_schedule(
            cls.repo_root, **cls.paths)
        api = _load_api(cls.repo_root)
        cls.rows = list(api["iter_records"](cls.paths["original_observer"]))
        cls.consumed_ports = api["_consumed_ports"]
        cls.state_slice = staticmethod(api["_state_slice"])
        cls.replay_rows = list(api["iter_records"](cls.paths["replay_observer"]))

    def test_packet_has_86_exact_four_port_bundles_and_171_reference_actions(self):
        runtime = self.packet["runtime_payload"]
        self.assertEqual(len(runtime["bundles"]), 86)
        self.assertEqual(len(runtime["schedule"]), 171)
        self.assertEqual(sum(item["action"] == "tick" for item in runtime["schedule"]), 86)
        self.assertEqual(sum(item["action"] == "draw" for item in runtime["schedule"]), 85)
        self.assertEqual(runtime["bundles"][0]["ports"], [
            {"port": 0, "status_hex": "0000000000000000000000"},
            {"port": 1, "status_hex": "0000000000000000000000"},
            {"port": 2, "status_hex": "00000000000000000000ff"},
            {"port": 3, "status_hex": "00000000000000000000ff"},
        ])
        self.assertEqual(runtime["schedule"][-3:], [
            {"action": "tick", "ordinal": 84, "bundle": 84},
            {"action": "draw", "ordinal": 84},
            {"action": "tick", "ordinal": 85, "bundle": 85},
        ])
        for bundle in runtime["bundles"]:
            self.assertEqual(set(bundle), {"bundle", "ports"})
            self.assertEqual(len(bundle["ports"]), 4)
            self.assertEqual([port["port"] for port in bundle["ports"]], [0, 1, 2, 3])
            self.assertTrue(all(len(port["status_hex"]) == 22 for port in bundle["ports"]))

    def test_runtime_payload_contains_no_source_state_or_historical_wire_fields(self):
        runtime = self.packet["runtime_payload"]
        self.assertEqual(set(runtime), {"schema", "version", "bundles", "schedule"})
        forbidden = {"pc", "source_tick", "draw_ordinal", "sequence", "payload",
                     "state", "expected", "map2", "display", "go_after", "ground"}

        def visit(value):
            if isinstance(value, dict):
                self.assertTrue(forbidden.isdisjoint(value))
                for nested in value.values():
                    visit(nested)
            elif isinstance(value, list):
                for nested in value:
                    visit(nested)

        visit(runtime)
        host = self.packet["host_only"]
        self.assertEqual(host["source_identities"]["original_v6_recorded_input"]["artifacts"][
            "original_observer"]["sha256"],
            "361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778")
        self.assertEqual(host["source_identities"]["original_v2_recorded_replay_proof"][
            "common_wire_comparison"]["decision"], "PASS_EXACT_STADIUM_COMMON_WIRE")
        self.assertEqual(set(host["expected_original_state"]["summary"]["snapshots"][i]["label"]
                             for i in range(3)), {"GO", "C", "F"})

    def test_source_timeline_refuses_reordered_missing_and_duplicated_rows(self):
        reordered = list(self.rows)
        reordered[2504], reordered[2505] = reordered[2505], reordered[2504]
        with self.assertRaisesRegex(StadiumSetupCfScheduleError, "missing, reordered"):
            _source_timeline(reordered, self.consumed_ports)

        missing_draw = list(self.rows)
        del missing_draw[2506]
        with self.assertRaisesRegex(StadiumSetupCfScheduleError, "missing, reordered"):
            _source_timeline(missing_draw, self.consumed_ports)

        doubled_draw = list(self.rows)
        doubled_draw.insert(2507, copy.deepcopy(doubled_draw[2506]))
        with self.assertRaisesRegex(StadiumSetupCfScheduleError, "missing, reordered|duplicated"):
            _source_timeline(doubled_draw, self.consumed_ports)

    def test_source_timeline_refuses_missing_consume_tick_or_paired_draw(self):
        for sequence in (2504, 2505, 2506, 2507, 2511, 2512, 2930, 2931, 2932, 2933):
            with self.subTest(missing_sequence=sequence):
                missing = list(self.rows)
                del missing[sequence]
                with self.assertRaises(StadiumSetupCfScheduleError):
                    _source_timeline(missing, self.consumed_ports)

    def test_source_timeline_refuses_doubled_tick_and_consume(self):
        for sequence in (2504, 2505, 2506, 2507, 2930, 2931, 2932, 2933):
            with self.subTest(sequence=sequence):
                doubled = list(self.rows)
                doubled.insert(sequence + 1, copy.deepcopy(doubled[sequence]))
                with self.assertRaises(StadiumSetupCfScheduleError):
                    _source_timeline(doubled, self.consumed_ports)

    def test_source_timeline_refuses_changed_outer_or_payload_counter(self):
        for container in ("outer", "payload"):
            with self.subTest(container=container):
                malformed = copy.deepcopy(self.rows)
                row = malformed[2505]
                target = row if container == "outer" else row["payload"]
                target["source_tick"] += 1
                with self.assertRaises(StadiumSetupCfScheduleError):
                    _source_timeline(malformed, self.consumed_ports)

    def test_source_timeline_refuses_partial_typed_pad_status(self):
        malformed = copy.deepcopy(self.rows)
        consume = malformed[2504]
        slot = next(item for item in consume["payload"]["slices"]
                    if item.get("tag") == 3)
        slot["hex"] = slot["hex"][:-2]
        with self.assertRaises(StadiumSetupCfScheduleError):
            _source_timeline(malformed, self.consumed_ports)

    def test_wrong_provenance_hash_refuses_before_packet_creation(self):
        with tempfile.TemporaryDirectory() as temporary:
            bad_report = Path(temporary) / "wrong-report.json"
            bad_report.write_bytes(self.paths["original_report"].read_bytes() + b"\n")
            arguments = dict(self.paths)
            arguments["original_report"] = bad_report
            with self.assertRaisesRegex(StadiumSetupCfScheduleError,
                                        "original v6 report size/hash differs"):
                extract_stadium_setup_cf_input_schedule(self.repo_root, **arguments)


    def test_wrong_replay_report_identity_refuses(self):
        with tempfile.TemporaryDirectory() as temporary:
            bad_report = Path(temporary) / "wrong-replay-report.json"
            bad_report.write_bytes(self.paths["replay_report"].read_bytes() + b"\n")
            arguments = dict(self.paths)
            arguments["replay_report"] = bad_report
            with self.assertRaisesRegex(StadiumSetupCfScheduleError,
                                        "original replay report size/hash differs"):
                extract_stadium_setup_cf_input_schedule(self.repo_root, **arguments)

    def test_boundary_rng_is_direct_observation_at_exact_go_c_f(self):
        observations = self.packet["host_only"]["boundary_rng_observations"]
        self.assertEqual([(item["label"], item["sequence"], item["seed_hex"])
                          for item in observations],
                         [("GO", 2925, "f228f136"), ("C", 2926, "868d77be"),
                          ("F", 2931, "b50f624b")])
        for item in observations:
            self.assertEqual(item["pointer_slot_address"], 0x804D5F94)
            self.assertEqual(int(item["pointer_raw_hex"], 16), item["seed_address"])
            self.assertEqual(item["seed_raw_hex"], item["seed_hex"])
            self.assertTrue(item["pointer_equals_seed_address"])

    def test_boundary_rng_refuses_missing_duplicate_malformed_and_wrong_owner(self):
        for mutation in ("missing", "duplicate", "malformed", "wrong_owner", "wrong_event"):
            with self.subTest(mutation=mutation):
                rows = copy.deepcopy(self.replay_rows)
                row = rows[2931]
                slices = row["payload"]["slices"]
                seed = next(item for item in slices if item.get("tag") == 20)
                if mutation == "missing":
                    slices.remove(seed)
                elif mutation == "duplicate":
                    slices.append(copy.deepcopy(seed))
                elif mutation == "malformed":
                    seed["hex"] = seed["hex"][:-2]
                elif mutation == "wrong_owner":
                    seed["address"] += 4
                else:
                    row["payload"]["boundary"] = "draw_return"
                with self.assertRaises(StadiumSetupCfScheduleError):
                    _boundary_rng_observations(rows, self.state_slice)

    def test_bad_input_status_mode_is_refused_by_existing_strict_validator(self):
        api = _load_api(self.repo_root)
        status = json.loads(self.paths["original_mwri_status"].read_text())
        status["mode"] = "replay"
        with tempfile.TemporaryDirectory() as temporary:
            bad_status = Path(temporary) / "bad-status.json"
            bad_status.write_text(json.dumps(status), encoding="utf-8")
            with self.assertRaises(api["InputStreamError"]):
                api["validate_status"](bad_status, mode="record", events=4706)


if __name__ == "__main__":
    unittest.main()
