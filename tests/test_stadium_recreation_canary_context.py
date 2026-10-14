"""Run the focused, asset-free A/close/restart/B canary ownership controls."""
import os
from pathlib import Path
import subprocess
import sys

SOURCE_ROOT = Path(__file__).resolve().parents[1]
ROOT = Path(os.environ.get("MELEE_STADIUM_RECREATION_TEST_REPO_ROOT", SOURCE_ROOT)).resolve()
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime


class StadiumRecreationCanaryContextTests(OwnedWorkspaceTests):
    def test_a_close_restart_b_ownership_and_refusal_controls(self):
        scratch = self.new_workspace(ROOT, "stadium-recreation-canary-context-")
        environment = os.environ.copy()
        environment["MELEE_STADIUM_RECREATION_TEST_ROOT"] = str(scratch)
        environment["MELEE_STADIUM_RECREATION_TEST_REPO_ROOT"] = str(ROOT)
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test",
             str(SOURCE_ROOT / "tests/test_stadium_recreation_canary_context.mjs")],
            cwd=ROOT, env=environment, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    import unittest
    unittest.main()
