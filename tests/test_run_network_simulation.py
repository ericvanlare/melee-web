import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from run_network_simulation import validate_result


class NetworkSimulationSuitePolicyTests(unittest.TestCase):
    def setUp(self):
        self.owner = {"schema": "melee-web-capture-process-owner-v3", "result": "pass",
                      "capture_exit_code": 0, "cleanup_passed": True,
                      "process_observation_passed": True}
        self.browser = {"schema": "melee-web-a3-local-room-keyboard-v1", "decision": "PASS",
                        "first_error": None, "relay_mode": "simulated-network",
                        "network_simulation": {"proxy_cleanup": {"verified": True, "final": True}},
                        "gameplay_action_probe": False,
                        "full_route": {"result": "pass", "gameplay_action_probe": False}}

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
