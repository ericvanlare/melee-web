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
                        "full_route": {"result": "pass"}}

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


if __name__ == "__main__":
    unittest.main()
