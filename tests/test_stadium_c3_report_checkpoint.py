"""Run asset-free Stadium C3 compact-report checkpoint controls."""
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


class StadiumC3ReportCheckpointTests(OwnedWorkspaceTests):
    def test_compact_report_checkpoint_contract(self):
        environment = os.environ.copy()
        environment["MELEE_STADIUM_OWNER_READER"] = str(ROOT / "scripts/capture_owner_deadline.py")
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test", str(SOURCE_ROOT / "tests/stadium_c3_report_checkpoint.test.mjs")],
            cwd=ROOT, env=environment, capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    import unittest
    unittest.main()
