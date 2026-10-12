import copy
import hashlib
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from run_network_simulation import SCENARIOS, scenario_route_flags, validate_result


def neutral_input_diagnostic(samples):
    """Synthetic full shape matching browser_input.cpp; actual producer is tested separately."""
    pad = {"source": "keyboard", "err": 0, "buttons": 0, "stick": [0, 0],
           "cstick": [0, 0], "triggers": [0, 0], "analog": [0, 0],
           "clamped": {"stick": [0, 0], "cstick": [0, 0], "triggers": [0, 0]}}
    return {"ready": 1, "focused": 1, "visible": 1, "active": 1,
            "keyboard_requested": 1, "keyboard_active": 1,
            "keyboard_requested_mask": 3, "keyboard_active_mask": 3, "physical_mask": 0,
            "keyboard_layout": 0, "samples": samples, "error": "",
            "pads": [copy.deepcopy(pad) for _ in range(4)], "last_non_neutral": None}


class NetworkSimulationSuitePolicyTests(unittest.TestCase):
    def setUp(self):
        self.owner = {"schema": "melee-web-capture-process-owner-v3", "result": "pass",
                      "capture_exit_code": 0, "cleanup_passed": True,
                      "process_observation_passed": True}
        self.browser = {"schema": "melee-web-a3-local-room-keyboard-v1", "decision": "PASS",
                        "first_error": None, "relay_mode": "simulated-network",
                        "network_simulation": {"proxy_cleanup": {"verified": True, "final": True}},
                        "gameplay_action_probe": False, "gameplay_combat_probe": False,
                        "full_route": {"result": "pass", "gameplay_action_probe": False,
                                       "gameplay_combat_probe": False}}

    def test_independent_owner_and_browser_receipts_are_both_required(self):
        validate_result(0, self.owner, self.browser, True)
        for key, value in (("result", "fail"), ("cleanup_passed", False),
                           ("process_observation_passed", False), ("capture_exit_code", 1),
                           ("schema", "unknown")):
            with self.subTest(owner_field=key):
                broken = dict(self.owner, **{key: value})
                with self.assertRaises(ValueError):
                    validate_result(0, broken, self.browser, True)
        for key, value in (("decision", "FAIL"), ("first_error", "teardown failed"),
                           ("relay_mode", "external"), ("schema", "unknown")):
            with self.subTest(browser_field=key):
                with self.assertRaises(ValueError):
                    validate_result(0, self.owner, dict(self.browser, **{key: value}), True)
        with self.assertRaises(ValueError):
            validate_result(1, self.owner, self.browser, True)

    def test_css_and_unverified_cleanup_cannot_satisfy_a_complete_route(self):
        css = dict(self.browser, full_route=None)
        validate_result(0, self.owner, css, False)
        with self.assertRaisesRegex(ValueError, "full route"):
            validate_result(0, self.owner, css, True)
        for key in ("verified", "final"):
            broken = copy.deepcopy(self.browser)
            broken["network_simulation"]["proxy_cleanup"].pop(key)
            with self.assertRaisesRegex(ValueError, "proxy"):
                validate_result(0, self.owner, broken, True)

    def test_controlled_stall_has_a_separate_policy_and_never_claims_clean_route_pass(self):
        browser = copy.deepcopy(self.browser)
        browser["decision"] = "PASS_POST_CSS_STALL_PROBE"
        browser["network_simulation"]["post_css_stall_probe"] = {
            "outcome": "EXERCISED_AND_RESUMED",
            "policy_result": {"policy": "controlled-post-css-stall-exercised",
                              "result_is_not_clean_route_pass": True},
        }
        validate_result(0, self.owner, browser, False, controlled_stall=True)
        for corrupt in (
                lambda row: row["network_simulation"].pop("post_css_stall_probe"),
                lambda row: row["network_simulation"]["post_css_stall_probe"].update(outcome="FAIL"),
                lambda row: row["network_simulation"]["post_css_stall_probe"]["policy_result"].update(
                    result_is_not_clean_route_pass=False),
                lambda row: row.update(decision="PASS")):
            damaged = copy.deepcopy(browser)
            corrupt(damaged)
            with self.assertRaises(ValueError):
                validate_result(0, self.owner, damaged, False, controlled_stall=True)
    def repeat_route(self):
        identity = {"room_id": "same-room", "relay_url": "ws://127.0.0.1:1",
                    "invite_sha256": "a" * 64, "network_profile_sha256": "c" * 64,
                    "owner_process_id": 123, "owner_process_group_id": 123,
                    "worker_hashes": {"worker": "b" * 64},
                    "persistent_pages": ["alpha", "beta"]}
        def checkpoint(label, cursor, count):
            return {"label": label, "result": "pass", "room_identity": copy.deepcopy(identity),
                    "peers": {role: {"source_cursor": cursor,
                        "checksum_counts": {"retained": count, "local": count,
                            "remote": count, "compared": count},
                        "peer_identity": {"role": role, "local_port": 0 if role == "alpha" else 1,
                            "owner_seed": 77},
                        "input_diagnostics": neutral_input_diagnostic(cursor)}
                        for role in ("alpha", "beta")}}
        first_boundary, second_boundary = 20, 30
        checksum = self.repeat_checksum_comparison()
        digest = hashlib.sha256(b"".join(bytes.fromhex(row["alpha_raw_hex"])
                                         for row in checksum["records"][:first_boundary])).hexdigest()
        second_digest = hashlib.sha256(b"".join(bytes.fromhex(row["alpha_raw_hex"])
                                                for row in checksum["records"][:second_boundary])).hexdigest()
        first_return = checkpoint("match-1-returned-css", 100, 101)
        first_return["common_raw_prefix"] = {"result": "pass",
            "source_tick_range": [0, first_boundary - 1], "record_count": first_boundary,
            "bytes_per_record": 64, "alpha_equals_beta": True,
            "sha256_concatenated_records": digest}
        second_entry = checkpoint("match-2-entry", 100, 101)
        second_return = checkpoint("match-2-returned-css", 200, 201)
        return {"result": "pass", "gameplay_action_probe": False,
            "gameplay_combat_probe": False,
            "repeat_mode": "same-room-two-match", "required_checksum_boundary": second_boundary,
            "repeated_matches": [
                {"match_number": 1, "result": "pass", "route_elapsed_ms": 100000, "report_artifact":
                    {"file": "match-1/report.json", "bytes": 100, "sha256": "a" * 64},
                 "route": {"result": "pass", "required_checksum_boundary": first_boundary,
                    "screenshots": {"css": {"alpha": {"file": "match-1/css-alpha.png", "bytes": 90, "sha256": "e" * 64},
                                              "beta": {"file": "match-1/css-beta.png", "bytes": 90, "sha256": "e" * 64}}}},
                 "first_return": first_return},
                {"match_number": 2, "result": "pass", "route_elapsed_ms": 100000, "report_artifact":
                    {"file": "match-2/report.json", "bytes": 100, "sha256": "b" * 64},
                 "route": {"result": "pass", "required_checksum_boundary": second_boundary,
                    "returned_css": {"source_cursor_by_peer": {"alpha": 180, "beta": 180}},
                    "screenshots": {"css": {"alpha": {"file": "match-2/css-alpha.png", "bytes": 90, "sha256": "e" * 64},
                                              "beta": {"file": "match-2/css-beta.png", "bytes": 90, "sha256": "e" * 64}}}},
                 "entry": second_entry, "second_return": second_return}],
            "admission": {"first_route_duration_ms": 100000,
                "remaining_before_second_ms": 130000, "required_remaining_ms": 130000,
                "second_route_subdeadline_ms": 105000, "second_route_elapsed_ms": 100000,
                "final_reserve_ms_at_second_return": 20000},
            "final_prefix": {"result": "pass",
                "raw_records_present_in_final_checksum_comparison": True,
                "compared_source_ticks": second_boundary,
                "first_boundary": first_boundary, "first_prefix_sha256": digest,
                "second_boundary": second_boundary, "second_prefix_sha256": second_digest}}

    def repeat_checksum_comparison(self):
        rows = []
        for tick in range(30):
            raw = bytes([tick]) * 64
            rows.append({"source_tick": tick, "bytes": 64,
                         "alpha_raw_hex": raw.hex(), "beta_raw_hex": raw.hex()})
        return {"compared_source_ticks": 30, "exact_record_count_each": 30,
                "source_tick_range": [0, 29], "records": rows}

    def test_repeat_input_rejects_wrong_source_non_neutral_or_history_only_shape(self):
        from run_network_simulation import validate_repeat_checkpoint_input, validate_repeat_route
        mutations = [
            lambda row: row.update(samples=True), lambda row: row.pop("samples"),
            lambda row: row.update(ready=0), lambda row: row.update(keyboard_active_mask=1),
            lambda row: row.update(physical_mask=2), lambda row: row.update(error="input failed"),
            lambda row: row["pads"][1].update(source="none"),
            lambda row: row["pads"][1].update(err=-1),
            lambda row: row["pads"][1].update(buttons=1),
            lambda row: row["pads"][1].update(stick=[0, 1]),
            lambda row: row["pads"][1]["clamped"].update(cstick=[1, 0]),
        ]
        for index, corrupt in enumerate(mutations):
            with self.subTest(index=index):
                row = neutral_input_diagnostic(100)
                corrupt(row)
                with self.assertRaises(ValueError): validate_repeat_checkpoint_input(row, 1)
        with self.assertRaises(ValueError):
            validate_repeat_checkpoint_input({"port": 1, "sample": 100}, 1)
        route = self.repeat_route()
        route["repeated_matches"][1]["entry"]["peers"]["beta"]["input_diagnostics"]["samples"] = 99
        with self.assertRaisesRegex(ValueError, "input sample counter regressed"):
            validate_repeat_route(route, self.repeat_checksum_comparison())

    def test_repeat_requires_two_routes_same_room_raw_prefix_and_budget(self):
        from run_network_simulation import validate_repeat_artifacts, validate_repeat_route
        route = self.repeat_route()
        browser = copy.deepcopy(self.browser)
        browser["full_route"] = route
        browser["checksum_comparison"] = self.repeat_checksum_comparison()
        validate_result(0, self.owner, browser, True, False, True)
        validate_repeat_route(route, browser["checksum_comparison"])
        broken_cases = []
        missing_second = copy.deepcopy(route)
        missing_second["repeated_matches"].pop()
        broken_cases.append(("two route reports", missing_second))
        failed_second = copy.deepcopy(route)
        failed_second["repeated_matches"][1]["result"] = "not_attempted_budget"
        broken_cases.append(("match 2", failed_second))
        prefix_not_compared = copy.deepcopy(route)
        prefix_not_compared["repeated_matches"][0]["first_return"]["common_raw_prefix"]["alpha_equals_beta"] = False
        broken_cases.append(("raw checksum prefix", prefix_not_compared))
        wrong_room = copy.deepcopy(route)
        wrong_room["repeated_matches"][1]["entry"]["room_identity"]["room_id"] = "new-room"
        broken_cases.append(("identity changed", wrong_room))
        regressed_cursor = copy.deepcopy(route)
        regressed_cursor["repeated_matches"][1]["second_return"]["peers"]["alpha"]["source_cursor"] = 99
        broken_cases.append(("source cursors", regressed_cursor))
        returned_before_route = copy.deepcopy(route)
        returned_before_route["repeated_matches"][1]["second_return"]["peers"]["alpha"]["source_cursor"] = 179
        broken_cases.append(("source cursors", returned_before_route))
        regressed_checksums = copy.deepcopy(route)
        regressed_checksums["repeated_matches"][1]["second_return"]["peers"]["alpha"][
            "checksum_counts"]["compared"] = 100
        broken_cases.append(("compared checksum counts", regressed_checksums))
        insufficient = copy.deepcopy(route)
        insufficient["admission"]["remaining_before_second_ms"] = 129999
        broken_cases.append(("admission", insufficient))
        negative_subdeadline = copy.deepcopy(route)
        negative_subdeadline["admission"]["second_route_subdeadline_ms"] = -1
        broken_cases.append(("admission", negative_subdeadline))
        missed_deadline = copy.deepcopy(route)
        missed_deadline["admission"]["second_route_elapsed_ms"] = 105001
        broken_cases.append(("admission", missed_deadline))
        missing_reserve = copy.deepcopy(route)
        missing_reserve["admission"]["final_reserve_ms_at_second_return"] = 19999
        broken_cases.append(("admission", missing_reserve))
        changed_prefix = copy.deepcopy(route)
        changed_prefix["final_prefix"]["first_prefix_sha256"] = "e" * 64
        broken_cases.append(("bind both route boundaries", changed_prefix))
        for message, bad_route in broken_cases:
            with self.subTest(rejected=message), self.assertRaisesRegex(ValueError, message):
                validate_repeat_route(bad_route, browser["checksum_comparison"])
        broken_records = copy.deepcopy(browser["checksum_comparison"])
        broken_records["records"][7]["beta_raw_hex"] = "f" * 128
        with self.assertRaisesRegex(ValueError, "record 7"):
            validate_repeat_route(route, broken_records)
        missing_records = copy.deepcopy(browser["checksum_comparison"])
        missing_records["records"] = missing_records["records"][:29]
        with self.assertRaisesRegex(ValueError, "does not cover match two"):
            validate_repeat_route(route, missing_records)
        with self.assertRaisesRegex(ValueError, "unexpectedly contains repeated"):
            validate_result(0, self.owner, browser, True, False, False)
        with self.assertRaisesRegex(ValueError, "did not request a full route"):
            validate_result(0, self.owner, browser, False, False, True)

    def test_repeat_reports_and_screenshots_are_unique_and_hash_bound(self):
        from tempfile import TemporaryDirectory
        from pathlib import Path
        from run_network_simulation import validate_repeat_artifacts
        route = self.repeat_route()
        with TemporaryDirectory() as temp:
            root = Path(temp)
            for entry in route["repeated_matches"]:
                paths = [entry["report_artifact"]["file"]]
                def visit(value):
                    if isinstance(value, dict):
                        if {"file", "bytes", "sha256"}.issubset(value):
                            paths.append(value["file"])
                        for child in value.values():
                            visit(child)
                    elif isinstance(value, list):
                        for child in value:
                            visit(child)
                visit(entry["route"]["screenshots"])
                for relative in paths:
                    file_path = root / relative
                    file_path.parent.mkdir(parents=True, exist_ok=True)
                    payload = ("contents:" + relative).encode()
                    file_path.write_bytes(payload)
                    receipt = (entry["report_artifact"] if relative == entry["report_artifact"]["file"]
                               else next(item for item in [entry["route"]["screenshots"]["css"]["alpha"],
                                                            entry["route"]["screenshots"]["css"]["beta"]]
                                         if item["file"] == relative))
                    receipt["bytes"] = len(payload)
                    receipt["sha256"] = hashlib.sha256(payload).hexdigest()
            validate_repeat_artifacts(root, route)
            route["repeated_matches"][1]["route"]["screenshots"]["css"]["alpha"]["sha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "hash differs"):
                validate_repeat_artifacts(root, route)

    def test_combat_probe_has_an_isolated_moderate_full_route_scenario(self):
        self.assertEqual(SCENARIOS["full-moderate-combat-probe"],
                         ("moderate", True, False, False, False, False, True))
        self.assertEqual(scenario_route_flags("full-moderate-combat-probe"),
                         ["--full-route", "--gameplay-combat-probe"])
        self.assertEqual(scenario_route_flags("full-moderate"), ["--full-route"])
        combat = copy.deepcopy(self.browser)
        combat["gameplay_combat_probe"] = True
        combat["full_route"]["gameplay_combat_probe"] = True
        combat["full_route"]["combat_probe"] = {"result": "pass"}
        validate_result(0, self.owner, combat, True, gameplay_combat_probe=True)
        with self.assertRaisesRegex(ValueError, "combat mode"):
            validate_result(0, self.owner, combat, True)
        with self.assertRaisesRegex(ValueError, "incomplete"):
            incomplete = copy.deepcopy(combat)
            incomplete["full_route"]["combat_probe"]["result"] = "running"
            validate_result(0, self.owner, incomplete, True, gameplay_combat_probe=True)
        with self.assertRaisesRegex(ValueError, "unexpectedly contains a gameplay action probe"):
            mixed = copy.deepcopy(combat)
            mixed["full_route"]["action_prefix"] = {"result": "pass"}
            validate_result(0, self.owner, mixed, True, gameplay_combat_probe=True)
        with self.assertRaisesRegex(ValueError, "separate scenarios"):
            validate_result(0, self.owner, combat, True, True,
                            gameplay_combat_probe=True)
        with self.assertRaisesRegex(ValueError, "combat mode"):
            validate_result(0, self.owner,
                            dict(combat, gameplay_combat_probe=False), True,
                            gameplay_combat_probe=True)
        unexpected = copy.deepcopy(self.browser)
        unexpected["full_route"]["combat_probe"] = {"result": "pass"}
        with self.assertRaisesRegex(ValueError, "unexpectedly contains a gameplay combat"):
            validate_result(0, self.owner, unexpected, True)

    def test_combat_exchange_has_an_isolated_moderate_scenario_and_result_gate(self):
        self.assertEqual(SCENARIOS["full-moderate-combat-exchange"],
                         ("moderate", True, False, False, False, False, True, True))
        self.assertEqual(scenario_route_flags("full-moderate-combat-exchange"),
                         ["--full-route", "--gameplay-combat-probe",
                          "--gameplay-combat-exchange"])
        exchange = copy.deepcopy(self.browser)
        exchange["gameplay_combat_probe"] = True
        exchange["gameplay_combat_exchange"] = True
        exchange["full_route"]["gameplay_combat_probe"] = True
        exchange["full_route"]["gameplay_combat_exchange"] = True
        exchange["full_route"]["combat_probe"] = {
            "result": "pass", "combat_exchange": {"result": "pass"}}
        validate_result(0, self.owner, exchange, True, gameplay_combat_probe=True,
                        gameplay_combat_exchange=True)
        with self.assertRaisesRegex(ValueError, "combat exchange is incomplete"):
            incomplete = copy.deepcopy(exchange)
            incomplete["full_route"]["combat_probe"]["combat_exchange"]["result"] = "running"
            validate_result(0, self.owner, incomplete, True, gameplay_combat_probe=True,
                            gameplay_combat_exchange=True)
        with self.assertRaisesRegex(ValueError, "explicitly true"):
            wrong_mode = copy.deepcopy(exchange)
            wrong_mode["gameplay_combat_exchange"] = False
            validate_result(0, self.owner, wrong_mode, True, gameplay_combat_probe=True,
                            gameplay_combat_exchange=True)
        with self.assertRaisesRegex(ValueError, "browser.*explicitly true"):
            missing_browser_mode = copy.deepcopy(exchange)
            del missing_browser_mode["gameplay_combat_exchange"]
            validate_result(0, self.owner, missing_browser_mode, True,
                            gameplay_combat_probe=True, gameplay_combat_exchange=True)
        with self.assertRaisesRegex(ValueError, "full route.*explicitly true"):
            missing_route_mode = copy.deepcopy(exchange)
            del missing_route_mode["full_route"]["gameplay_combat_exchange"]
            validate_result(0, self.owner, missing_route_mode, True,
                            gameplay_combat_probe=True, gameplay_combat_exchange=True)
        with self.assertRaisesRegex(ValueError, "requires the existing combat probe"):
            validate_result(0, self.owner, self.browser, True,
                            gameplay_combat_exchange=True)

    def test_action_probe_cannot_be_relabelled_as_baseline(self):
        action = copy.deepcopy(self.browser)
        action["gameplay_action_probe"] = True
        action["full_route"]["gameplay_action_probe"] = True
        action["full_route"]["action_prefix"] = {"result": "pass"}
        validate_result(0, self.owner, action, True, True)
        with self.assertRaisesRegex(ValueError, "mode"):
            validate_result(0, self.owner, action, True)
        with self.assertRaisesRegex(ValueError, "mode"):
            validate_result(0, self.owner, self.browser, True, True)
        action["full_route"]["action_prefix"]["result"] = "fail"
        with self.assertRaisesRegex(ValueError, "incomplete"):
            validate_result(0, self.owner, action, True, True)
        partial = copy.deepcopy(self.browser)
        partial["full_route"]["action_prefix"] = {"result": "fail"}
        with self.assertRaisesRegex(ValueError, "unexpectedly"):
            validate_result(0, self.owner, partial, True)
        with self.assertRaisesRegex(ValueError, "mode"):
            validate_result(0, self.owner, dict(self.browser, gameplay_action_probe=None), True)



if __name__ == "__main__":
    unittest.main()
