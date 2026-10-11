"""Run the asset-free matched Setup C/F runner admission controls."""
import os
from pathlib import Path
import subprocess
import sys

SOURCE_ROOT = Path(__file__).resolve().parents[1]
ROOT = Path(os.environ.get("MELEE_STADIUM_CF_RUNNER_TEST_REPO_ROOT", SOURCE_ROOT)).resolve()
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime


class StadiumMatchedSetupCfRunnerGateTests(OwnedWorkspaceTests):
    def test_opt_in_and_packet_preflight_refuse_before_server(self):
        scratch = self.new_workspace(ROOT, "stadium-matched-setup-cf-runner-")
        environment = os.environ.copy()
        environment["MELEE_STADIUM_CF_RUNNER_TEST_ROOT"] = str(scratch)
        environment["MELEE_STADIUM_CF_RUNNER_TEST_REPO_ROOT"] = str(ROOT)
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test",
             str(SOURCE_ROOT / "tests/stadium_matched_setup_cf_runner_gate.test.mjs")],
            cwd=ROOT, env=environment, capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_host_plan_and_state_comparator_controls(self):
        host_test = Path(os.environ.get(
            "MELEE_STADIUM_CF_HOST_TEST_SCRIPT",
            ROOT / "tests/test_stadium_matched_setup_cf_consumer.mjs",
        )).resolve()
        self.assertTrue(host_test.is_file(), f"host contract test is missing: {host_test}")
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test", str(host_test)],
            cwd=ROOT, capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    import unittest
    unittest.main()
