import copy
import hashlib
import json
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from run_network_simulation import SCENARIOS, scenario_route_flags, validate_result, _prepared_platform_exit_receipt_ok
from check_gameplay import node_runtime


def incident_recording():
    return {"requested": True, "profiles": {role: {
        "readiness": {"requested": True, "export_available": True, "ring_status": {
            "selection": {"staging_diagnostics_requested": True,
                          "staging_diagnostics_enabled": True,
                          "staging_diagnostics_reason": "explicit_loopback_opt_in"}}},
        "export": {"current": {"schema": "melee-web-runtime-diagnostics",
                               "session_id": role, "incidents": []}, "retained": {"records": []}},
        "before_leave": {"native_running": 1, "native_message": "Running"},
    } for role in ("alpha", "beta")}}


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
                        "runtime_incident_recording": incident_recording(),
                        "first_error": None, "relay_mode": "simulated-network",
                        "network_simulation": {"proxy_cleanup": {"verified": True, "final": True}},
                        "gameplay_action_probe": False, "gameplay_combat_probe": False,
                        "full_route": {"result": "pass", "gameplay_action_probe": False,
                                       "gameplay_combat_probe": False}}

    def test_incident_recording_and_unmodified_h1_evidence_are_required(self):
        for corrupt in (
            lambda row: row.pop("runtime_incident_recording"),
            lambda row: row["runtime_incident_recording"]["profiles"].pop("beta"),
            lambda row: row["runtime_incident_recording"]["profiles"]["alpha"].update(export=None),
            lambda row: row["runtime_incident_recording"]["profiles"]["alpha"]["export"]["current"]
                ["incidents"].append({"reason": "simulation_debt", "staging": {"observed": True}}),
        ):
            broken = copy.deepcopy(self.browser)
            corrupt(broken)
            with self.assertRaisesRegex(ValueError, "incident|H1"):
                validate_result(0, self.owner, broken, False)

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



    def test_ko_mode_requires_explicit_exchange_and_validated_three_stock_handoff(self):
        self.assertEqual(scenario_route_flags("full-moderate-combat-ko"),
                         ["--full-route", "--gameplay-combat-probe", "--gameplay-combat-exchange", "--gameplay-combat-ko"])
        browser = copy.deepcopy(self.browser)
        browser.update(gameplay_combat_probe=True, gameplay_combat_exchange=True, gameplay_combat_ko=True)
        route = browser["full_route"]
        route.update(gameplay_combat_probe=True, gameplay_combat_exchange=True, gameplay_combat_ko=True)
        route["combat_probe"] = {"result":"pass", "combat_exchange":{"result":"pass"},
                                 "combat_ko":{"result":"pass","handoff":{"starting_stocks":3}}}
        kwargs = dict(gameplay_combat_probe=True,gameplay_combat_exchange=True,gameplay_combat_ko=True)
        validate_result(0,self.owner,browser,True,**kwargs)
        for corrupt in (lambda b:b.pop("gameplay_combat_ko"),
                        lambda b:b["full_route"].pop("gameplay_combat_ko"),
                        lambda b:b["full_route"]["combat_probe"]["combat_ko"].update(result="fail"),
                        lambda b:b["full_route"]["combat_probe"]["combat_ko"]["handoff"].update(starting_stocks=4)):
            bad=copy.deepcopy(browser);corrupt(bad)
            with self.assertRaises(ValueError): validate_result(0,self.owner,bad,True,**kwargs)
        with self.assertRaises(ValueError):
            validate_result(0,self.owner,browser,True,gameplay_combat_probe=True,gameplay_combat_ko=True)

    def test_charged_ko_is_a_separate_explicit_mode_with_source_animation_evidence(self):
        self.assertEqual(scenario_route_flags("full-moderate-combat-ko-charged"),
                         ["--full-route", "--gameplay-combat-probe", "--gameplay-combat-exchange",
                          "--gameplay-combat-ko-charged"])
        browser = copy.deepcopy(self.browser)
        browser.update(gameplay_combat_probe=True, gameplay_combat_exchange=True,
                       gameplay_combat_ko=False, gameplay_combat_ko_charged=True)
        route = browser["full_route"]
        route.update(gameplay_combat_probe=True, gameplay_combat_exchange=True,
                     gameplay_combat_ko=False, gameplay_combat_ko_charged=True)
        route["combat_probe"] = {"result":"pass", "combat_exchange":{"result":"pass"},
            "combat_ko":{"result":"pass", "handoff":{"starting_stocks":3},
                "charge_animation_evidence":{"kind":
                    "observed-s4s60-charge-animation-plateau-and-release-advance-v1"}}}
        kwargs = dict(gameplay_combat_probe=True, gameplay_combat_exchange=True,
                      gameplay_combat_ko_charged=True)
        validate_result(0, self.owner, browser, True, **kwargs)
        for corrupt in (
                lambda row: row.pop("gameplay_combat_ko_charged"),
                lambda row: row["full_route"].update(gameplay_combat_ko_charged=False),
                lambda row: row["full_route"]["combat_probe"]["combat_ko"].pop(
                    "charge_animation_evidence"),
                lambda row: row.update(gameplay_combat_ko=True),
                lambda row: row["full_route"].update(gameplay_combat_ko=True)):
            damaged = copy.deepcopy(browser)
            corrupt(damaged)
            with self.assertRaises(ValueError):
                validate_result(0, self.owner, damaged, True, **kwargs)
        with self.assertRaisesRegex(ValueError, "separate scenarios"):
            validate_result(0, self.owner, browser, True,
                gameplay_combat_probe=True, gameplay_combat_exchange=True,
                gameplay_combat_ko=True, gameplay_combat_ko_charged=True)

    def test_prepared_ko_requires_bounded_actual_damage_baseline_and_uncharged_handoff(self):
        self.assertEqual(SCENARIOS["full-moderate-combat-ko-prepared"],
                         ("moderate", True, False, False, False, False, True, True,
                          False, False, True))
        self.assertEqual(scenario_route_flags("full-moderate-combat-ko-prepared"),
                         ["--full-route", "--gameplay-combat-probe",
                          "--gameplay-combat-exchange", "--gameplay-combat-ko-prepared"])
        browser = copy.deepcopy(self.browser)
        browser.update(gameplay_combat_probe=True, gameplay_combat_exchange=True,
                       gameplay_combat_ko=False, gameplay_combat_ko_charged=False,
                       gameplay_combat_ko_prepared=True)
        route = browser["full_route"]
        route.update(gameplay_combat_probe=True, gameplay_combat_exchange=True,
                     gameplay_combat_ko=False, gameplay_combat_ko_charged=False,
                     gameplay_combat_ko_prepared=True)
        match = {"frame": 200, "players": [
            {"source_port": 0, "source_player_index": 4, "motion": 14, "groundAir": 0, "stocks": 4,
             "damage_percent": 18, "x": -1, "facing_direction": 1},
            {"source_port": 1, "source_player_index": 1, "motion": 14, "groundAir": 0, "stocks": 4,
             "damage_percent": 0, "x": 1, "facing_direction": -1}]}
        baseline = {"source_tick": 200, "stocks_by_port": [4, 4],
                    "source_player_index_by_port": [4, 1],
                    "actual_matches": [copy.deepcopy(match), copy.deepcopy(match)]}
        route["combat_probe"] = {"result": "pass", "combat_exchange": {"result": "pass"},
            "combat_ko": {"result": "pass", "input_mode": "cstick-left",
                "baseline": copy.deepcopy(baseline), "source_cursor_limit": 680,
                "handoff": {"starting_stocks": 3}, "preparation": {
                    "result": "pass", "literal_input_window": {"status": "validated"},
                    "jab_count": 4, "max_jabs": 8, "final_p1_damage_percent": 18,
                    "actual_damage_goal_percent": 18, "max_approach_taps": 16,
                    "approach": {"steps": []}, "source_cursor_start": 100,
                    "source_cursor_limit": 580, "source_cursor_end": 200,
                    "source_cursor_span": 100, "max_source_cursors": 480,
                    "baseline": {"source_tick": 100},
                    "prepared_ko_baseline": baseline}}}
        kwargs = dict(gameplay_combat_probe=True, gameplay_combat_exchange=True,
                      gameplay_combat_ko_prepared=True)
        validate_result(0, self.owner, browser, True, **kwargs)
        mutations = (
            lambda prep: prep.update(final_p1_damage_percent=17.99),
            lambda prep: prep.update(source_cursor_span=481),
            lambda prep: prep.update(source_cursor_span=99),
            lambda prep: prep.update(source_cursor_limit=581),
            lambda prep: prep["baseline"].update(source_tick=101),
            lambda prep: prep.pop("baseline"),
            lambda prep: prep.update(jab_count=9),
            lambda prep: prep["prepared_ko_baseline"].update(stocks_by_port=[4, 3]),
            lambda prep: prep["prepared_ko_baseline"].update(source_tick=201),
            lambda prep: prep["prepared_ko_baseline"]["actual_matches"][0]["players"][0].update(
                groundAir=1),
        )
        for mutate in mutations:
            damaged = copy.deepcopy(browser)
            mutate(damaged["full_route"]["combat_probe"]["combat_ko"]["preparation"])
            with self.assertRaisesRegex(ValueError, "prepared damage proof"):
                validate_result(0, self.owner, damaged, True, **kwargs)
        ko_mutations = (
            lambda ko: ko.pop("baseline"),
            lambda ko: ko["baseline"].update(source_tick=201),
            lambda ko: ko.update(source_cursor_limit=679),
            lambda ko: ko["preparation"]["prepared_ko_baseline"]["actual_matches"][0]["players"][0].update(
                damage_percent=17),
            lambda ko: ko["preparation"]["prepared_ko_baseline"]["actual_matches"][1]["players"][0].update(
                damage_percent=17),
        )
        for mutate in ko_mutations:
            damaged = copy.deepcopy(browser)
            mutate(damaged["full_route"]["combat_probe"]["combat_ko"])
            with self.assertRaisesRegex(ValueError, "prepared damage proof"):
                validate_result(0, self.owner, damaged, True, **kwargs)

    def test_prepared_platform_exit_is_a_distinct_bounded_policy_and_cannot_relabel_legacy(self):
        name = "full-moderate-combat-ko-prepared-platform-exit"
        self.assertEqual(SCENARIOS[name],
                         ("moderate", True, False, False, False, False, True, True,
                          False, False, True, True))
        self.assertEqual(scenario_route_flags(name), ["--full-route", "--gameplay-combat-probe",
            "--gameplay-combat-exchange", "--gameplay-combat-ko-prepared",
            "--gameplay-combat-ko-prepared-platform-exit"])
        browser = copy.deepcopy(self.browser)
        browser.update(gameplay_combat_probe=True, gameplay_combat_exchange=True,
                       gameplay_combat_ko=False, gameplay_combat_ko_charged=False,
                       gameplay_combat_ko_prepared=True,
                       gameplay_combat_ko_prepared_platform_exit=True)
        route = browser["full_route"]
        route.update(gameplay_combat_probe=True, gameplay_combat_exchange=True,
                     gameplay_combat_ko=False, gameplay_combat_ko_charged=False,
                     gameplay_combat_ko_prepared=True,
                     gameplay_combat_ko_prepared_platform_exit=True)
        match = {"frame": 200, "players": [
            {"source_port": 0, "source_player_index": 4, "motion": 14, "groundAir": 0,
             "stocks": 4, "damage_percent": 18, "x": -1, "facing_direction": 1},
            {"source_port": 1, "source_player_index": 1, "motion": 14, "groundAir": 0,
             "stocks": 4, "damage_percent": 0, "x": 1, "facing_direction": -1}]}
        baseline = {"source_tick": 200, "stocks_by_port": [4, 4],
                    "source_player_index_by_port": [4, 1],
                    "actual_matches": [copy.deepcopy(match), copy.deepcopy(match)]}
        p1_rows, p2_rows = [], []
        for tick in range(196, 297):
            p1 = [0] * 11
            if tick in (237, 238):
                p1[2] = 80
            p2 = [0] * 11
            if tick in (197, 198):
                p2[4] = 176
            p1_rows.append({"input_tick": tick, "source_cursor": tick,
                            "local_port": 0, "bytes": p1})
            p2_rows.append({"input_tick": tick, "source_cursor": tick,
                            "local_port": 1, "bytes": p2})
        input_window = {"kind": "literal-facing-horizontal-platform-exit-v1", "input_delay": 3,
            "start_input_tick": 196, "gate_source_tick": 240, "gate_input_tick": 236,
            "p1_axis": "left_x", "p1_axis_byte": 80, "facing_direction": 1,
            "direction_key": "d", "p1_axis_rise_input_tick": 237,
            "p1_axis_last_held_input_tick": 238,
            "p1_neutral_release_input_tick": 239, "p1_axis_held_row_count": 2,
            "p1_neutral_prefix_row_count": 41, "through_grounded_wait_input_tick": 296,
            "p2_cstick_left_rise_input_tick": 197,
            "p2_cstick_left_neutral_release_input_tick": 199,
            "p2_cstick_left_held_row_count": 2, "p1_rows": p1_rows, "p2_rows": p2_rows}
        position_policy = {"min_x": -64, "max_x": 64, "min_y": 32, "max_y": 64,
            "policy": "conservative scenario admission only; not authored Final Destination geometry or landing proof"}
        loss = {"kind": "validated-source-credited-combat-ko-loss-v1", "source_tick": 230}
        gate = {"kind": "validated-source-credited-rebirth-wait-platform-gate-v1",
            "source_tick": 240, "loss_source_tick": 230, "facing_direction": 1,
            "direction_key": "d", "position_by_peer": [{"x": 16, "y": 45},
                                                               {"x": 16, "y": 45}],
            "position_admission_policy": position_policy}
        fall = {"kind": "validated-post-platform-exit-source-fall-v1",
            "input_delay": 3,
            "source_tick": 250, "action_input_tick": 237,
            "action_consumed_source_tick": 241}
        platform_exit = {"result": "pass", "gate": gate,
            "pre_keydown_readbacks": {"accepted": True},
            "pre_keydown_prefix": {"kind": "validated-pre-keydown-platform-exit-neutral-prefix-v1"},
            "action": {"policy": "one-contiguous-horizontal-only-actual-input-episode",
                "maximum_held_pad_rows": 30, "callback_limit_ms": 500,
                "hold_ms": 0, "observe_press": True,
                "movement": {"input_tick": 237}},
            "action_record": {"source_input_tick": 237,
                "observed_pad_sample": {"input_tick": 237, "local_port": 0,
                                         "bytes": [0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 0]}},
            "fall": fall}
        prep = {"result": "pass", "literal_input_window": {"status": "validated"},
            "jab_count": 4, "max_jabs": 8, "final_p1_damage_percent": 18,
            "actual_damage_goal_percent": 18, "max_approach_taps": 16,
            "approach": {"steps": []}, "source_cursor_start": 100,
            "source_cursor_limit": 580, "source_cursor_end": 200,
            "source_cursor_span": 100, "max_source_cursors": 480,
            "baseline": {"source_tick": 100}, "prepared_ko_baseline": baseline}
        ko = {"result": "pass", "input_mode": "cstick-left",
            "input_policy": "post-credit-platform-exit", "input_delay": 3,
            "baseline": copy.deepcopy(baseline), "source_cursor_limit": 680,
            "sequence": {"loss": loss}, "platform_exit": platform_exit,
            "handoff": {"kind": "validated-post-credit-platform-exit-combat-ko-handoff-v1",
                "input_policy": "post-credit-platform-exit", "starting_stocks": 3,
                "respawn_source_tick": 300, "input_window": input_window},
            "preparation": prep}
        route["combat_probe"] = {"result": "pass", "combat_exchange": {"result": "pass"},
                                  "combat_ko": ko}
        kwargs = dict(gameplay_combat_probe=True, gameplay_combat_exchange=True,
                      gameplay_combat_ko_prepared=True,
                      gameplay_combat_ko_prepared_platform_exit=True)
        validate_result(0, self.owner, browser, True, **kwargs)
        mutations = (
            lambda row: row["full_route"]["combat_probe"]["combat_ko"]["platform_exit"]["gate"]
                ["position_by_peer"][0].update(x=1e6, y=-1e6),
            lambda row: row["full_route"]["combat_probe"]["combat_ko"]["handoff"]["input_window"]
                .update(start_input_tick="invalid"),
            lambda row: row["full_route"]["combat_probe"]["combat_ko"]["platform_exit"]["fall"]
                .update(action_input_tick=236),
            lambda row: row["full_route"]["combat_probe"]["combat_ko"]["handoff"]["input_window"]
                .update(through_grounded_wait_input_tick=144),
            lambda row: row["full_route"]["combat_probe"]["combat_ko"]["platform_exit"]["action_record"]
                ["observed_pad_sample"]["bytes"].__setitem__(0, 1),
            lambda row: row["full_route"].update(gameplay_combat_ko_prepared_platform_exit=False),
        )
        for mutate in mutations:
            damaged = copy.deepcopy(browser)
            mutate(damaged)
            with self.assertRaises(ValueError):
                validate_result(0, self.owner, damaged, True, **kwargs)
        legacy = copy.deepcopy(browser)
        legacy["gameplay_combat_ko_prepared_platform_exit"] = False
        legacy["full_route"]["gameplay_combat_ko_prepared_platform_exit"] = False
        legacy["full_route"]["combat_probe"]["combat_ko"].pop("platform_exit")
        legacy["full_route"]["combat_probe"]["combat_ko"].pop("input_policy")
        legacy_ko = legacy["full_route"]["combat_probe"]["combat_ko"]
        legacy_ko["handoff"].pop("input_policy")
        legacy_ko["handoff"]["kind"] = "validated-combat-ko-respawn-handoff-v1"
        legacy_window = legacy_ko["handoff"]["input_window"]
        for row in legacy_window["p1_rows"]:
            row["bytes"] = [0] * 11
        for key in list(legacy_window):
            if key.startswith(("gate_", "direction_key", "facing_direction", "p1_axis",
                               "p1_neutral_prefix", "p1_neutral_release", "position_admission")):
                legacy_window.pop(key)
        validate_result(0, self.owner, legacy, True, gameplay_combat_probe=True,
            gameplay_combat_exchange=True, gameplay_combat_ko_prepared=True)
        mislabeled = copy.deepcopy(legacy)
        mislabeled["full_route"]["combat_probe"]["combat_ko"]["platform_exit"] = platform_exit
        with self.assertRaisesRegex(ValueError, "cannot be relabeled"):
            validate_result(0, self.owner, mislabeled, True, gameplay_combat_probe=True,
                gameplay_combat_exchange=True, gameplay_combat_ko_prepared=True)
        with self.assertRaisesRegex(ValueError, "requires prepared combat KO"):
            validate_result(0, self.owner, browser, True, gameplay_combat_probe=True,
                gameplay_combat_exchange=True, gameplay_combat_ko_prepared_platform_exit=True)


    def test_actual_js_platform_handoff_serializes_into_python_policy(self):
        root = Path(__file__).resolve().parents[1]
        script = r'''
import * as H from './scripts/a3_local_room_combat_ko.mjs';
const pair=(tick,motion,air,afterLoss=false)=>[0,1].map(()=>({
  source_tick:tick,phase:7,running:1,error:null,
  native_status:{active:1,context_applied:1,terminal:{kind:0}},
  match:{frame:tick+100,rules:{is_stock:1,stage:32},ready:true,
    paused:false,complete:false,ending:false,outcome:0,
    players:[0,1].map(port=>({source_port:port,source_player_index:port,
      stocks:afterLoss&&port===0?3:4,motion:port===0?motion:14,
      groundAir:port===0?air:0,damage_percent:0,x:16,y:45,facing_direction:1,
      primary_falls:afterLoss&&port===0?1:0,suicides:0,
      kos_by_player_slot:[afterLoss&&port===1?1:0,0,0,0,0,0]}))}}));
const baseline=H.captureCombatKoBaseline({pairedRows:pair(100,14,0)});
const loss=H.validateCombatKoAttackLoss({baseline,pairedRows:pair(130,0,1,true)});
const gate=H.validateCombatKoPlatformExitGate({baseline,attackLossReceipt:loss,
  pairedRows:pair(140,13,1,true),sourceCursorLimit:580});
const p1Rows=[],p2Rows=[];
for(let tick=96;tick<=156;tick++){
  const a=Array(11).fill(0),b=Array(11).fill(0);
  if(tick>=137&&tick<=139)a[2]=80;
  if(tick>=105&&tick<108)b[4]=176;
  p1Rows.push({input_tick:tick,source_cursor:tick,local_port:0,bytes:a});
  p2Rows.push({input_tick:tick,source_cursor:tick,local_port:1,bytes:b});
}
const prefix=H.validateCombatKoPlatformExitInputPrefix({baseline,gate,
  attackLossReceipt:loss,p1Rows,p2Rows,inputStartTick:96,inputDelay:3,
  p2PressTick:105,p2ReleaseTick:108});
const fall=H.validateCombatKoPlatformExitFall({baseline,gate,attackLossReceipt:loss,
  pairedRows:pair(145,29,1,true),actionInputTick:137,inputDelay:3,sourceCursorLimit:580});
const handoff=H.createCombatKoPlatformExitHandoff({baseline,gate,fallReceipt:fall,
  attackLossReceipt:loss,pairedRows:pair(160,14,0,true),p1Rows,p2Rows,
  inputStartTick:96,inputEndTick:156,p2PressTick:105,p2ReleaseTick:108,
  inputDelay:3,actionStartTick:137,movementEndTick:139,releaseTick:140});
console.log(JSON.stringify({baseline,input_policy:'post-credit-platform-exit',
  input_delay:3,source_cursor_limit:580,sequence:{loss},handoff,
  platform_exit:{result:'pass',gate,pre_keydown_prefix:prefix,
    pre_keydown_readbacks:{accepted:true},fall,
    action:{policy:'one-contiguous-horizontal-only-actual-input-episode',hold_ms:0,
      observe_press:true,callback_limit_ms:500,maximum_held_pad_rows:30},
    action_record:{source_input_tick:137,observed_pad_sample:p1Rows[137-96]}}}));
'''
        result = subprocess.run([str(node_runtime(root)), "--input-type=module", "-e", script],
                                cwd=root, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        actual = json.loads(result.stdout)
        self.assertTrue(_prepared_platform_exit_receipt_ok(actual))
        for mutate in (
            lambda value: value["handoff"]["input_window"].pop("start_input_tick"),
            lambda value: value["handoff"]["input_window"].pop("input_delay"),
            lambda value: value["platform_exit"]["fall"].update(input_delay=0),
            lambda value: value["platform_exit"]["fall"].update(source_tick=140),
        ):
            broken = copy.deepcopy(actual)
            mutate(broken)
            self.assertFalse(_prepared_platform_exit_receipt_ok(broken))


if __name__ == "__main__":
    unittest.main()
